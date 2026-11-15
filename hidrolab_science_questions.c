#include "hidrolab_science_questions.h"
/* Topics 0..5 conditions; 6 LED,7 battery,8 panel,9 shield,10 control.
 * Questions test sourced concepts, never invented species response curves. */
#define LIGHT ((1u<<2)|(1u<<3)|(1u<<4)|(1u<<6))
#define ENV ((1u<<0)|(1u<<1)|(1u<<5)|(1u<<10))
#define POWER ((1u<<7)|(1u<<8))
const sim_question sim_questions[SIM_QUESTION_COUNT]={
 {"VEG-04 deneyinde hangi bitki kullanıldı?",{"Mizuna","Çilek","Domates"},"VEG-04'ün bitkisi mizunaydı; bu oyunun türleri farklıdır.",0,0,LIGHT},
 {"VEG-04'te günlük aydınlık süresi neydi?",{"8 saat","16 saat","24 saat"},"Protokolde 16 saat ışık ve 8 saat karanlık vardı.",1,0,LIGHT},
 {"Kırmızı ağırlıklı reçetede R/B/G değerleri?",{"150/150/30","330/0/0","270/30/30"},"Kırmızı reçete 270 kırmızı,30 mavi,30 yeşil PPFD içerir.",2,0,LIGHT},
 {"Mavi ağırlıklı reçetede R/B/G değerleri?",{"150/150/30","30/270/30","270/30/30"},"Mavi ağırlıklı protokolde kırmızı ve mavi kanallar eşittir.",0,0,LIGHT},
 {"İki NASA reçetesinin toplam PPFD'si?",{"300","330","660"},"Her iki reçetede toplam 330 olur; yeşil kanal da toplama dahildir.",1,0,LIGHT},
 {"90/10 oranı hangi kanalları karşılaştırır?",{"Tüm renkleri","Yeşil ile maviyi","Yalnız kırmızı ve maviyi"},"Yeşil kanal,kırmızı/mavi arasındaki 90/10 oranına dahil değildir.",2,0,LIGHT},
 {"VEG-04A'nın tek hasadı hangi gündü?",{"35","29","58"},"VEG-04A'da bitkinin tamamı 35.günde hasat edildi.",0,0,ENV},
 {"VEG-04B'nin hasat günleri hangileriydi?",{"Sadece 35","29,43 ve 58","Her gün"},"VEG-04B'de iki yaprak hasadını 58.gün son hasadı izledi.",1,0,ENV},
 {"VEG-04 bitkileri nerede yetiştirildi?",{"Ay yüzeyinde","Mars'ta","ISS ve Dünya kontrol ortamında"},"ISS mikro yerçekimi sonuçları doğrudan Ay sonuçları değildir.",2,0,ENV},
 {"VEG-04 yetiştirme ortamı hangisiydi?",{"Seramik içeren bitki yastığı","Yalnız saf su","Ay toprağı"},"Veggie yastıkları seramik ortam ve kontrollü salınımlı gübre içerir.",0,0,ENV},
 {"VEG-04 kaydı doğrudan neyi sağlar?",{"Her tür için ideal pH","Mizunanın gerçek deney koşullarını","Ay'da kesin hasat miktarını"},"Bu protokol tüm bitkiler için hidroponik pH/EC verim eğrisi değildir.",1,0,ENV},
 {"İki NASA ışık reçetesinde ne aynıydı?",{"Kırmızı kanal","Mavi kanal","Toplam PPFD ve ışık süresi"},"Toplam PPFD ve süre eşitken ışığın renk dağılımı farklıydı.",2,0,LIGHT},
 {"CSA'ya göre uzay bitkisi seçerken neye bakılır?",{"Besin değeri ve yetişme gereksinimleri","Sadece rengine","Sadece adına"},"Besin değeri ve ihtiyaç duyulan kaynaklar birlikte değerlendirilir.",0,1,ENV},
 {"Kapalı serada hangi koşul kontrol edilir?",{"Gezegenin büyüklüğü","Sıcaklık","Ay'ın yörüngesi"},"CSA kontrollü yetiştirmede sıcaklık düzenlemesini anlatır.",1,1,ENV},
 {"Uzun karanlıkta bitkiye ışık nasıl sağlanabilir?",{"Bitkinin adını değiştirerek","Suyu renklendirerek","Yapay aydınlatmayla"},"CSA gerektiğinde yapay aydınlatma kullanılmasını önerir.",2,1,POWER|LIGHT},
 {"Ay'da açık alan yerine neden kapalı sera düşünülür?",{"Yetişme ortamını kontrol etmek için","Işığı tamamen yok etmek için","Su kullanmamak için"},"Kapalı sera,su ve iç ortam koşullarının yönetilmesine yardımcı olur.",0,1,ENV|POWER},
 {"Uzayda taze gıda üretimi hangi ihtiyacı destekler?",{"Daha ağır kargo","Mürettebatın beslenmesi","Daha az gözlem"},"Bitki seçimi mürettebatın dengeli beslenmesini desteklemelidir.",1,1,ENV|POWER},
 {"CSA'nın Ay serası için saydığı risklerden biri?",{"Deniz dalgaları","Yağmur","Uzay radyasyonu"},"Korunaklı yetiştirme alanı önerilir; belirli koruma yüzdesi verilmez.",2,1,(1u<<9)},
 {"Radyasyon için önerilen yaklaşım hangisi?",{"Korunaklı kapalı alan","Serayı tamamen açık bırakmak","Bitkinin adını değiştirmek"},"CSA radyasyona karşı korunaklı alanı ele alır.",0,1,(1u<<9)},
 {"Ay koşullarında enerji planlarken ne önemlidir?",{"Her yerde sürekli güneş var sanmak","Aydınlık ve karanlık sürelerini dikkate almak","Lambayı enerji hesabına katmamak"},"Doğal ışık koşulları yapay aydınlatma gereksinimini etkiler.",1,1,POWER},
 {"NASA'nın Apollo toprağı araştırması nerede yapıldı?",{"Ay yüzeyinde","ISS'de","Dünya laboratuvarında"},"Apollo'dan getirilen regolit Dünya'da Arabidopsis yetiştirmede kullanıldı.",2,2,ENV|(1u<<9)},
 {"Apollo regolit araştırmasının bitkisi hangisiydi?",{"Arabidopsis","Çilek","Domates"},"Bu araştırmanın türü Arabidopsis'ti; hidroponik domates değildi.",0,2,ENV},
 {"CSA'ya göre hasat süresi neden önemlidir?",{"Saksının rengini belirler","Gıda ve kaynak planını etkiler","Işık gereksinimini yok eder"},"Bitki seçerken hasada kadar geçen süre de değerlendirilir.",1,1,POWER|ENV},
 {"APH sisteminde hangi öğeler ayarlanabilir?",{"Ay'ın çekimi","Dünya'nın dönüşü","İç sıcaklık ve gaz koşulları"},"CSA,APH'nin sıcaklık ve gaz koşullarını düzenlediğini açıklar.",2,1,ENV|(1u<<10)}
};
unsigned sim_question_select(unsigned topic,unsigned cursor){if(topic>10)topic=0;for(unsigned i=0;i<SIM_QUESTION_COUNT;i++){unsigned q=(cursor+i)%SIM_QUESTION_COUNT;if(sim_questions[q].topics&(1u<<topic))return q;}return cursor%SIM_QUESTION_COUNT;}
bool sim_transaction_begin(sim_state *s,sim_transaction *t,const sim_change *c){
 if(t->active||!sim_change_valid(s,c))return false;
 unsigned topic=c->kind==2?6+c->upgrade:c->mode==SIM_FREE?0:c->mode;
 t->change=*c;t->question=sim_question_select(topic,s->question_cursor);t->active=1;t->token++;return true;
}
int sim_transaction_answer(sim_state *s,sim_transaction *t,unsigned token,unsigned option){
 if(!t->active||token!=t->token||option>2)return 0;
 t->active=0;s->question_cursor=(t->question+1)%SIM_QUESTION_COUNT;
 if(option!=sim_questions[t->question].correct)return 2;
 return sim_apply(s,&t->change)?1:0;
}
