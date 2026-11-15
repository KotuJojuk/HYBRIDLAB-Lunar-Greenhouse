#include "hidrolab_nasa_data.h"
#define URL "https://osdr.nasa.gov/bio/repo/data/studies/OSD-655"
#define ROW(m,t,r,b,h1,h2,h3) {"OSD-655",m,"Brassica rapa var. japonica (mizuna)",URL,t,2019,r,b,30,16,{h1,h2,h3},"ISS microgravity; not lunar surface","Veggie plant pillow; porous ceramic + controlled-release fertilizer","NASA experiment protocol","2026-11-14","Source attribution retained; factual protocol transcription; no NASA endorsement"}
const sim_nasa_protocol sim_veg04[4]={ROW("VEG-04A","red-rich",270,30,35,0,0),ROW("VEG-04A","blue-rich",150,150,35,0,0),ROW("VEG-04B","red-rich",270,30,29,43,58),ROW("VEG-04B","blue-rich",150,150,29,43,58)};
static const char *const urls[]={URL,"https://www.asc-csa.gc.ca/eng/youth-educators/activities/fitness-and-nutrition/interplanetary-farming.asp","https://www.nasa.gov/humans-in-space/scientists-grow-plants-in-lunar-soil/"};
static const char *const names[]={"NASA OSD-655 / 2019","CSA Interplanetary Farming / 2019","NASA Apollo regolit araştırması / 2022"};
const char *sim_source_url(unsigned n){return urls[n<3?n:0];}
const char *sim_source_name(unsigned n){return names[n<3?n:0];}

const float sim_veg04b_flight_total_g[2]={186.80f,222.85f};
const char *const sim_veg04b_sample_notes[2]={"6 bitki; ilk hasattan sonra 5", "5 bitki"};
const char *const sim_veg04_publication_url="https://doi.org/10.1080/17429145.2023.2292220";
