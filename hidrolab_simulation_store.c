#include "hidrolab_simulation_store.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#ifndef SIM_HOST_TEST
#include <unistd.h>
#endif
#ifndef SIM_STORE_DIR
#define SIM_STORE_DIR "/spiffs"
#endif
static uint32_t crc(const void *data,size_t n){uint32_t v=0xFFFFFFFFu;const uint8_t *p=data;while(n--){v^=*p++;for(unsigned k=0;k<8;k++)v=(v>>1)^((0u-(v&1u))&0xEDB88320u);}return ~v;}
void sim_record_encode(const sim_state *s,uint32_t seq,sim_record *r){memset(r,0,sizeof(*r));r->magic=0x48534D31;r->version=SIM_VERSION;r->size=sizeof(sim_state);r->sequence=seq;r->state=*s;r->crc=crc(&r->state,sizeof(r->state))^seq;}
bool sim_record_decode(const sim_record *r,size_t n,sim_state *s){if(n!=sizeof(*r)||r->magic!=0x48534D31||r->version!=SIM_VERSION||r->size!=sizeof(sim_state)||r->crc!=(crc(&r->state,sizeof(r->state))^r->sequence)||!sim_valid(&r->state))return false;*s=r->state;return true;}
static sim_record io;static sim_state candidate;
static bool read_record(unsigned slot){char path[100];snprintf(path,sizeof(path),SIM_STORE_DIR "/sim%u.dat",slot);FILE *f=fopen(path,"rb");if(!f)return false;size_t n=fread(&io,1,sizeof(io),f);int extra=fgetc(f);fclose(f);return extra==EOF&&sim_record_decode(&io,n,&candidate);}
int sim_store_load(sim_state *s,uint32_t *sequence){
 bool found=false;*sequence=0;int status=0;
 for(unsigned k=0;k<2;k++){if(read_record(k)){if(!found||io.sequence>*sequence){*s=candidate;*sequence=io.sequence;found=true;}}else{char path[100];snprintf(path,sizeof(path),SIM_STORE_DIR "/sim%u.dat",k);FILE *f=fopen(path,"rb");if(f){fclose(f);status=-1;}}}
 return found?1:status;
}
bool sim_store_save(const sim_state *s,uint32_t seq){
 if(!sim_valid(s)||seq==UINT32_MAX)return false;
 sim_record_encode(s,seq,&io);char path[100];snprintf(path,sizeof(path),SIM_STORE_DIR "/sim%u.dat",(unsigned)(seq&1u));
 FILE *f=fopen(path,"wb");if(!f)return false;bool ok=fwrite(&io,1,sizeof(io),f)==sizeof(io);if(fflush(f))ok=false;
#ifndef SIM_HOST_TEST
 if(fsync(fileno(f)))ok=false;
#endif
 if(fclose(f))ok=false;
 if(!ok)return false;
 return read_record(seq&1u)&&io.sequence==seq;
}
