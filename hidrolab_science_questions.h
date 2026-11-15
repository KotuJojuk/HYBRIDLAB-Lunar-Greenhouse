#pragma once
#include <stdint.h>
#include "hidrolab_simulation.h"
#define SIM_QUESTION_COUNT 24u
typedef struct { const char *title,*options[3],*explanation; uint8_t correct,source; uint16_t topics; } sim_question;
extern const sim_question sim_questions[SIM_QUESTION_COUNT];
unsigned sim_question_select(unsigned topic,unsigned cursor);
typedef struct { sim_change change; unsigned active,question,token; } sim_transaction;
bool sim_transaction_begin(sim_state *s,sim_transaction *t,const sim_change *c);
/* 0 ignored/stale,1 correct,2 incorrect; wrong never applies or debits. */
int sim_transaction_answer(sim_state *s,sim_transaction *t,unsigned token,unsigned option);
