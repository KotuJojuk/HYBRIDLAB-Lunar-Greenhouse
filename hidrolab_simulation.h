#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SIM_VERSION 1u
#define SIM_DAYS 90u
#define SIM_UPGRADES 5u
enum { SIM_PH, SIM_EC, SIM_PPFD, SIM_PERIOD, SIM_SPECTRUM, SIM_TEMP, SIM_FREE };
typedef struct { float ph,ec,ppfd,hours,temp; uint32_t spectrum; } sim_condition;
typedef struct { const char *name; float ph_lo,ph_hi,ec_lo,ec_hi,temp_lo,temp_hi,dli,days,max_g; } sim_profile;
typedef struct { float progress,mass_g,dli_today,led_wh,last_quality; uint32_t harvested; } sim_plant;
typedef struct { float progress[2],mass[2],ph[2],ec[2],ppfd[2],dli[2],led_wh[2],battery; } sim_sample;
typedef struct {
    uint32_t version,hour,crop,mode,revision,upgrades,history_count;
    uint32_t stock_g,crew_g,shipped_g,credits_cents,harvested_total,question_cursor,interventions;
    sim_condition conditions[2],initial[2]; sim_plant plant[2];
    float battery_wh,generated_wh,consumed_wh,curtailed_wh,unserved_wh,risk_index;
    uint32_t shield; sim_sample history[SIM_DAYS+1];
} sim_state;
typedef struct { uint32_t kind,crop,mode,new_run,upgrade; sim_condition conditions[2]; } sim_change;
extern const sim_profile sim_profiles[3];
extern const char *const sim_upgrade_names[SIM_UPGRADES];
extern const uint32_t sim_upgrade_prices[SIM_UPGRADES];
float sim_dli(float ppfd,float hours);
float sim_ec_us(float ms);
float sim_factor(float value,float lo,float hi,float margin);
unsigned sim_differences(sim_condition a,sim_condition b);
bool sim_condition_valid(sim_condition c);
bool sim_valid(const sim_state *s);
void sim_init(sim_state *s);
void sim_defaults(unsigned crop,sim_condition *c);
void sim_nasa_template(sim_condition c[2]);
bool sim_change_valid(const sim_state *s,const sim_change *c);
bool sim_apply(sim_state *s,const sim_change *c);
void sim_step_hour(sim_state *s);
bool sim_harvest(sim_state *s,unsigned room);
bool sim_ship(sim_state *s);
float sim_capacity(const sim_state *s);
const char *sim_stage(const sim_plant *p);

/* Custom RGB weights use the existing spectrum word; legacy presets 0..2 remain readable. */
bool sim_spectrum_valid(uint32_t spectrum);
uint32_t sim_spectrum_pack(unsigned red,unsigned green,unsigned blue);
void sim_spectrum_rgb(uint32_t spectrum,unsigned *red,unsigned *green,unsigned *blue);
float sim_effective_ppfd(sim_condition c);
/* Educational light-response coefficients (not measured NASA crop yields). */
float sim_light_response(sim_condition c,const sim_profile *p);
float sim_spectrum_response(sim_condition c,unsigned crop);
