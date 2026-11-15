#include "hidrolab_simulation_runtime.h"
#include "hidrolab_simulation_store.h"
#include "hidrolab_science_questions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_spiffs.h"
#include <string.h>
#include "esp_timer.h"
static int64_t session_started_ms;
static QueueHandle_t queue;static SemaphoreHandle_t mutex;static TaskHandle_t worker;
static sim_snapshot state,published;static sim_state before;static sim_transaction pending;
static uint32_t sequence;static bool mounted;static volatile bool suspend_requested;
static void publish(void){xSemaphoreTake(mutex,portMAX_DELAY);published=state;xSemaphoreGive(mutex);}
static bool checkpoint(void){if(!mounted||!sim_store_save(&state.state,sequence+1)){state.result=-2;return false;}sequence++;return true;}
static void handle(sim_command c){
 bool save=false,ok=true,auto_run=false;before=state.state;
 if(c.kind==SC_START_EXPERIMENT){
  if(state.running||c.change.kind!=1||!c.change.new_run||!sim_apply(&state.state,&c.change)){state.result=-1;state.notice++;publish();return;}
  state.running=0;state.pending=0;pending.active=0;state.completed=0;state.session=0;state.elapsed_ms=0;
  if(checkpoint()){
   state.result=0;state.session=1;state.running=1;
   session_started_ms=esp_timer_get_time()/1000;
  }else state.state=before;
  state.notice++;publish();return;
 }
 /* A timed experiment has fixed conditions and speed until it finishes. */
 if(state.session&&state.running&&c.kind!=SC_SAVE)return;
 if(c.kind==SC_PROPOSE){
  if(!sim_transaction_begin(&state.state,&pending,&c.change)){ok=false;goto done;}
  state.running=0;state.question=pending.question;state.pending=1;state.token=pending.token;state.result=0;goto done;
 }
 if(c.kind==SC_ANSWER){
  if(!state.pending||c.token!=state.token||c.value>2)return;
  state.pending=0;state.result=sim_transaction_answer(&state.state,&pending,c.token,c.value);ok=state.result!=0;
  auto_run=state.result==1&&pending.change.kind==1;
  save=true;goto done;
 }
 if(state.pending&&c.kind!=SC_PAUSE&&c.kind!=SC_SAVE){ok=false;goto done;}
 switch(c.kind){
 case SC_RUN:state.speed=(c.value==2||c.value==4)?c.value:1;state.running=state.state.hour<SIM_DAYS*24;break;
 case SC_PAUSE:state.running=0;save=true;break;
 case SC_DAY:state.running=0;for(unsigned i=0;i<24;i++)sim_step_hour(&state.state);save=true;break;
 case SC_HARVEST:ok=sim_harvest(&state.state,c.value);save=ok;break;
 case SC_SHIP:ok=sim_ship(&state.state);save=ok;break;
 case SC_SHIELD:if(!(state.state.upgrades&8)){ok=false;break;}state.state.shield=!state.state.shield;state.state.revision++;save=true;break;
 case SC_SAVE:save=true;break;
 default:ok=false;break;
 }
 if(ok)state.result=0;
done:
 if(!ok)state.result=-1;
 if(save&&ok&&!checkpoint()){state.state=before;state.running=0;state.pending=0;}
 /* Start only after a successful experiment commit AND durable checkpoint. */
 if(auto_run&&state.result==1){state.speed=1;state.running=state.state.hour<SIM_DAYS*24;}
 state.notice++;publish();
}
/* Derive progress from monotonic elapsed time, not scheduler tick counts.
 * Keeping the legacy 90-day storage capacity preserves existing save files. */
static void advance_session(int64_t now_ms){
 if(!state.session||!state.running)return;
 int64_t elapsed=now_ms-session_started_ms;
 if(elapsed<0)elapsed=0;
 if(elapsed>SIM_SESSION_MS)elapsed=SIM_SESSION_MS;
 state.elapsed_ms=(uint32_t)elapsed;
 unsigned target=(unsigned)(elapsed*(SIM_SESSION_DAYS*24u)/SIM_SESSION_MS);
 while(state.state.hour<target)sim_step_hour(&state.state);
 if(elapsed==SIM_SESSION_MS){
  state.running=0;state.completed=1;
  sim_harvest(&state.state,0);sim_harvest(&state.state,1);
  state.result=0;checkpoint();state.notice++;
 }
 publish();
}
static void task(void *unused){
 (void)unused;sim_init(&state.state);state.speed=1;
 const esp_vfs_spiffs_conf_t conf={.base_path="/spiffs",.partition_label="spiffs",.max_files=3,.format_if_mount_failed=false};
 esp_err_t err=esp_vfs_spiffs_register(&conf);mounted=err==ESP_OK||err==ESP_ERR_INVALID_STATE;
 if(mounted){int loaded=sim_store_load(&state.state,&sequence);if(loaded<0){state.result=-3;state.notice++;}}else{state.result=-2;state.notice++;}
 state.ready=1;publish();TickType_t last=xTaskGetTickCount(),legacy_last=last;
 for(;;){
  sim_command c;if(xQueueReceive(queue,&c,pdMS_TO_TICKS(25)))handle(c);
  if(suspend_requested){state.running=0;state.pending=0;pending.active=0;suspend_requested=false;checkpoint();state.notice++;publish();}
  TickType_t now=xTaskGetTickCount();if(now-last>=pdMS_TO_TICKS(50)){last=now;if(state.session){advance_session(esp_timer_get_time()/1000);}else if(state.running&&now-legacy_last>=pdMS_TO_TICKS(250)){legacy_last=now;for(unsigned i=0;i<state.speed;i++)sim_step_hour(&state.state);if(state.state.hour>=SIM_DAYS*24){state.running=0;checkpoint();}publish();}}
 }
}
bool sim_runtime_init(void){
 if(worker)return true;
 queue=xQueueCreate(8,sizeof(sim_command));mutex=xSemaphoreCreateMutex();
 if(!queue||!mutex){if(queue)vQueueDelete(queue);if(mutex)vSemaphoreDelete(mutex);queue=NULL;mutex=NULL;return false;}
 sim_init(&published.state);published.speed=1;
 if(xTaskCreate(task,"ay_serasi",6144,NULL,2,&worker)!=pdPASS){vQueueDelete(queue);vSemaphoreDelete(mutex);queue=NULL;mutex=NULL;worker=NULL;return false;}return true;
}
bool sim_runtime_send(sim_command c){return queue&&xQueueSend(queue,&c,0)==pdTRUE;}
bool sim_runtime_snapshot(sim_snapshot *out){if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return false;*out=published;xSemaphoreGive(mutex);return true;}
void sim_runtime_suspend(void){suspend_requested=true;}
