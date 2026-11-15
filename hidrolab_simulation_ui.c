#include "hidrolab_simulation_ui.h"
#include "hidrolab_simulation_runtime.h"
#include "hidrolab_science_questions.h"
#include "hidrolab_nasa_data.h"
#include "hidrolab_experiment_references.h"
#include "hidrolab_game_assets.h"
#include "hidrolab_greenhouse_frames.h"
#include "hidrolab_font_tr.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

enum { INTRO, SETUP, LIVE, RESULT, NASA_INFO, NASA_DATA, QUIZ_SUMMARY, QUIZ, QUIZ_END };
static lv_obj_t *root,*slider,*value_label,*room_value[2],*lamp[2],*beam[2],*plant[2],*room_info[2],*dots[2][12],*progress,*clock_label,*start_button,*rgb_sliders[3],*rgb_labels[3],*roots[2][3],*seeds[2],*buds[2][3];
static float shown_growth[2];
static sim_condition painted_condition[2];
static bool condition_painted[2];
static lv_timer_t *timer;
static sim_snapshot snap;
static sim_condition draft[2];
static unsigned page,room,crop,mode,field,quiz_index,score,notice;
static bool dirty,closing,starting,finished_seen,answered;
static unsigned chosen;
static bool preview_pending;
static lv_obj_t *live_hint;
static unsigned last_live_slice=0xffffffffu;
static unsigned preview_room,room_paint_count;
static const sim_question variable_questions[]={
 {"pH deneyinde değiştirdiğimiz koşul hangisi?",{"Suyun asitlik düzeyi","Işık rengi","Bitki türü"},"pH suyun asitlik düzeyini anlatır. Diğer koşulları aynı tutarak karşılaştırırız.",0,0,0},
 {"EC bize ne hakkında bilgi verir?",{"Yaprak sayısı","Çözeltinin elektrik iletkenliği","Işık süresi"},"EC çözeltinin iletkenliğidir. Tek başına hangi besinlerin bulunduğunu söylemez.",1,0,0},
 {"Işık şiddeti deneyinde neyi değiştiririz?",{"Saksının adı","Gün sayısı","Bitkiye ulaşan ışık miktarı"},"PPFD birim alana, bir saniyede ulaşan fotosentetik foton akısını gösterir.",2,0,0},
 {"Aynı ışık şiddetinde süre uzarsa ne artar?",{"Günlük toplam ışık","Suyun pH değeri","Bitkinin yaşı birden 60 olur"},"Günlük toplam ışık, ışık şiddetine ve aydınlatma süresine bağlıdır.",0,0,0},
 {"Işık rengi deneyinde neyi karıştırdık?",{"Su ve toprak","Kırmızı, yeşil ve mavi ışık","Üç farklı bitki"},"Renk karışımıyla toplam ışık şiddeti ayrı ayarlardır. Birini değiştirmek diğerini otomatik artırmaz.",1,0,0},
 {"Sıcaklık deneyinde hangi koşulu değiştiririz?",{"Hasat düğmesini","Saksı sayısını","Seranın sıcaklığını"},"Sıcaklığı değiştirip diğer koşulları aynı tutarız; sonra gelişimi karşılaştırırız.",2,0,0},
 {"Birçok koşul aynı anda değişirse ne diyebiliriz?",{"Tek neden belirleyemeyiz","Sonucu yalnız pH yaptı","Her zaman kırmızı ışık kazandı"},"Tek bir koşulun etkisini incelemek için diğer koşulları aynı tutmak gerekir.",0,0,0}
};
static const char *const lesson_notes[]={
 "pH suyun asitlik düzeyini anlatır. Bu deneyde pH'ı değiştirip\ndiğer koşulları aynı tutuyoruz. Sonuçta gelişimi karşılaştırıyoruz.",
 "EC, besin çözeltisinin elektrik iletkenliğidir. EC sayısı tek başına\nhangi besinlerin bulunduğunu söylemez. Daha yüksek her zaman daha iyi değildir.",
 "Işık şiddeti bitkiye ulaşan ışık miktarıyla ilgilidir. PPFD, birim alana\nbir saniyede gelen fotosentetik fotonları anlatır. Süre ayrı bir ayardır.",
 "Bitkinin bir günde aldığı toplam ışık, ışık şiddetine ve süreye bağlıdır.\nAynı şiddette daha uzun süre, daha çok günlük ışık demektir.",
 "Kırmızı, yeşil ve mavi ışığı karıştırabiliriz. Renk dağılımı ile\ntoplam ışık şiddeti ayrı ayarlardır. Üç kanal da sıfırsa lamba söner.",
 "Bu deneyde seranın sıcaklığını değiştiriyoruz. Diğer koşulları\naynı tutarak bitkilerin gelişimini karşılaştırabiliriz.",
 "Serbest deneyde birden fazla koşul değişebilir. Sonuç farklıysa,\nfarkın tek bir koşuldan kaynaklandığını söyleyemeyiz."
};
static const sim_question *current_question(void){return quiz_index==0?&variable_questions[snap.state.mode]:&experiment_references[experiment_reference_id(&snap.state)].question[quiz_index-1];}

