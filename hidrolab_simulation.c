#include "hidrolab_simulation.h"
#include <math.h>
#include <string.h>
/* All species response curves, harvest times, mass limits and equipment values
 * below are teaching assumptions, NOT measured NASA crop/yield coefficients. */
const sim_profile sim_profiles[3]={
 {"Uzay Marulu",5.5f,6.5f,1,2,18,24,14,28,200},
 {"Cüce Domates",5.8f,6.5f,1.5f,2.5f,20,26,19,45,400},
 {"Çilek",5.5f,6.2f,1,1.8f,18,24,16,40,180}
};
const char *const sim_upgrade_names[SIM_UPGRADES]={"Verimli LED","Büyük batarya","Güneş paneli","Radyasyon kalkanı","Sera kontrolü"};
const uint32_t sim_upgrade_prices[SIM_UPGRADES]={2000,3000,4000,2500,3500};
static float clamp(float x,float lo,float hi){return fminf(hi,fmaxf(lo,x));}
float sim_dli(float p,float h){return p*h*0.0036f;}
float sim_ec_us(float ms){return ms*1000.f;}
float sim_factor(float v,float lo,float hi,float m){return clamp(v<lo?1-(lo-v)/m:v>hi?1-(v-hi)/m:1,0,1);}
bool sim_spectrum_valid(uint32_t s){return s<3||((s&0xff000000u)==0x01000000u&&((s>>16)&255)<=100&&((s>>8)&255)<=100&&(s&255)<=100);}
uint32_t sim_spectrum_pack(unsigned r,unsigned g,unsigned b){return 0x01000000u|((r>100?100:r)<<16)|((g>100?100:g)<<8)|(b>100?100:b);}
void sim_spectrum_rgb(uint32_t s,unsigned *r,unsigned *g,unsigned *b){
 if(s==0){*r=*g=*b=100;}else if(s==1){*r=90;*g=10;*b=10;}else if(s==2){*r=50;*g=10;*b=50;}
 else{*r=(s>>16)&255;*g=(s>>8)&255;*b=s&255;}
}
float sim_effective_ppfd(sim_condition c){unsigned r,g,b;sim_spectrum_rgb(c.spectrum,&r,&g,&b);return r+g+b?c.ppfd:0;}
/* ILLUSTRATIVE TRAINING MODEL -- not a NASA crop-yield response curve.
 * Previous implementation used clamp(DLI / target, 0, 1), which made all
 * above-target light treatments indistinguishable. This simple peaked
 * response teaches that both insufficient and excessive light can matter.
 * The target DLI is a teaching parameter, not a crop-specific prescription. */
float sim_light_response(sim_condition c,const sim_profile *p){
 float photon_dose=sim_dli(sim_effective_ppfd(c),c.hours);
 if(!p||p->dli<=0||photon_dose<=0)return 0.f;
 float ratio=photon_dose/p->dli;
 float q=ratio<=1.f?powf(ratio,0.8f):fmaxf(.35f,1.f-.12f*(ratio-1.f));
 /* An illustrative photoperiod/rest penalty above 20h; not a species trial. */
 if(c.hours>20.f)q*=clamp(1.f-.05f*(c.hours-20.f),.80f,1.f);
 return clamp(q,0.f,1.f);
}
/* A deliberately disclosed game coefficient for the red/blue mix.  NASA
 * VEG-04 measured treatments, but does not supply calibrated growth factors
 * for our lettuce/tomato/strawberry hydroponic scenarios. White is neutral.
 * Keep values positive and bounded; no false NASA-quantified yield claims. */
