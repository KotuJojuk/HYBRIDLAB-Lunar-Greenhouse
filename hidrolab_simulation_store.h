#pragma once
#include "hidrolab_simulation.h"
#include <stddef.h>
typedef struct { uint32_t magic,version,size,sequence,crc; sim_state state; } sim_record;
void sim_record_encode(const sim_state *s,uint32_t sequence,sim_record *r);
bool sim_record_decode(const sim_record *r,size_t size,sim_state *out);
/* Independent SPIFFS files: never modifies physical garden NVS. */
int sim_store_load(sim_state *s,uint32_t *sequence);
bool sim_store_save(const sim_state *s,uint32_t sequence);