static const char *const modes[]={"pH","EC","Işık şiddeti","Işık süresi","Işık rengi","Sıcaklık","Serbest deney"};

static const int limits[]={140,60,1000,24,2,45};
static void draw(void);
static void action(lv_event_t *e);
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,unsigned color){
 lv_obj_t *o=lv_obj_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,14,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);return o;
}
static lv_obj_t *text(lv_obj_t *p,const char *s,int x,int y,int w){
 lv_obj_t *o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,&hidrolab_font_tr_14,0);lv_obj_set_style_text_color(o,lv_color_hex(0xE0EDF5),0);lv_label_set_text(o,s);return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *s,int x,int y,int w,int h,unsigned id,unsigned color){
 lv_obj_t *o=lv_button_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,12,0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
 lv_obj_add_event_cb(o,action,LV_EVENT_CLICKED,(void *)(uintptr_t)id);lv_obj_t *l=text(o,s,0,0,w-20);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);return o;
}
static lv_obj_t *picture(lv_obj_t *p,const lv_image_dsc_t *src,int x,int y){lv_obj_t *o=lv_image_create(p);lv_image_set_src(o,src);lv_obj_set_pos(o,x,y);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;}
static float get(sim_condition c,unsigned k){return k==0?c.ph:k==1?c.ec:k==2?c.ppfd:k==3?c.hours:k==4?(float)c.spectrum:c.temp;}
static void put(sim_condition *c,unsigned k,float v){if(k==0)c->ph=v;else if(k==1)c->ec=v;else if(k==2)c->ppfd=v;else if(k==3)c->hours=v;else if(k==4)c->spectrum=(unsigned)v;else c->temp=v;}
static unsigned active_field(void){return mode==SIM_FREE?field:mode;}
static void equalize(void){if(mode!=SIM_FREE)for(unsigned k=0;k<6;k++)if(k!=mode){if(k==SIM_SPECTRUM)draft[1].spectrum=draft[0].spectrum;else put(&draft[1],k,get(draft[0],k));}}
static void format_value(char *b,size_t n,sim_condition c,unsigned k){
 if(k==0)snprintf(b,n,"pH %.1f",c.ph);else if(k==1)snprintf(b,n,"EC %.1f mS/cm",c.ec);else if(k==2)snprintf(b,n,"%.0f umol/m2/s",c.ppfd);else if(k==3)snprintf(b,n,"%.0f saat / gün",c.hours);else if(k==4){unsigned r,g,blue;sim_spectrum_rgb(c.spectrum,&r,&g,&blue);snprintf(b,n,"K%u M%u Y%u",r,blue,g);}else snprintf(b,n,"%.0f °C",c.temp);
}
static void set_text(lv_obj_t *o,const char *s){if(strcmp(lv_label_get_text(o),s))lv_label_set_text(o,s);}
static void paint_room(unsigned i,sim_condition c,const sim_plant *p){
 room_paint_count++;
 if(!condition_painted[i]||memcmp(&painted_condition[i],&c,sizeof(c))){
  unsigned r,g,b;sim_spectrum_rgb(c.spectrum,&r,&g,&b);unsigned max=r>g?r:g;if(b>max)max=b;
  unsigned col=max?((r*255/max)<<16)|((g*255/max)<<8)|(b*255/max):0;
  float ppfd=sim_effective_ppfd(c);
  lv_obj_set_style_bg_color(lamp[i],lv_color_mix(lv_color_hex(col),lv_color_hex(0x091C2C),(lv_opa_t)(30+225*ppfd/1000)),0);
  lv_obj_set_style_bg_color(beam[i],lv_color_mix(lv_color_hex(col),lv_color_hex(0x091C2C),(lv_opa_t)(80*ppfd/1000)),0);
  unsigned count=(unsigned)lroundf(c.ec*2);for(unsigned j=0;j<12;j++){if(j<count)lv_obj_remove_flag(dots[i][j],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(dots[i][j],LV_OBJ_FLAG_HIDDEN);}
  char info[170];snprintf(info,sizeof(info),"pH %.1f | EC %.1f\nIşık %.0f PPFD\n%.0f sa/gün | %.0f °C",c.ph,c.ec,ppfd,c.hours,c.temp);set_text(room_info[i],info);
  painted_condition[i]=c;condition_painted[i]=true;
 }
 char label[100];
 if(p){
  shown_growth[i]=p->progress;
  float growth=shown_growth[i];
  unsigned stage=(unsigned)(growth*15);if(stage>15)stage=15;
  const lv_image_dsc_t *src=greenhouse_growth_frames[stage];
  if(lv_image_get_src(plant[i])!=src){
   lv_image_set_src(plant[i],src);
   lv_obj_set_pos(plant[i],85-(int)src->header.w/2,118-(int)src->header.h);
  }
  if(growth<.025f){lv_obj_add_flag(plant[i],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(seeds[i],LV_OBJ_FLAG_HIDDEN);}else{lv_obj_remove_flag(plant[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(seeds[i],LV_OBJ_FLAG_HIDDEN);}
  for(unsigned j=0;j<3;j++){
   int height=(int)(2+growth*(19-j*4));if(lv_obj_get_height(roots[i][j])!=height)lv_obj_set_height(roots[i][j],height);
   if(crop&&growth>.65f){lv_obj_remove_flag(buds[i][j],LV_OBJ_FLAG_HIDDEN);lv_color_t bud_color=lv_color_hex(growth>.85f?0xF47564:0xE8D783);if(!lv_color_eq(lv_obj_get_style_bg_color(buds[i][j],0),bud_color))lv_obj_set_style_bg_color(buds[i][j],bud_color,0);}else lv_obj_add_flag(buds[i][j],LV_OBJ_FLAG_HIDDEN);
  }
  snprintf(label,sizeof(label),"%s\nGelişim: %.0f%%",sim_stage(p),p->progress*100);
 }else format_value(label,sizeof(label),c,active_field());
 set_text(room_value[i],label);
}
static void room_card(unsigned i,int y,int h,bool editable){
 lv_obj_t *c=box(root,16+i*392,y,376,h,i?0x123A37:0x15334B);
 lv_obj_t *badge=box(c,12,5,26,25,i?0x287D69:0x356BA0);lv_obj_t *letter=text(badge,i?"B":"A",0,4,26);lv_obj_set_style_text_align(letter,LV_TEXT_ALIGN_CENTER,0);
 text(c,i?"DENEY ODASI":"BAŞLANGIÇ ODASI",48,10,314);
 lv_obj_t *glass=box(c,12,34,170,156,0x091C2C);
 lv_obj_set_style_border_width(glass,1,0);lv_obj_set_style_border_color(glass,lv_color_hex(0x416F84),0);
 lamp[i]=box(glass,22,8,126,12,0xFFF1B0);beam[i]=box(glass,12,23,146,94,0xFFF1B0);
 box(glass,8,120,154,28,0x17536B);box(glass,10,118,150,2,0x53BCBF);
 for(unsigned j=0;j<3;j++)roots[i][j]=box(glass,76+j*8,123,2,2,0xADC6A2);
 for(unsigned j=0;j<12;j++)dots[i][j]=box(glass,22+(j%6)*24,128+(j/6)*11,4,4,0x74D9DC);
 plant[i]=picture(glass,&hidrolab_img_garden_sprout,53,54);lv_image_set_pivot(plant[i],32,64);lv_image_set_antialias(plant[i],false);
 seeds[i]=box(glass,80,110,10,7,0xD4AF79);lv_obj_add_flag(seeds[i],LV_OBJ_FLAG_HIDDEN);
 for(unsigned j=0;j<3;j++){buds[i][j]=box(glass,64+j*17,64+(j%2)*14,7,7,0xE8D783);lv_obj_add_flag(buds[i][j],LV_OBJ_FLAG_HIDDEN);}
 room_value[i]=text(c,"",194,42,170);room_info[i]=text(c,"",194,98,170);
 text(c,editable?(i==room?"Seçili oda / sürgüyü kaydır":"Bu odayı ayarlamak için dokun"):"Işık / kökler / bitki gelişimi",16,197,344);
 if(editable){lv_obj_add_flag(c,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(c,action,LV_EVENT_CLICKED,(void *)(uintptr_t)(20+i));if(i==room){lv_obj_set_style_outline_width(c,2,0);lv_obj_set_style_outline_color(c,lv_color_hex(0x54D8B0),0);}}
 condition_painted[i]=false;shown_growth[i]=0;paint_room(i,editable?draft[i]:snap.state.conditions[i],editable?NULL:&snap.state.plant[i]);
}
static void rgb_changed(lv_event_t *e){
 (void)e;unsigned r=lv_slider_get_value(rgb_sliders[0]),g=lv_slider_get_value(rgb_sliders[1]),b=lv_slider_get_value(rgb_sliders[2]);
 draft[room].spectrum=sim_spectrum_pack(r,g,b);preview_room=room;preview_pending=true;
 const char *names[]={"Kırmızı","Yeşil","Mavi"};unsigned v[]={r,g,b};char label[40];
 for(unsigned k=0;k<3;k++){snprintf(label,sizeof(label),"%s %u",names[k],v[k]);set_text(rgb_labels[k],label);}
}
static lv_obj_t *track(int x,int y,int w,int max,int value,unsigned color,lv_event_cb_t cb){
 lv_obj_t *o=lv_slider_create(root);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,12);lv_obj_set_ext_click_area(o,16);
 lv_obj_set_style_bg_color(o,lv_color_hex(0x295065),LV_PART_MAIN);lv_obj_set_style_bg_opa(o,255,LV_PART_MAIN);lv_obj_set_style_radius(o,20,LV_PART_MAIN);
 lv_obj_set_style_bg_opa(o,0,LV_PART_INDICATOR);lv_obj_set_style_bg_color(o,lv_color_hex(color),LV_PART_KNOB);lv_obj_set_style_bg_opa(o,255,LV_PART_KNOB);lv_obj_set_style_pad_all(o,10,LV_PART_KNOB);lv_obj_set_style_radius(o,20,LV_PART_KNOB);
 lv_slider_set_range(o,0,max);lv_slider_set_value(o,value,LV_ANIM_OFF);lv_obj_add_event_cb(o,cb,LV_EVENT_VALUE_CHANGED,NULL);return o;
}
static void slider_changed(lv_event_t *e){
 (void)e;unsigned k=active_field();float v=(float)lv_slider_get_value(slider);if(k<2)v/=10;put(&draft[room],k,v);
 char b[90];format_value(b,sizeof(b),draft[room],k);set_text(value_label,b);preview_room=room;preview_pending=true;
}
static void intro(void){
 /* Static lunar view: atmosphere without a costly full-screen animation. */
 for(unsigned j=0;j<22;j++)box(root,22+(j*97)%756,12+(j*43)%350,2+(j%2),2+(j%2),0x45677E);
 text(root,"AY SERASI / KEŞİF GÖREVİ",28,24,730);
 text(root,"Aynı bitki. İki oda. Sonucu sen keşfet.",28,62,730);
 lv_obj_t *scene=box(root,20,110,350,270,0x102C40);
 box(scene,18,210,314,42,0x284459);lv_obj_t *moon=box(scene,242,16,62,62,0xA4C2CC);lv_obj_set_style_radius(moon,LV_RADIUS_CIRCLE,0);box(scene,254,24,13,10,0x7F9AA8);box(scene,276,42,17,14,0x7F9AA8);
 for(unsigned i=0;i<2;i++){
  lv_obj_t *pod=box(scene,24+i*158,82,140,152,0x163F4B);lv_obj_set_style_border_width(pod,1,0);lv_obj_set_style_border_color(pod,lv_color_hex(i?0x63C7AA:0x83B9E4),0);
  box(pod,20,12,100,8,i?0xBBA5E2:0xE5C885);picture(pod,i?&hidrolab_img_garden_other_plant:&hidrolab_img_garden_sprout,38,38);box(pod,14,112,112,18,0x216578);
 }
 text(scene,"60 günlük yaşam / 30 saniyede",20,20,215);
 const char *title[]={"01  KOŞULLARI SEÇ","02  BÜYÜMEYİ GÖZLE","03  KANITLARI KARŞILAŞTIR"};
 const char *copy[]={"Bir koşulu değiştir.\nSence hangi bitki daha hızlı büyür?","Kökleri ve yaprakları izle.\nHer saniye iki gün geçecek.","Hasadı ve olgunlaşma gününü karşılaştır.\nSonra gerçek NASA deneyini keşfet."};
 for(unsigned i=0;i<3;i++){text(root,title[i],396,118+i*88,380);text(root,copy[i],396,146+i*88,380);}
 text(root,"Bu bir simülasyondur; gerçek bitkiler 30 saniyede büyümez.",100,390,690);
 button(root,"Deneye başla",200,426,400,44,1,0x247C62);
}
static void setup(void){
 char b[100];snprintf(b,sizeof(b),"Bitki: %s",sim_profiles[crop].name);button(root,b,16,58,240,44,10,0x285477);
 snprintf(b,sizeof(b),"Değişken: %s",modes[mode]);button(root,b,268,58,300,44,11,0x285477);
 if(mode==SIM_FREE)button(root,modes[field],580,58,204,44,12,0x285477);else{
  const char *hint[]={"Suyun asitliği\nA/B kartına dokun","Besin suyu iletkenliği\nNoktalar EC'yi gösterir","Lambanın parlaklığı\nIşık miktarını değiştir","Günlük aydınlık süresi\nKaç saat ışık olsun?","Üç rengi karıştır\nA/B kartına dokun","Seranın sıcaklığı\nA/B kartına dokun"};
  text(root,hint[mode],584,62,198);
 }
 room_card(0,114,218,true);room_card(1,114,218,true);
 unsigned k=active_field();
 if(k==SIM_SPECTRUM){
  unsigned r,g,blue;sim_spectrum_rgb(draft[room].spectrum,&r,&g,&blue);unsigned v[]={r,g,blue};unsigned cols[]={0xF17C7C,0x54D8B0,0x7CACFF};const char *names[]={"Kırmızı","Yeşil","Mavi"};
  for(unsigned j=0;j<3;j++){snprintf(b,sizeof(b),"%s %u",names[j],v[j]);rgb_labels[j]=text(root,b,32+j*260,346,230);rgb_sliders[j]=track(42+j*260,390,196,100,v[j],cols[j],rgb_changed);}
 }else{
  text(root,room?"B odasının ayarı":"A odasının ayarı",20,348,360);format_value(b,sizeof(b),draft[room],k);value_label=text(root,b,430,348,350);lv_obj_set_style_text_align(value_label,LV_TEXT_ALIGN_RIGHT,0);
  slider=track(40,391,720,limits[k],(int)lroundf(get(draft[room],k)*(k<2?10:1)),0x54D8B0,slider_changed);
 }
 start_button=button(root,starting?"Deney hazırlanıyor...":"Deneyi başlat / 30 saniye",200,426,400,44,15,0x247C62);
 if(starting)lv_obj_add_state(start_button,LV_STATE_DISABLED);
}
static void update_live(void){
 unsigned slice=snap.elapsed_ms/250;if(slice==last_live_slice)return;last_live_slice=slice;
 char b[120];snprintf(b,sizeof(b),"Gün %u / 60   |   %u saniye kaldı",(unsigned)snap.state.hour/24,(unsigned)((SIM_SESSION_MS-snap.elapsed_ms+999)/1000));set_text(clock_label,b);
 int pct=(int)(snap.elapsed_ms*100/SIM_SESSION_MS);if(lv_bar_get_value(progress)!=pct)lv_bar_set_value(progress,pct,LV_ANIM_OFF);
 for(unsigned i=0;i<2;i++)paint_room(i,snap.state.conditions[i],&snap.state.plant[i]);
 const sim_plant *a=&snap.state.plant[0],*c=&snap.state.plant[1];const char *hint;
 if(a->progress>=.9999f&&c->progress>=.9999f)hint="İki bitki de olgunlaştı. 60. günde otomatik hasat yapılacak.";
 else if(a->progress<.025f&&c->progress<.025f)hint="Henüz filiz yok. Ayarladığın koşullarda gelişimi izliyoruz.";
 else if(fabsf(a->progress-c->progress)>.03f)hint=a->progress>c->progress?"A daha ileride. B'nin yaprak ve kök gelişimiyle karşılaştır.":"B daha ileride. A'nın yaprak ve kök gelişimiyle karşılaştır.";
 else hint="Gelişimleri birbirine yakın. Yaprak büyüklüğü ve yüzdeleri karşılaştır.";
 set_text(live_hint,hint);
}
static void live_screen(void){
 clock_label=text(root,"",20,66,465);
 lv_obj_t *tag=box(root,510,56,274,44,0x24485C);
 const char *icons[]={"pH","EC","IŞIK","SAAT","RGB","°C","A/B"};
 lv_obj_t *badge=box(tag,6,6,54,32,0x326C7D);lv_obj_t *symbol=text(badge,icons[snap.state.mode],0,8,54);lv_obj_set_style_text_align(symbol,LV_TEXT_ALIGN_CENTER,0);
 text(tag,modes[snap.state.mode],70,14,194);
 room_card(0,112,218,false);room_card(1,112,218,false);
 lv_obj_t *guide=box(root,16,344,768,124,0x142F43);
 text(guide,"GÖZLEM AKIŞI",16,12,350);text(guide,"1 saniye = 2 gün",580,12,172);
 progress=lv_bar_create(root);lv_obj_set_pos(progress,32,383);lv_obj_set_size(progress,736,12);lv_bar_set_range(progress,0,100);
 lv_obj_set_style_bg_color(progress,lv_color_hex(0x245069),LV_PART_MAIN);lv_obj_set_style_bg_color(progress,lv_color_hex(0x54D8B0),LV_PART_INDICATOR);
 live_hint=text(root,"",32,416,736);last_live_slice=0xffffffffu;update_live();
}
static unsigned maturity_day(const sim_state *s,unsigned r){
 for(unsigned d=0;d<s->history_count;d++){if(s->history[d].progress[r]>=99.99f)return d;}
 return 0;
}
/* The reasoning and the mini graph use the SAME experiment state as the
 * growth engine. No separate invented results are displayed. */
static void result_explanation(char *out,size_t n,const sim_state *s){
 const sim_condition a=s->initial[0],b=s->initial[1];
 const sim_profile *p=&sim_profiles[s->crop];
 unsigned diff=sim_differences(a,b);
 unsigned da=maturity_day(s,0),db=maturity_day(s,1);
 char why[245];why[0]='\0';
 if(!diff){
  snprintf(why,sizeof(why),"İki odanın koşulları aynıydı.\nBu yüzden belirgin fark beklenmez.");
 }else if(s->mode==SIM_FREE || diff>1){
  snprintf(why,sizeof(why),"Birden fazla koşul değişmiş.\nFarkı tek bir nedene bağlayamayız.");
 }else if(s->mode==SIM_PPFD||s->mode==SIM_PERIOD){
  float dl_a=sim_dli(sim_effective_ppfd(a),a.hours);
  float dl_b=sim_dli(sim_effective_ppfd(b),b.hours);
  float q_a=sim_light_response(a,p),q_b=sim_light_response(b,p);
  snprintf(why,sizeof(why),"Günlük ışık: A %.1f, B %.1f\nmol/m2. Modelin ışık uygunluğu\nA %%%.0f, B %%%.0f.\nAz ya da aşırı ışık büyümeyi\nyavaşlatabilir.",dl_a,dl_b,100.f*q_a,100.f*q_b);
 }else if(s->mode==SIM_SPECTRUM){
  float q_a=sim_spectrum_response(a,s->crop);
  float q_b=sim_spectrum_response(b,s->crop);
  snprintf(why,sizeof(why),"Kırmızı/mavi ışık karışımı farklı.\nEğitsel renk katsayısı:\nA %.2f, B %.2f. Bu katsayılar\nNASA verim ölçümü değil,\noyunun örnek varsayımıdır.",q_a,q_b);
 }else if(s->mode==SIM_PH){
  snprintf(why,sizeof(why),"pH: A %.1f / B %.1f. Bitkinin\nörnek uygun aralığı %.1f–%.1f.\nAralık dışında modelde büyüme\nyavaşlar.",a.ph,b.ph,p->ph_lo,p->ph_hi);
 }else if(s->mode==SIM_EC){
  snprintf(why,sizeof(why),"EC: A %.1f / B %.1f mS/cm.\nModelin uygun aralığı %.1f–%.1f.\nÇok düşük/yüksek EC, büyümeyi\nyavaşlatabilir.",a.ec,b.ec,p->ec_lo,p->ec_hi);
 }else{
  snprintf(why,sizeof(why),"Sıcaklık: A %.0f / B %.0f °C.\nModelin uygun aralığı %.0f–%.0f °C.\nBu aralıktan uzaklaşmak\nbüyümeyi yavaşlatabilir.",a.temp,b.temp,p->temp_lo,p->temp_hi);
 }
 if(da&&db&&da!=db){
  snprintf(out,n,"%s\n\nOlgunlaşma: A %u. gün, B %u. gün.",why,da,db);
 }else if(da&&!db){
  snprintf(out,n,"%s\n\nA olgunlaştı, B henüz değil.",why);
 }else if(!da&&db){
  snprintf(out,n,"%s\n\nB olgunlaştı, A henüz değil.",why);
 }else{
  snprintf(out,n,"%s",why);
 }
}
static void result_screen(void){
 const sim_state *s=&snap.state;
 char b[450];
 snprintf(b,sizeof(b),"DENEY SONUCU  /  %s",modes[s->mode]);
 text(root,b,20,61,760);
 for(unsigned i=0;i<2;i++){
  lv_obj_t *card=box(root,16+(int)i*392,92,376,68,i?0x16443D:0x193B55);
  text(card,i?"B • DENEY ODASI":"A • KONTROL ODASI",12,8,347);
  unsigned day=maturity_day(s,i);
  if(day)snprintf(b,sizeof(b),"Gelişim %%%.0f  |  %.0f g  |  %u. gün olgun",s->plant[i].progress*100.f,s->plant[i].mass_g,day);
  else snprintf(b,sizeof(b),"Gelişim %%%.0f  |  %.0f g  |  Henüz olgun değil",s->plant[i].progress*100.f,s->plant[i].mass_g);
  text(card,b,12,35,348);
 }
 lv_obj_t *left=box(root,16,174,458,232,0x142E42);
 text(left,"GELİŞİM GRAFİĞİ",16,9,202);
 text(left,"A: mavi     B: yeşil",231,9,212);
 lv_obj_t *chart=lv_chart_create(left);
 lv_obj_set_pos(chart,36,47);lv_obj_set_size(chart,406,148);
 lv_obj_set_style_bg_color(chart,lv_color_hex(0x0D2537),0);
 lv_obj_set_style_border_width(chart,0,0);
 lv_obj_set_style_pad_all(chart,7,0);
 lv_obj_set_style_size(chart,0,0,LV_PART_INDICATOR);
 lv_chart_set_type(chart,LV_CHART_TYPE_LINE);
 unsigned count=s->history_count;
 lv_chart_set_point_count(chart,count>=2?count:2);
 lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,100);
 for(unsigned r=0;r<2;r++){
  lv_chart_series_t *series=lv_chart_add_series(chart,lv_color_hex(r?0x52E0AB:0x66B1FF),LV_CHART_AXIS_PRIMARY_Y);
  for(unsigned d=0;d<(count>=2?count:2);d++){
   int y=d<count?(int)lroundf(s->history[d].progress[r]):LV_CHART_POINT_NONE;
   lv_chart_set_value_by_id(chart,series,d,y);
  }
 }
 lv_chart_refresh(chart);
 text(left,"%100",2,45,35);text(left,"%0",6,190,35);
 snprintf(b,sizeof(b),"0. gün                                %u. gün",count?count-1:0);
 text(left,b,37,210,405);

 lv_obj_t *right=box(root,486,174,298,232,0x19384A);
 text(right,"NEDEN BÖYLE OLDU?",14,10,270);
 char explanation[380];result_explanation(explanation,sizeof(explanation),s);
 text(right,explanation,14,43,270);
 float delta=s->plant[1].mass_g-s->plant[0].mass_g;
 snprintf(b,sizeof(b),"Tahmini kütle farkı (B-A): %+.0f g",delta);
 text(root,b,20,410,440);
 text(root,"Eğitsel model; NASA verimi değil.",475,410,310);
 button(root,"NASA araştırmasına bak",180,438,440,34,30,0x247C62);
}
static const sim_reference *reference(void){return &experiment_references[experiment_reference_id(&snap.state)];}
static void nasa_screen(void){
 const sim_reference *r=reference();text(root,r->title,20,65,760);text(root,r->match,20,100,760);
 lv_obj_t *c=box(root,16,150,768,154,0x17364F);picture(c,&hidrolab_img_garden_leaf,18,24);
 text(c,"Araştırmacılar neyi denedi?",104,18,640);text(c,r->protocol,104,58,640);
 char b[140];snprintf(b,sizeof(b),"Senin deneyin: %s / %s",sim_profiles[snap.state.crop].name,modes[snap.state.mode]);text(root,b,20,324,760);
 text(root,r->limit,20,354,760);
 button(root,"Çalışmanın bulgusunu gör",180,426,440,44,33,0x247C62);
}
static void nasa_data_screen(void){
 const sim_reference *r=reference();text(root,"BU DENEYLE İLGİLİ ARAŞTIRMA BULGUSU",20,64,760);
 lv_obj_t *c=box(root,16,106,768,168,0x17364F);text(c,r->title,20,18,728);text(c,r->finding,20,66,728);
 text(root,r->limit,20,294,760);text(root,"Kaynak / çalışmayı yeniden bulabilmen için:",20,354,760);text(root,r->url,20,383,760);
 button(root,"Bu çalışmanın özet ve soruları",180,433,440,44,31,0x247C62);
}
static void quiz_summary(void){
 char b[80];snprintf(b,sizeof(b),"SORUDAN ÖNCE / %u / 3",quiz_index+1);text(root,b,24,70,752);
 lv_obj_t *c=box(root,24,126,752,238,0x17364F);picture(c,quiz_index?&hidrolab_img_garden_leaf:&hidrolab_img_garden_sprout,22,24);
 text(c,quiz_index==0?"Deneyinden hatırla":"Seçilen çalışmadan hatırla",110,34,610);
 const char *note=quiz_index==0?lesson_notes[snap.state.mode]:current_question()->explanation;
 text(c,note,22,112,708);button(root,"Hazırım, soruyu göster",180,426,440,44,35,0x247C62);
}
static void quiz_screen(void){
 const sim_question *q=current_question();char b[60];snprintf(b,sizeof(b),"Keşif sorusu %u / 3",quiz_index+1);text(root,b,24,66,752);text(root,q->title,24,110,752);
 if(!answered){for(unsigned i=0;i<3;i++)button(root,q->options[i],24,176+i*70,752,56,100+i,0x285477);}
 else{lv_obj_t *c=box(root,24,174,752,202,0x17364F);text(c,chosen==q->correct?"Doğru!":"Birlikte öğrenelim",20,20,710);text(c,q->explanation,20,65,710);button(root,quiz_index<2?"Sonraki soru":"Keşfi tamamla",200,426,400,44,32,0x247C62);}
}
static void draw(void){
 dirty=false;preview_pending=false;lv_obj_clean(root);slider=NULL;memset(rgb_sliders,0,sizeof(rgb_sliders));memset(room_info,0,sizeof(room_info));
 if(page==INTRO){intro();return;}
 text(root,"HYBRIDLAB / Ay Serası",20,17,550);button(root,"Merkeze dön",636,4,148,44,99,0x29465B);
 if(!snap.ready){text(root,"Kayıt yükleniyor...",24,110,752);return;}
 if(snap.result<0){text(root,"Kayıt tamamlanamadı. Deney verileri bellekte korunuyor.",24,130,752);button(root,"Kaydı yeniden dene",200,310,400,48,90,0x247C62);return;}
 if(page==SETUP)setup();else if(page==LIVE)live_screen();else if(page==RESULT)result_screen();else if(page==NASA_INFO)nasa_screen();else if(page==NASA_DATA)nasa_data_screen();else if(page==QUIZ_SUMMARY)quiz_summary();else if(page==QUIZ)quiz_screen();else{
  picture(root,&hidrolab_img_memory_marul,368,100);char b[120];snprintf(b,sizeof(b),"Keşif tamamlandı! 3 sorudan %u doğru.",score);text(root,b,160,220,600);text(root,"Başka bir koşul seçip yeni bir deney yapabilirsin.",160,266,600);button(root,"Yeni deney",200,410,400,48,1,0x247C62);
 }

}
static void action(lv_event_t *e){
 unsigned id=(unsigned)(uintptr_t)lv_event_get_user_data(e);if(closing)return;
 if(id==99){closing=true;lv_obj_delete_async(root);return;}
 if(id==1){page=SETUP;starting=false;finished_seen=false;room=1;crop=0;mode=SIM_PH;field=0;sim_defaults(crop,&draft[0]);draft[1]=draft[0];draft[1].ph=8;}
 else if(id==10){crop=(crop+1)%3;sim_defaults(crop,&draft[0]);draft[1]=draft[0];}
 else if(id==11){mode=(mode+1)%7;equalize();}
 else if(id==12)field=(field+1)%6;
 else if(id==20||id==21)room=id-20;
 else if(id==15){if(starting)return;sim_change c={.kind=1,.crop=crop,.mode=mode,.new_run=1,.conditions={draft[0],draft[1]}};starting=sim_runtime_send((sim_command){.kind=SC_START_EXPERIMENT,.change=c});}
 else if(id==90){sim_runtime_send((sim_command){.kind=SC_SAVE});}
 else if(id==30)page=NASA_INFO;
 else if(id==33)page=NASA_DATA;
 else if(id==35)page=QUIZ;
 else if(id==31){page=QUIZ_SUMMARY;quiz_index=score=0;answered=false;}
 else if(id>=100&&id<103){if(answered)return;chosen=id-100;answered=true;if(chosen==current_question()->correct)score++;}
 else if(id==32){if(quiz_index<2){quiz_index++;answered=false;page=QUIZ_SUMMARY;}else page=QUIZ_END;}
 dirty=true;
}
static void tick(lv_timer_t *t){
 (void)t;if(closing)return;bool ready=snap.ready;if(!sim_runtime_snapshot(&snap))return;
 if(!ready&&snap.ready)dirty=true;
 if(notice!=snap.notice){notice=snap.notice;dirty=true;if(snap.result<0){starting=false;dirty=true;}}
 if(starting&&snap.session&&snap.running){starting=false;page=LIVE;dirty=true;}
 if(snap.completed&&!finished_seen&&page==LIVE){finished_seen=true;page=RESULT;dirty=true;}
 if(dirty)draw();else if(page==LIVE)update_live();
 else if(page==SETUP&&preview_pending){preview_pending=false;paint_room(preview_room,draft[preview_room],NULL);}
}
static void destroyed(lv_event_t *e){(void)e;if(timer){lv_timer_delete(timer);timer=NULL;}root=NULL;sim_runtime_suspend();}
void hidrolab_simulation_ui_open(lv_obj_t *parent){
 if(root||!sim_runtime_init()){return;}
 memset(&snap,0,sizeof(snap));sim_runtime_snapshot(&snap);page=INTRO;closing=starting=finished_seen=answered=false;dirty=false;notice=snap.notice;
 root=box(parent,0,0,800,480,0x0C1C2B);lv_obj_add_event_cb(root,destroyed,LV_EVENT_DELETE,NULL);draw();timer=lv_timer_create(tick,100,NULL);
}

bool hidrolab_simulation_ui_is_open(void){return root!=NULL;}