float sim_spectrum_response(sim_condition c,unsigned crop){
 unsigned r,g,b;sim_spectrum_rgb(c.spectrum,&r,&g,&b);
 if(!r&&!g&&!b)return 0.f;
 if(c.spectrum==0||(r==g&&g==b)||(!r&&!b))return 1.f;
 float blue_ratio=(float)b/(float)(r+b);
 float illustrative_slope=crop==0?.70f:crop==1?-.60f:.45f;
 return clamp(1.f+illustrative_slope*(blue_ratio-.30f),.65f,1.25f);
}
bool sim_condition_valid(sim_condition c){return isfinite(c.ph)&&c.ph>=0&&c.ph<=14&&isfinite(c.ec)&&c.ec>=0&&c.ec<=6&&isfinite(c.ppfd)&&c.ppfd>=0&&c.ppfd<=1000&&isfinite(c.hours)&&c.hours>=0&&c.hours<=24&&isfinite(c.temp)&&c.temp>=0&&c.temp<=45&&sim_spectrum_valid(c.spectrum);}
static float field(sim_condition c,unsigned k){return k==0?c.ph:k==1?c.ec:k==2?c.ppfd:k==3?c.hours:k==4?(float)c.spectrum:c.temp;}
unsigned sim_differences(sim_condition a,sim_condition b){unsigned n=0;for(unsigned k=0;k<6;k++)if(k==SIM_SPECTRUM?a.spectrum!=b.spectrum:fabsf(field(a,k)-field(b,k))>0.0001f)n++;return n;}
void sim_defaults(unsigned crop,sim_condition *c){if(crop>2)crop=0;const sim_profile *p=&sim_profiles[crop];*c=(sim_condition){(p->ph_lo+p->ph_hi)/2,(p->ec_lo+p->ec_hi)/2,330,16,23,0};}
void sim_nasa_template(sim_condition c[2]){c[0].ppfd=c[1].ppfd=330;c[0].hours=c[1].hours=16;c[1]=c[0];c[0].spectrum=1;c[1].spectrum=2;}
float sim_capacity(const sim_state *s){return (s->upgrades&2)?18000:12000;}
static void sample(sim_state *s){
 unsigned n=s->hour/24;if(n>SIM_DAYS)return;sim_sample *h=&s->history[n];
 for(unsigned i=0;i<2;i++){h->progress[i]=s->plant[i].progress*100;h->mass[i]=s->plant[i].mass_g;h->ph[i]=s->conditions[i].ph;h->ec[i]=s->conditions[i].ec;h->ppfd[i]=s->conditions[i].ppfd;h->dli[i]=s->plant[i].dli_today;h->led_wh[i]=s->plant[i].led_wh;}
 h->battery=s->battery_wh;s->history_count=n+1;
}
void sim_init(sim_state *s){memset(s,0,sizeof(*s));s->version=SIM_VERSION;sim_defaults(0,&s->conditions[0]);s->conditions[1]=s->conditions[0];s->conditions[1].ph=8;s->initial[0]=s->conditions[0];s->initial[1]=s->conditions[1];s->battery_wh=sim_capacity(s);sample(s);}
bool sim_change_valid(const sim_state *s,const sim_change *c){
 if(c->kind==2)return c->upgrade<SIM_UPGRADES&&!(s->upgrades&(1u<<c->upgrade))&&s->credits_cents>=sim_upgrade_prices[c->upgrade];
 if(c->kind!=1||c->crop>2||c->mode>SIM_FREE||c->new_run>1)return false;
 if(!c->new_run&&c->crop!=s->crop)return false;
 if(!sim_condition_valid(c->conditions[0])||!sim_condition_valid(c->conditions[1]))return false;
 if(c->mode!=SIM_FREE)for(unsigned i=0;i<6;i++)if(i!=c->mode&&(i==SIM_SPECTRUM?c->conditions[0].spectrum!=c->conditions[1].spectrum:fabsf(field(c->conditions[0],i)-field(c->conditions[1],i))>0.0001f))return false;
 return true;
}
bool sim_apply(sim_state *s,const sim_change *c){
 if(!sim_change_valid(s,c))return false;
 if(c->kind==2){s->credits_cents-=sim_upgrade_prices[c->upgrade];s->upgrades|=1u<<c->upgrade;s->revision++;return true;}
 if(c->new_run){s->hour=0;s->history_count=0;s->interventions=0;memset(s->plant,0,sizeof(s->plant));memset(s->history,0,sizeof(s->history));s->battery_wh=sim_capacity(s);s->generated_wh=s->consumed_wh=s->curtailed_wh=s->unserved_wh=s->risk_index=0;s->initial[0]=c->conditions[0];s->initial[1]=c->conditions[1];}
 else if(s->hour) s->interventions++;
 if(!s->hour){s->initial[0]=c->conditions[0];s->initial[1]=c->conditions[1];}
 s->conditions[0]=c->conditions[0];s->conditions[1]=c->conditions[1];s->crop=c->crop;s->mode=c->mode;s->revision++;if(!s->hour)sample(s);return true;
}
void sim_step_hour(sim_state *s){
 if(s->hour>=24*SIM_DAYS)return;
 unsigned hour=s->hour%24;if(hour==0)for(unsigned i=0;i<2;i++)s->plant[i].dli_today=0;
 /* Simplified 29.53-day sine, not Moon Trek or measured site irradiance.
  * Initial phase = sunrise. Panel power and battery are fictional equipment. */
 float solar=fmaxf(0,sinf((float)(s->hour+0.5)*6.2831853f/(29.53f*24)))*120*((s->upgrades&4)?1.5f:1);
 float led[2],lit[2],climate=0;
 for(unsigned i=0;i<2;i++){sim_condition c=s->conditions[i];lit[i]=clamp(c.hours-(float)hour,0,1);led[i]=s->plant[i].harvested?0:sim_effective_ppfd(c)*0.1f/(2.5f*((s->upgrades&1)?1.25f:1)*0.8f)*lit[i];climate+=fabsf(c.temp-23)*0.5f;}
 if(s->upgrades&16)climate*=0.75f;
 float load=led[0]+led[1]+climate+2+(s->shield?1:0),available=s->battery_wh+solar;
 float supply=fminf(load,available),power=load>0?supply/load:1;
 s->battery_wh=clamp(available-supply,0,sim_capacity(s));s->generated_wh+=solar;s->consumed_wh+=supply;s->curtailed_wh+=fmaxf(0,available-supply-sim_capacity(s));s->unserved_wh+=load-supply;
 const sim_profile *p=&sim_profiles[s->crop];
 for(unsigned i=0;i<2;i++){
  sim_plant *v=&s->plant[i];sim_condition c=s->conditions[i];v->led_wh+=led[i]*power;
  if(v->harvested)continue;
  v->dli_today+=sim_effective_ppfd(c)*lit[i]*0.0036f*power;
  float quality=sim_factor(c.ph,p->ph_lo,p->ph_hi,2)*sim_factor(c.ec,p->ec_lo,p->ec_hi,2)*sim_factor(c.temp,p->temp_lo,p->temp_hi,12);
  /* Both lighting factors are educational assumptions, not NASA yields. */
  quality*=sim_light_response(c,p)*sim_spectrum_response(c,s->crop)*power;
  quality=clamp(quality,0.f,1.25f);v->last_quality=quality;
  float age=0.6f+0.4f*sinf(clamp(v->progress,0,1)*3.14159265f);
  v->progress=clamp(v->progress+quality/(p->days*24),0,1);
  /* Without a quality-related yield ceiling, every adequately lit plant
   * would hit max_g by day 60, hiding meaningful differences in results.
   * The ceiling is part of this toy model; it is NOT a measured yield. */
  float teaching_ceiling=p->max_g*clamp(.35f+.65f*quality,0.f,1.f);
  v->mass_g=fminf(v->mass_g+p->max_g/(p->days*24)*quality*age,
                  fmaxf(v->mass_g,teaching_ceiling));
 }
 /* Optional training risk scenario only, never a DONKI dose or growth penalty. */
 if(s->hour/24==10)s->risk_index+=s->shield?0.2f:1.f;
 s->hour++;if(s->hour%24==0)sample(s);
}
bool sim_harvest(sim_state *s,unsigned r){
 if(r>1||s->plant[r].harvested||s->plant[r].progress<0.9999f||s->hour/24<sim_profiles[s->crop].days)return false;
 unsigned grams=(unsigned)floorf(s->plant[r].mass_g);if(!grams||s->harvested_total>1000000-grams)return false;
 s->stock_g+=grams;s->harvested_total+=grams;s->plant[r].harvested=1;s->revision++;return true;
}
bool sim_ship(sim_state *s){
 if(!s->stock_g)return false;
 /* Cumulative rounding keeps exactly floor(total*40/100) crew grams. */
 uint32_t total=s->crew_g+s->shipped_g+s->stock_g,crew=total*40/100;
 uint32_t shipped=total-crew,delta=shipped-s->shipped_g;
 if(s->credits_cents>100000000-delta*12)return false;
 s->credits_cents+=delta*12;s->crew_g=crew;s->shipped_g=shipped;s->stock_g=0;s->revision++;return true;
}
const char *sim_stage(const sim_plant *p){return p->harvested?"Hasat edildi":p->progress>=.9999f?"Hasada hazır":p->progress<.04f?"Tohum":p->progress<.20f?"Filiz":p->progress<.50f?"Yapraklanma":"Büyüme";}
bool sim_valid(const sim_state *s){
 if(s->version!=SIM_VERSION||s->crop>2||s->mode>SIM_FREE||s->hour>24*SIM_DAYS||s->history_count!=s->hour/24+1||s->upgrades>=32||s->shield>1||s->credits_cents>100000000||s->harvested_total>1000000)return false;
 if((uint64_t)s->stock_g+s->crew_g+s->shipped_g!=s->harvested_total)return false;
 if(!isfinite(s->battery_wh)||s->battery_wh<0||s->battery_wh>sim_capacity(s)+.1f)return false;
 const float totals[]={s->generated_wh,s->consumed_wh,s->curtailed_wh,s->unserved_wh,s->risk_index};for(unsigned k=0;k<5;k++)if(!isfinite(totals[k])||totals[k]<0)return false;
 for(unsigned i=0;i<2;i++){const sim_plant *p=&s->plant[i];if(!sim_condition_valid(s->conditions[i])||!sim_condition_valid(s->initial[i])||!isfinite(p->progress)||p->progress<0||p->progress>1||!isfinite(p->mass_g)||p->mass_g<0||p->mass_g>sim_profiles[s->crop].max_g+.1f||p->harvested>1)return false;}
 for(unsigned n=0;n<s->history_count;n++){const sim_sample *h=&s->history[n];const float *v=(const float *)h;for(unsigned k=0;k<sizeof(*h)/sizeof(float);k++)if(!isfinite(v[k])||v[k]<0)return false;}
 return true;
}
