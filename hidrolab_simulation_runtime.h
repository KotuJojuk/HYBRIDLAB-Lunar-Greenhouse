#pragma once
#include "hidrolab_simulation.h"
#define SIM_SESSION_DAYS 60u
#define SIM_SESSION_MS 30000u
enum { SC_PROPOSE,SC_ANSWER,SC_RUN,SC_PAUSE,SC_DAY,SC_HARVEST,SC_SHIP,SC_SHIELD,SC_SAVE,SC_START_EXPERIMENT };
typedef struct { uint32_t kind,value,token;sim_change change; } sim_command;
typedef struct { sim_state state;uint32_t ready,running,speed,pending,question,token,notice,session,completed,elapsed_ms;int result; } sim_snapshot;
bool sim_runtime_init(void);
bool sim_runtime_send(sim_command command);
bool sim_runtime_snapshot(sim_snapshot *out);
void sim_runtime_suspend(void);
