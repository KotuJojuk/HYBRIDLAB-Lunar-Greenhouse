#pragma once
/* Verified 2026-11-15. Related studies are not model calibration curves. */
typedef struct {
 const char *title,*match,*protocol,*finding,*limit,*url;
 sim_question question[2];
} sim_reference;
#define RQ(q,a,b,c,correct,explain) {q,{a,b,c},explain,correct,0,0}
static const sim_reference experiment_references[]={
 {"pH kontrolü / NASA arşivi, 1985",
  "Konu eşleşmesi: pH. Çalışma pH tamponunu inceliyor; farklı pH seviyelerini değil.",
  "Marul ve domates dahil çeşitli bitkilerde, çözeltinin pH'ını\nsabit tutmaya yardımcı olan MES tamponu denendi.",
  "1 ve 10 mM MES incelendi. 1 mM düzeyinde pH dengelendi;\nkontrole göre büyüme hızları deneme boyunca birbirine yakındı.",
  "Çilek bu çalışmada yok. Bu veri, her pH değerine verim yüzdesi vermez.",
  "https://ntrs.nasa.gov/citations/20040121540",
  {RQ("Çalışmada ne sabit tutulmaya çalışıldı?","Saksının rengi","Çözeltinin pH'ı","Bitki sayfası",1,"MES tamponu çözeltinin pH'ını sabit tutmak için incelendi."),RQ("Bu çalışma hangi veriyi vermiyor?","Her pH için kesin ürün miktarı","Tamponun ne işe yaradığı","Denemede kullanılan madde",0,"Bu bir tampon çalışmasıdır. Her pH düzeyindeki ürün miktarını vermez.")}},
 {"Besin düzeyi / NASA arşivi, 1983",
  "Dolaylı eşleşme: besin düzeyi. Bu, EC değerlerini tek başına karşılaştıran bir deney değil.",
  "Dört marul çeşidinde ışık ve azotla besleme incelendi.\n15 ve 30 mM nitrat gibi farklı besin uygulamaları kullanıldı.",
  "Besin uygulamalarına yanıt marul çeşidine göre değişti.\nBir çözeltinin EC'si, içindeki azot miktarını tek başına açıklamaz.",
  "Domates/çilek ve senin EC aralığın için birebir NASA verisi burada yok.",
  "https://ntrs.nasa.gov/citations/20040120316",
  {RQ("Bu çalışmada hangi bitki incelendi?","Çilek","Domates","Marul",2,"Araştırmada dört marul çeşidiyle ışık ve azot düzeyleri incelendi."),RQ("EC tek başına neyi söylemez?","Elektrik iletkenliğini","Çözeltinin tam besin bileşimini","Bir ölçüm değeri olduğunu",1,"EC tek başına hangi besinin ne kadar bulunduğunu söylemez.")}},
 {"Işık ve süre / NASA arşivi, 1983",
  "Koşul eşleşmesi: ışık şiddeti ve süre. Araştırma bitkisi marul.",
  "Farklı PPFD değerleri 16 saatlik aydınlatmada denendi.\nAyrıca sürekli ışık ve farklı azot uygulamaları incelendi.",
  "Bazı marul çeşitlerinde 932 PPFD'deki büyüme, 644 ve altından\ndaha yüksekti. Etki çeşide ve beslenme koşullarına bağlıydı.",
  "Bu, sınırsız ışığın daha iyi olduğu veya domates/çileğe aynı etkiyi verdiği anlamına gelmez.",
  "https://ntrs.nasa.gov/citations/20040120316",
  {RQ("Işığı karşılaştırırken neyi de bilmeliyiz?","Sadece saksının adını","Aydınlatma süresini","Ekranın rengini",1,"Aynı ışık şiddeti farklı sürelerde farklı günlük ışık toplamı verir."),RQ("Bu araştırma hangi sonucu desteklemez?","Koşulların önemli olduğunu","Çeşitlerin farklı yanıt verebildiğini","Daha çok ışık her zaman daha iyidir",2,"Sonuçlar belirli çeşit ve koşullara aittir; sınırsız ışık önerisi değildir.")}},
 {"Beyaz ve kırmızı-mavi / NASA KSC, 2012",
  "Işık türü eşleşmesi: beyaz floresan ve kırmızı-mavi LED; marul ve turp.",
  "Dünya laboratuvarında 200 PPFD, 18 saat ışık ve 23 °C kullanıldı.\nKırmızı:mavi oranı 8:1 idi. Ölçümler 25. günde karşılaştırıldı.",
  "Marul taze kütlesi: kırmızı-mavi 15,2 g / beyaz 14,4 g.\nBeyaz altında yaprak alanı daha büyüktü: 415,5 / 330,9 cm2.",
  "Bunlar belirli lambaların sonuçları. Ekrandaki RGB beyazı floresan spektrumuyla aynı değil.",
  "https://ntrs.nasa.gov/citations/20120015737",
  {RQ("Bu deneyde hangi ışıklar karşılaştırıldı?","Beyaz ve kırmızı-mavi","Yalnız yeşil","Yalnız güneş",0,"Kırmızı-mavi LED ile beyaz floresan, aynı 200 PPFD'de karşılaştırıldı."),RQ("Daha geniş yaprak mutlaka daha ağır mıdır?","Her zaman","Hayır, ölçmek gerekir","Sadece mor ışıkta",1,"Beyazda yaprak alanı daha genişti; marul taze kütlesi ise kırmızı-mavide biraz daha yüksekti.")}},
 {"Sıcaklık ve ışık / NASA arşivi, 1983",
  "Koşul eşleşmesi: sıcaklık. Marulda gündüz/gece sıcaklıkları değiştirildi.",
  "20 veya 25 °C gündüz, 15 °C gece koşulları;\n444 veya 889 PPFD ve günde 20 saat ışık karşılaştırıldı.",
  "Bu çalışmada yüksek ışık ve sıcak rejimde yaprak kuru kütlesi\ndaha yüksekti. Işık ve sıcaklık birlikte değerlendirilmelidir.",
  "Oyunda sıcaklık sabit. Bu çalışmanın gündüz/gece düzeniyle birebir aynı değil.",
  "https://ntrs.nasa.gov/citations/19930060882",
  {RQ("Bu çalışmada sıcaklık neyle birlikte incelendi?","Saksı rengiyle","Işık şiddetiyle","Ekran boyuyla",1,"Gündüz/gece sıcaklıkları ve farklı ışık şiddetleri birlikte incelendi."),RQ("Oyundaki sabit sıcaklık bu çalışmayla aynı mı?","Her yönüyle aynı","Yalnız bitki büyürse aynı","Hayır, çalışmada gündüz/gece farklı",2,"Çalışmada gündüz ve gece sıcaklıkları ayrıydı; oyundaki ayar sabit sıcaklıktır.")}},
 {"Birlikte değişen koşullar / NASA arşivi",
  "Serbest deney: birden fazla koşul değişebilir. Işık ve besin çalışmasıyla ilişkilendirildi.",
  "Marul araştırmasında ışık düzeyleri ve azot uygulamaları birlikte\nincelendi. Sonuçlar bitki çeşidine göre de değişiyordu.",
  "Tek bir neden aramak için diğer koşulları sabit tutmak gerekir.\nBirçok ayarı birlikte değiştirince tek neden belirleyemeyiz.",
  "Bu bir yöntem benzerliği; senin bütün koşullarının NASA'da tekrarı değil.",
  "https://ntrs.nasa.gov/citations/20040120316",
  {RQ("Birçok ayar değiştiyse tek nedeni bulabilir miyiz?","Kesinlikle evet","Hayır","Yalnız sonuç iyi olursa",1,"Birçok koşul birlikte değiştiğinde farkı tek bir koşula bağlayamayız."),RQ("Tek bir koşulu incelemek için ne yaparız?","Diğer koşulları aynı tutarız","Her ayarı değiştiririz","Sonucu tahmin ederiz",0,"Diğer koşulları aynı tutmak karşılaştırmayı daha anlamlı yapar.")}},
 {"Domates ışıkları / NASA VEG-05 hazırlığı",
  "Bitki ve konu eşleşmesi: cüce domates, ışık renkleri. Yer doğrulama deneyi.",
  "Red Robin domateslerinde 270:30:30 ve 150:150:30\nkırmızı:mavi:yeşil PPFD reçeteleri, toplam 330 PPFD ile kullanıldı.",
  "SVT hasatları 90, 97, 104; EVT hasatları 83, 90, 99. günlerdeydi.\nSulama da değişti; olgunlaşma farkı yalnız ışığa bağlanamaz.",
  "Bu kayıt yer testlerini anlatır. Oyundaki 60 günlük domates hasadı ölçüm değil modeldir.",
  "https://ntrs.nasa.gov/citations/20230001374",
  {RQ("Bu araştırmada hangi domates vardı?","Her domates çeşidi","Red Robin cüce domates","Mizuna",1,"VEG-05 hazırlığında Red Robin cüce domatesleri incelendi."),RQ("Testlerdeki olgunlaşma farkı yalnız ışığa bağlanır mı?","Evet","Kesinlikle mor kazandı","Hayır, sulama da değişti",2,"Yer testleri arasında sulama da değişti. Tek bir neden çıkarılamaz.")}},
 {"Işık şiddeti ve süre / NASA ortaklı, 2025",
  "Koşul eşleşmesi: PPFD ve aydınlatma süresi. Bitki mizuna; senin türün farklı.",
  "ISS benzeri Dünya ortamında mizuna, 200 / 400 / 600 / 800 PPFD\nve 16 veya 24 saat ışık altında yetiştirildi.",
  "Işık şiddetiyle sürenin etkileri birlikte incelendi.\nDaha çok ışık her durumda daha yüksek verim anlamına gelmedi.",
  "Bu bir NASA ortaklı yer çalışmasıdır; ISS uçuş deneyi veya senin türüne ait ölçüm değil.",
  "https://doi.org/10.1038/s41598-025-11662-y",
  {RQ("Bu çalışmada hangi süreler karşılaştırıldı?","2 ve 4 saat","16 ve 24 saat","60 ve 90 gün",1,"Mizuna çalışmasında 16 ve 24 saatlik aydınlatmalar kullanıldı."),RQ("Bitki türü aynı mı?","Evet, tüm bitkiler aynı","Rengi aynıysa evet","Hayır, araştırmada mizuna var",2,"Araştırma mizunaya aittir; oyunun marul, domates veya çileğiyle birebir aynı değildir.")}},
 {"RGB karışımları / NASA KSC, 2004",
  "Koşul eşleşmesi: ışık karışımı. Araştırma marulla yapıldı.",
  "Kırmızı-mavi, yeşil eklenmiş kırmızı-mavi, yeşil ağırlıklı\nve soğuk beyaz ışık kaynakları karşılaştırıldı.",
  "Bu çalışmada %24 yeşil içeren RGB uygulaması büyümeyi artırdı;\nRGB altında biyokütle soğuk beyaz floresandan daha yüksekti.",
  "Sonuç belirli lambalara aittir. Her RGB oranı ve her bitki için aynı katsayı çıkarılamaz.",
  "https://ntrs.nasa.gov/citations/20050164131",
  {RQ("Bu araştırmada hangi ek ışık incelendi?","Yeşil","Sadece kırmızı","Sadece mavi",0,"Kırmızı-mavi ışığa yeşil eklenmesinin marul büyümesine etkisi incelendi."),RQ("Sonuç her karışıma ve bitkiye aynen uygulanır mı?","Evet","Hayır, tür ve koşullar önemli","Yalnız ekran parlaksa",1,"Çalışma belirli marul ve ışık kaynaklarına aittir; her karışımın etkisini vermez.")}}
};
#undef RQ
static unsigned experiment_reference_id(const sim_state *s){
 switch(s->mode){
 case SIM_PH:return 0;
 case SIM_EC:return 1;
 case SIM_PPFD:case SIM_PERIOD:return s->crop==0?2:7;
 case SIM_TEMP:return 4;
 case SIM_FREE:return 5;
 default:{
  if(s->crop==1)return 6;
  unsigned r0,g0,b0,r1,g1,b1;sim_spectrum_rgb(s->initial[0].spectrum,&r0,&g0,&b0);sim_spectrum_rgb(s->initial[1].spectrum,&r1,&g1,&b1);
  bool rb0=r0&&b0&&!g0,rb1=r1&&b1&&!g1;
  bool white0=r0&&g0&&b0&&r0==g0&&g0==b0,white1=r1&&g1&&b1&&r1==g1&&g1==b1;
  return (rb0&&white1)||(rb1&&white0)?3:8;
 }}
}
