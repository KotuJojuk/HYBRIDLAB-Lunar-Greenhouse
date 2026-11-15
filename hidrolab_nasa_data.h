#pragma once
#include <stdint.h>
typedef struct { const char *dataset_id,*mission,*organism,*source_url,*treatment_id; uint16_t study_year,red_ppfd,blue_ppfd,green_ppfd,photoperiod_hours; uint16_t harvest_days[3]; const char *gravity_context,*growth_medium,*measurement_kind,*retrieved_at,*license_note; } sim_nasa_protocol;
extern const sim_nasa_protocol sim_veg04[4];
const char *sim_source_url(unsigned source);
const char *sim_source_name(unsigned source);

/* Published total fresh edible biomass, VEG-04B flight, repeated harvests.
 * Group totals, not per-plant yields or calibration coefficients. */
extern const float sim_veg04b_flight_total_g[2];
extern const char *const sim_veg04b_sample_notes[2];
extern const char *const sim_veg04_publication_url;
