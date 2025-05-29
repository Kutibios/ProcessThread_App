#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <string.h>

// Sabit değerler
#define KAT_SAYISI 10           // Toplam kat sayısı (40x2)
#define DAIRE_SAYISI 4          // Her kattaki daire sayısını gösterir
#define MAX_BUFFER 512          // Buffer boyutunu gösterir
#define DAIRE_MALZEME 2         // Her daire için gereken malzeme miktarını belirleriz. 
#define GENISLIK 15

// Malzeme talebi için yapı
typedef struct {
    int daire_id;               // Daire kimliği oluşturuyoruz
    int talep_miktar;           // Talep edilen malzeme miktarı belirlenir
    int kat_no;                 // Kat numarası gösterilir
    int islem_turu;             // 0: başlangıçtır, 1: bitişi, 2: durum sorgulamı gösterir
} MalzemeTalebi;

// Malzeme cevabı için yapı oluşturulur
typedef struct {
    int basarili;               // 1: başarılı durumu, 0: başarısız durumu
    int kalan_malzeme;          // Kalan malzeme miktarı
} MalzemeCevabi;

// Thread parametreleri için yapı
typedef struct {
    int kat_no;                 // Hangi kattaki daire
    int daire_no;               // Daire numarası
    int global_daire_id;        // Genel daire ID'si
    int pipe_talep_fd;          // Malzeme talep pipe'ı (yazma)
    int pipe_cevap_fd;          // Malzeme cevap pipe'ı (okuma)
} DaireInfo;

// Process içinde kullanılan mutexler (her process kendi mutex'ini kullanır)
pthread_mutex_t vinc_mutex;         // Vinç kullanımı için mutex
pthread_mutex_t asansor_mutex;      // Asansör kullanımı için mutex
pthread_mutex_t konsol_mutex;       // Konsol çıktısı için mutex
pthread_mutex_t pipe_mutex;         // Pipe erişimi için mutex

// Kat bazında tesisatı mutexleri - aynı kattaki daireler sıralı çalışır
pthread_mutex_t kat_su_tesisati_mutex;    // Aynı kattaki daireler su tesisatını sırayla yapar
pthread_mutex_t kat_elektrik_mutex;       // Aynı kattaki daireler elektriği sırayla yapar

// Semafore'lar
sem_t elektrikci_sem;           // Elektrikçi sayısı sınırlaması (2 elektrikçi)
sem_t tesisatci_sem;            // Tesisatçı sayısı sınırlaması (2 tesisatçı)
sem_t yangin_alarm_sem;         // Yangın alarmı teknisyeni sınırlaması (3 teknisyen)

// Global değişkenler
int toplam_malzeme = 10;       // 80 daire x 2 birim = 80 birim (normal miktar)
int malzeme_tukendi = 0;       // Malzeme tükenme durumu flag'i (0: devam, 1: tükendi)

// Fonksiyon prototipleri (implicit declaration hatalarını önlemek için)
void guvenli_yazdir(const char* mesaj);
int malzeme_islem(int miktar, int daire_id, int kat_no, int islem_turu, int pipe_talep_fd, int pipe_cevap_fd);
void kaynak_kullan(int daire_id, const char* kaynak, const char* islem, int kat_no, pthread_mutex_t* kaynak_mutex);
void tesisati_kur(int daire_id, const char* tip, sem_t* isci_sem, pthread_mutex_t* kat_mutex);
void yangin_alarm_kur(int daire_id);
void* daire_insa_et(void* parametre);
void kat_insa_et(int kat_no, int pipe_talep_fd, int pipe_cevap_fd);
void process_senkronizasyon_baslat(void);
void process_senkronizasyon_temizle(void);
void malzeme_sunucu_calistir(int pipe_talep_fd, int pipe_cevap_fd);

/**
 * Güvenli konsol çıktısı için fonksiyon
 * Bu fonksiyon race condition'ı önlemek için mutex kullanır
 */
void guvenli_yazdir(const char* mesaj) {
    pthread_mutex_lock(&konsol_mutex);
    printf("%s", mesaj);
    fflush(stdout);             // Çıktıyı hemen göster
    pthread_mutex_unlock(&konsol_mutex);
}

/**
 * Malzeme işlemi - PIPE KULLANIR
 * Thread'den parent process'e malzeme talebi gönderir
 */
int malzeme_islem(int miktar, int daire_id, int kat_no, int islem_turu, int pipe_talep_fd, int pipe_cevap_fd) {
    char buffer[MAX_BUFFER];
    MalzemeTalebi talep;
    MalzemeCevabi cevap;
    
    // Pipe erişimini senkronize et (birden fazla thread aynı pipe'ı kullanacak)
    pthread_mutex_lock(&pipe_mutex);
    
    // Talep hazırla
    talep.daire_id = daire_id;
    talep.talep_miktar = miktar;
    talep.kat_no = kat_no;
    talep.islem_turu = islem_turu;
    
    // Parent process'e talep gönder
    write(pipe_talep_fd, &talep, sizeof(MalzemeTalebi));
    
    // Parent'tan cevap bekle
    read(pipe_cevap_fd, &cevap, sizeof(MalzemeCevabi));
    
    // Pipe mutex'ini serbest bırak
    pthread_mutex_unlock(&pipe_mutex);
    
    if (cevap.basarili) {
        if (islem_turu == 0) {
            snprintf(buffer, sizeof(buffer), 
                    "   📦 Daire %d: Malzeme kontrol başarılı (Mevcut: %d birim)\n", 
                    daire_id, cevap.kalan_malzeme);
        } else {
            snprintf(buffer, sizeof(buffer), 
                    "   ✅ Daire %d: Tamamlandı! Kullanılan: %d birim (Kalan: %d birim)\n", 
                    daire_id, miktar, cevap.kalan_malzeme);
        }
        guvenli_yazdir(buffer);  // Thread-safe çıktı
        return 1;   // Başarılı
    } else {
        // MALZEME YETERSİZ - KRİTİK DURUM!
        snprintf(buffer, sizeof(buffer), 
                "   ❌ Daire %d: MALZEME TÜKENDİ! İstenen: %d, Mevcut: %d\n", 
                daire_id, miktar, cevap.kalan_malzeme);
        guvenli_yazdir(buffer);
        
        snprintf(buffer, sizeof(buffer), 
                "   🚨 KRİTİK: Daire %d malzeme yetersizliği nedeniyle inşaat durduruluyor!\n", 
                daire_id);
        guvenli_yazdir(buffer);
        
        // Global malzeme tükenme flag'ini set et
        malzeme_tukendi = 1;
        
        return 0;   // Başarısız
    }
}

/**
 * Genel kaynak kullanım fonksiyonu (vinç, asansör vs.)
 * Ortak kaynaklar mutex ile korunur
 */
void kaynak_kullan(int daire_id, const char* kaynak, const char* islem, int kat_no, pthread_mutex_t* kaynak_mutex) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(kaynak_mutex);
    if (strcmp(kaynak, "asansör") == 0) {
        snprintf(buffer, sizeof(buffer), 
                "🛗 Daire %d: %s kullanılıyor (Kat %d'e çıkış)\n", 
                daire_id, kaynak, kat_no);
    } else {
        snprintf(buffer, sizeof(buffer), 
                "🏗️  Daire %d: %s kullanılıyor - %s\n", daire_id, kaynak, islem);
    }
    guvenli_yazdir(buffer);
    
    sleep(1);   // Kaynak kullanım süresi
    
    if (strcmp(kaynak, "asansör") == 0) {
        snprintf(buffer, sizeof(buffer), 
                "✅ Daire %d: %s Kat %d'e vardı\n", daire_id, kaynak, kat_no);
    } else {
        snprintf(buffer, sizeof(buffer), 
                "✅ Daire %d: %s işlemi tamamlandı - %s\n", daire_id, kaynak, islem);
    }
    guvenli_yazdir(buffer);
    pthread_mutex_unlock(kaynak_mutex);
}

/**
 * Genel tesisatı kurulum fonksiyonu
 * İki seviyeli senkronizasyon:
 * 1. Kat seviyesi: Aynı kattaki daireler sıralı çalışır (mutex)
 * 2. İşçi seviyesi: Sınırlı sayıda işçi (semafore)
 */
void tesisati_kur(int daire_id, const char* tip, sem_t* isci_sem, pthread_mutex_t* kat_mutex) {
    char buffer[MAX_BUFFER];
    
    // ÖNEMLİ: Önce kat mutex'ini al - aynı kattaki daireler sıralı çalışsın
    pthread_mutex_lock(kat_mutex);
    snprintf(buffer, sizeof(buffer), 
            "🔒 Daire %d: %s tesisatı için kat sırası alındı (aynı katta sıralı çalışma)\n", 
            daire_id, tip);
    guvenli_yazdir(buffer);
    
    // Sonra işçi bekle - sınırlı sayıda işçi var
    snprintf(buffer, sizeof(buffer), 
            "⏳ Daire %d: %s işçisi bekleniyor...\n", 
            daire_id, tip);
    guvenli_yazdir(buffer);
    sem_wait(isci_sem);
    
    snprintf(buffer, sizeof(buffer), 
            "%s Daire %d: %s tesisatı kurulumu başladı (kat mutex + işçi semaforu aktif)\n", 
            (strcmp(tip, "su") == 0) ? "🚰" : "⚡", daire_id, tip);
    guvenli_yazdir(buffer);
    
    sleep(2);   // Tesisatı kurulum süresi
    
    snprintf(buffer, sizeof(buffer), 
            "✅ Daire %d: %s tesisatı kurulumu tamamlandı\n", daire_id, tip);
    guvenli_yazdir(buffer);
    
    // İşçiyi serbest bırak
    sem_post(isci_sem);
    snprintf(buffer, sizeof(buffer), 
            "🔓 Daire %d: %s işçisi serbest bırakıldı\n", 
            daire_id, tip);
    guvenli_yazdir(buffer);
    
    // Kat mutex'ini serbest bırak - aynı kattaki bir sonraki daire başlayabilir
    pthread_mutex_unlock(kat_mutex);
    snprintf(buffer, sizeof(buffer), 
            "🔓 Daire %d: %s tesisatı kat sırası serbest bırakıldı (sıradaki daire başlayabilir)\n", 
            daire_id, tip);
    guvenli_yazdir(buffer);
}

/**
 * Yangın alarmı sistemi kurulum fonksiyonu
 * Paralel çalışma: Aynı kattaki daireler eş zamanlı alarm sistemi kurabilir
 * Sadece işçi sayısı sınırlaması var (semafore)
 */
void yangin_alarm_kur(int daire_id) {
    char buffer[MAX_BUFFER];
    
    // Yangın alarmı teknisyeni bekle - paralel çalışma için sadece işçi sınırlaması
    snprintf(buffer, sizeof(buffer), 
            "⏳ Daire %d: Yangın alarmı teknisyeni bekleniyor...\n", 
            daire_id);
    guvenli_yazdir(buffer);
    sem_wait(&yangin_alarm_sem);
    
    snprintf(buffer, sizeof(buffer), 
            "🚨 Daire %d: Yangın alarmı sistemi kurulumu başladı (paralel çalışma)\n", 
            daire_id);
    guvenli_yazdir(buffer);
    
    sleep(1);   // Yangın alarmı kurulum süresi (diğerlerinden daha hızlı)
    
    snprintf(buffer, sizeof(buffer), 
            "✅ Daire %d: Yangın alarmı sistemi kurulumu tamamlandı\n", daire_id);
    guvenli_yazdir(buffer);
    
    // Teknisyeni serbest bırak
    sem_post(&yangin_alarm_sem);
    snprintf(buffer, sizeof(buffer), 
            "🔓 Daire %d: Yangın alarmı teknisyeni serbest bırakıldı\n", 
            daire_id);
    guvenli_yazdir(buffer);
}

/**
 * Thread fonksiyonu - tek bir dairenin inşaat sürecini yönetir
 */
void* daire_insa_et(void* parametre) {
    DaireInfo* info = (DaireInfo*)parametre;
    char buffer[MAX_BUFFER];
    
    snprintf(buffer, sizeof(buffer), 
            "🏠 Daire %d başlıyor (Kat %d)\n", 
            info->global_daire_id, info->kat_no);
    guvenli_yazdir(buffer);
    
    // 1. Malzeme kontrolü - KRİTİK NOKTA
    if (!malzeme_islem(0, info->global_daire_id, info->kat_no, 0, 
                       info->pipe_talep_fd, info->pipe_cevap_fd)) {
        snprintf(buffer, sizeof(buffer), 
                "❌ Daire %d: Malzeme eksikliği nedeniyle inşaat durduruluyor!\n", 
                info->global_daire_id);
        guvenli_yazdir(buffer);
        
        snprintf(buffer, sizeof(buffer), 
                "🚨 Daire %d: Thread sonlandırılıyor (malzeme tükendi)\n", 
                info->global_daire_id);
        guvenli_yazdir(buffer);
        
        return NULL;  // Thread'i sonlandır
    }
    
    // 2-3. Asansör ve Vinç kullanımı
    kaynak_kullan(info->global_daire_id, "asansör", "", info->kat_no, &asansor_mutex);
    kaynak_kullan(info->global_daire_id, "vinç", "beton döküm", info->kat_no, &vinc_mutex);
    
    // 4-5. Tesisatı kurulumları (sıralı çalışma - ortak sistem)
    tesisati_kur(info->global_daire_id, "su", &tesisatci_sem, &kat_su_tesisati_mutex);
    tesisati_kur(info->global_daire_id, "elektrik", &elektrikci_sem, &kat_elektrik_mutex);
    
    // 6. Yangın alarmı sistemi (paralel çalışma - bağımsız sistem)
    yangin_alarm_kur(info->global_daire_id);
    
    // 7. İç işler
    snprintf(buffer, sizeof(buffer), "🎨 Daire %d: İç işler yapılıyor...\n", info->global_daire_id);
    guvenli_yazdir(buffer);
    sleep(2);
    
    // 8. Malzeme kullanımı ve bitiş
    malzeme_islem(DAIRE_MALZEME, info->global_daire_id, info->kat_no, 1, 
                  info->pipe_talep_fd, info->pipe_cevap_fd);
    
    snprintf(buffer, sizeof(buffer), "🎉 Daire %d TAMAMLANDI!\n", info->global_daire_id);
    guvenli_yazdir(buffer);
    
    return NULL;
}

/**
 * Tek bir katın inşaatını yöneten fonksiyon
 * Bu kat için 4 thread oluşturur (her daire için bir thread)
 */
void kat_insa_et(int kat_no, int pipe_talep_fd, int pipe_cevap_fd) {
    char buffer[MAX_BUFFER];
    pthread_t thread_listesi[DAIRE_SAYISI];
    DaireInfo daire_bilgileri[DAIRE_SAYISI];
    
    // Process içi senkronizasyon başlat
    process_senkronizasyon_baslat();
    
    snprintf(buffer, sizeof(buffer), 
            "\n🏗️  *** KAT %d İNŞAATI BAŞLIYOR (4 Daire Paralel) ***\n", kat_no);
    guvenli_yazdir(buffer);
    
    // Her daire için thread oluştur
    for (int daire = 1; daire <= DAIRE_SAYISI; daire++) {
        int global_id = ((kat_no-1) * DAIRE_SAYISI) + daire;
        
        // Daire bilgilerini hazırla
        daire_bilgileri[daire-1].kat_no = kat_no;
        daire_bilgileri[daire-1].daire_no = daire;
        daire_bilgileri[daire-1].global_daire_id = global_id;
        daire_bilgileri[daire-1].pipe_talep_fd = pipe_talep_fd;
        daire_bilgileri[daire-1].pipe_cevap_fd = pipe_cevap_fd;
        
        // Thread oluştur
        if (pthread_create(&thread_listesi[daire-1], NULL, 
                          daire_insa_et, &daire_bilgileri[daire-1]) != 0) {
            snprintf(buffer, sizeof(buffer), 
                    "❌ Daire %d için thread oluşturulamadı!\n", global_id);
            guvenli_yazdir(buffer);
        }
    }
    
    // Tüm thread'lerin bitmesini bekle - KAT İÇİ SENKRONİZASYON
    printf("⏳ Kat %d: Tüm dairelerin tamamlanması bekleniyor (pthread_join)...\n", kat_no);
    for (int daire = 0; daire < DAIRE_SAYISI; daire++) {
        int join_result = pthread_join(thread_listesi[daire], NULL);
        if (join_result == 0) {
            printf("✅ Kat %d, Daire %d thread'i başarıyla tamamlandı\n", kat_no, daire+1);
        } else {
            printf("❌ Kat %d, Daire %d thread join hatası!\n", kat_no, daire+1);
        }
    }
    
    snprintf(buffer, sizeof(buffer), 
            "✅ *** KAT %d İNŞAATI TAMAMLANDI (4 Daire) - Yapısal istikrar sağlandı ***\n", kat_no);
    guvenli_yazdir(buffer);
    
    // Process içi senkronizasyon temizle
    process_senkronizasyon_temizle();
}

/**
 * Process içi mutexleri ve semafore'ları başlatan fonksiyon
 */
void process_senkronizasyon_baslat() {
    // Process içi mutexleri başlat
    pthread_mutex_init(&vinc_mutex, NULL);
    pthread_mutex_init(&asansor_mutex, NULL);
    pthread_mutex_init(&kat_su_tesisati_mutex, NULL);
    pthread_mutex_init(&kat_elektrik_mutex, NULL);
    pthread_mutex_init(&konsol_mutex, NULL);
    pthread_mutex_init(&pipe_mutex, NULL);
    
    // Semafore'ları başlat (2 elektrikçi, 2 tesisatçı, 3 yangın alarmı teknisyeni)
    sem_init(&elektrikci_sem, 0, 2);
    sem_init(&tesisatci_sem, 0, 2);
    sem_init(&yangin_alarm_sem, 0, 3);
}

/**
 * Process içi senkronizasyon araçlarını temizleyen fonksiyon
 */
void process_senkronizasyon_temizle() {
    // Process içi mutexleri yok et
    pthread_mutex_destroy(&vinc_mutex);
    pthread_mutex_destroy(&asansor_mutex);
    pthread_mutex_destroy(&kat_su_tesisati_mutex);
    pthread_mutex_destroy(&kat_elektrik_mutex);
    pthread_mutex_destroy(&konsol_mutex);
    pthread_mutex_destroy(&pipe_mutex);
    
    // Semafore'ları yok et
    sem_destroy(&elektrikci_sem);
    sem_destroy(&tesisatci_sem);
    sem_destroy(&yangin_alarm_sem);
}

/**
 * Malzeme sunucusu fonksiyonu
 * Ana process'te çalışır ve child process'lerden gelen malzeme taleplerini karşılar
 */
void malzeme_sunucu_calistir(int pipe_talep_fd, int pipe_cevap_fd) {
    MalzemeTalebi talep;
    MalzemeCevabi cevap;
    int tamamlanan_daire = 0;
    
    printf("🏪 MALZEME DEPOSU HİZMETE BAŞLADI!\n");
    printf("   📦 Başlangıç stok: %d birim\n", toplam_malzeme);
    printf("   📋 Her daire için gerekli: %d birim\n", DAIRE_MALZEME);
    printf("   🏠 Toplam daire sayısı: %d\n", KAT_SAYISI * DAIRE_SAYISI);
    printf("   🎯 Hedef: Tüm malzeme tüketilmeli\n\n");
    
    while (1) {
        // Child process'lerden talep bekle
        ssize_t okunan = read(pipe_talep_fd, &talep, sizeof(MalzemeTalebi));
        
        if (okunan <= 0) {
            // Pipe kapandı, çık
            break;
        }
        
        if (talep.islem_turu == 0) {
            // Başlangıç kontrolü - malzeme yeterli mi?
            if (toplam_malzeme >= DAIRE_MALZEME) {
                cevap.basarili = 1;
                cevap.kalan_malzeme = toplam_malzeme;
            } else {
                // MALZEME YETERSİZ - TÜM İNŞAAT DURDURULSUN
                cevap.basarili = 0;
                cevap.kalan_malzeme = toplam_malzeme;
                malzeme_tukendi = 1;  // Global flag set et
                
                printf("🚨 KRİTİK UYARI: Malzeme tükendi! Daire %d için yeterli malzeme yok.\n", talep.daire_id);
                printf("📊 Mevcut malzeme: %d birim, Gerekli: %d birim\n", toplam_malzeme, DAIRE_MALZEME);
                printf("🛑 TÜM İNŞAAT SÜRECİ DURDURULACAK!\n\n");
            }
        } else if (talep.islem_turu == 1) {
            // Bitiş - malzeme kullanımını kaydet
            if (toplam_malzeme >= talep.talep_miktar) {
                toplam_malzeme -= talep.talep_miktar;
                tamamlanan_daire++;
                cevap.basarili = 1;
                cevap.kalan_malzeme = toplam_malzeme;
                
                // Her daire sonunda durum raporu
                printf("📊 MALZEME DURUMU: Daire %d tamamlandı - Kullanılan: %d birim, Kalan: %d birim (%d/%d daire)\n", 
                       talep.daire_id, talep.talep_miktar, toplam_malzeme, tamamlanan_daire, KAT_SAYISI * DAIRE_SAYISI);
                       
                // Eğer tüm daireler tamamlandıysa özel mesaj
                if (tamamlanan_daire == KAT_SAYISI * DAIRE_SAYISI) {
                    if (toplam_malzeme == 0) {
                        printf("🎯 MÜKEMMEL! Tüm malzeme başarıyla tüketildi!\n");
                    } else {
                        printf("✅ Tüm daireler tamamlandı! %d birim malzeme kaldı.\n", toplam_malzeme);
                    }
                }
            } else {
                cevap.basarili = 0;
                cevap.kalan_malzeme = toplam_malzeme;
            }
        } else if (talep.islem_turu == 2) {
            // Durum sorgulama - final rapor için
            cevap.basarili = 1;
            cevap.kalan_malzeme = toplam_malzeme;
        }
        
        // Cevabı gönder
        write(pipe_cevap_fd, &cevap, sizeof(MalzemeCevabi));
    }
}

/**
 * Ana fonksiyon
 * Apartman inşaatının genel akışını yönetir
 */
 
 void ciz_cati() {
    printf("      /\\\n");
    printf("     /  \\\n");
    printf("    /    \\\n");
    printf("   /      \\\n");
    printf("  / ÜÇBEY  \\\n");
    printf(" /APARTMANI \\\n");
    printf("/____________\\\n");
}

void ciz_kat(int kat_no) {
    for (int i = 0; i < 3; i++) {
        printf("|");
        for (int j = 0; j < 12; j++) {
            if (i == 1 && (j == 2 || j == 7)) {
                printf("[]"); // pencere
                j++; // çünkü pencere 2 karakter
            } else {
                printf(" ");
            }
        }
        printf("|\n");
    }
    // Kat zemini
    printf("+------------+\n");
}

void ciz_apartman() {
    ciz_cati();
    ciz_kat(2);
    ciz_kat(1);

    
}

int main() {
    printf("🏢 ÜÇBEY APARTMANI İNŞAAT SİMÜLASYONU BAŞLIYOR\n");
    printf("======================================\n");
    printf("📋 Proje Detayları:\n");
    printf("   🏗️  %d Katlı ÜÇBEY APARTMANI\n", KAT_SAYISI);
    printf("   🏠 Her katta %d daire (paralel inşaat)\n", DAIRE_SAYISI);
    printf("   🎯 Toplam %d daire inşa edilecek\n", KAT_SAYISI * DAIRE_SAYISI);
    printf("   📦 Başlangıç malzeme: %d birim\n", toplam_malzeme);
    printf("   💰 Her daire malzeme ihtiyacı: %d birim\n", DAIRE_MALZEME);
    printf("   🔧 Sınırlı kaynaklar: 1 Vinç, 1 Asansör, 2 Elektrikçi, 2 Tesisatçı, 3 Yangın Alarmı Teknisyeni\n");
    printf("   ⚠️  Önemli: Aynı kattaki daireler elektrik ve su tesisatını sıralı yapar (ortak sistem)\n");
    printf("   🚨 Yangın alarmı: Tüm dairelerde paralel kurulum (bağımsız sistem)\n\n");
    
    // Pipe'ları oluştur
    int pipe_talep[2], pipe_cevap[2];
    
    if (pipe(pipe_talep) == -1 || pipe(pipe_cevap) == -1) {
        perror("❌ Pipe oluşturulamadı");
        exit(1);
    }
    
    printf("🔧 İletişim kanalları oluşturuldu\n");
    
    // Malzeme sunucu process'ini başlat
    pid_t sunucu_pid = fork();
    if (sunucu_pid == 0) {
        close(pipe_talep[1]);
        close(pipe_cevap[0]);
        malzeme_sunucu_calistir(pipe_talep[0], pipe_cevap[1]);
        exit(0);
    }
    
    // Ana process pipe ayarları
    close(pipe_talep[0]);
    close(pipe_cevap[1]);
    
    // 1. TEMEL ATMA AŞAMASI
    printf("🏗️  TEMEL ATMA AŞAMASI\n");
    printf("=======================\n");
    printf("⏳ Temel atma işlemi başlıyor...\n");
    sleep(2);
    printf("🏗️  Kazı işlemleri...\n");
    sleep(1);
    printf("🏗️  Beton döküm...\n");
    sleep(2);
    printf("✅ Temel atma tamamlandı!\n\n");
    
    // 2. KAT VE DAİRE İNŞAAT AŞAMASI
    printf("🏠 KAT VE DAİRE İNŞAAT AŞAMASI BAŞLIYOR\n");
    printf("=====================================\n");
    printf("ℹ️  Her kat ayrı process, her daire ayrı thread olarak çalışacak\n");
    printf("ℹ️  Aynı kattaki 4 daire paralel inşa edilecek\n");
    printf("⚠️  YAPISAL İSTİKRAR: Alt kat tamamlanmadan üst kat başlamaz (wait() ile senkronizasyon)\n\n");
    
    // Her kat için ayrı process oluştur - SIRALI İNŞAAT
    for (int kat = 1; kat <= KAT_SAYISI; kat++) {
        // MALZEME KONTROL - Her kat öncesi kontrol et
        if (malzeme_tukendi) {
            printf("🚨 MALZEME TÜKENDİ! Kat %d ve sonraki katlar inşa edilemeyecek.\n", kat);
            printf("🛑 İnşaat süreci Kat %d'den önce sonlandırılıyor.\n", kat);
            break;  // Döngüyü kır, daha fazla kat inşa etme
        }
        
        printf("🏗️  === KAT %d İNŞAAT SÜRECİ BAŞLIYOR ===\n", kat);
        
        if (kat > 1) {
            printf("⏳ Yapısal istikrar için Kat %d bekleniyor (alt kat tamamlanmalı)...\n", kat-1);
        }
        
        pid_t kat_pid = fork();
        
        if (kat_pid == 0) {
            // Child process - bu kat için inşaat yap
            kat_insa_et(kat, pipe_talep[1], pipe_cevap[0]);
            exit(0);
        } else if (kat_pid < 0) {
            printf("❌ Kat %d için process oluşturulamadı!\n", kat);
            exit(1);
        } else {
            // Parent process - child'ın bitmesini bekle (YAPISAL İSTİKRAR)
            printf("⏳ Ana process Kat %d'in tamamlanmasını bekliyor (wait() komutu)...\n", kat);
            
            int status;
            pid_t tamamlanan_pid = wait(&status);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                printf("✅ Kat %d başarıyla tamamlandı! (PID: %d)\n", kat, tamamlanan_pid);
                printf("🏗️  Yapısal istikrar sağlandı - Kat %d üzerine inşaat yapılabilir\n", kat);
                
                // Malzeme tükenme kontrolü - kat tamamlandıktan sonra
                if (malzeme_tukendi) {
                    printf("🚨 Kat %d tamamlandı ancak malzeme tükendi!\n", kat);
                    printf("🛑 Sonraki katlar için malzeme yetersiz - İnşaat sonlandırılıyor.\n");
                    break;  // Ana döngüden çık
                }
            } else {
                printf("❌ Kat %d inşaatında hata oluştu!\n", kat);
                exit(1);
            }
            
            if (kat < KAT_SAYISI) {
                printf("⏳ Bir sonraki kata geçiliyor... (Kat %d → Kat %d)\n\n", kat, kat+1);
                sleep(1);
            }
        }
    }
    
    // Pipe'ları kapat ve malzeme sunucusunu bekle
    close(pipe_talep[1]);
    close(pipe_cevap[0]);
    wait(NULL);
    
    // 3. FINAL RAPORU
    printf("\n\n🎊 ÜÇBEY APARTMANI İNŞAAT SİMÜLASYONU TAMAMLANDI! 🎊\n");
    printf("===============================================\n");
    
    if (malzeme_tukendi) {
        printf("⚠️  UYARI: İnşaat malzeme yetersizliği nedeniyle erken sonlandı!\n");
        printf("📊 KISMI İNŞAAT RAPORU:\n");
    } else {
        printf("📊 BAŞARILI İNŞAAT RAPORU:\n");
    }
    
    printf("   🏗️  Toplam kat sayısı: %d\n", KAT_SAYISI);
    printf("   🏠 Toplam daire sayısı: %d\n", KAT_SAYISI * DAIRE_SAYISI);
    printf("   📦 Başlangıç malzeme: 80 birim\n");
    printf("   📦 Hedef malzeme tüketimi: %d birim (%d daire x %d birim)\n", 
           KAT_SAYISI * DAIRE_SAYISI * DAIRE_MALZEME, KAT_SAYISI * DAIRE_SAYISI, DAIRE_MALZEME);
    
    if (malzeme_tukendi) {
        printf("   🚨 Malzeme durumu: ❌ Tükendi (erken sonlandırma)\n");
        printf("   ⚠️  Sonuç: Kısmi inşaat tamamlandı\n");
    } else {
        printf("   📦 Malzeme durumu: ✅ Yeterli\n");
        printf("   ✅ Sonuç: Tam inşaat başarıyla tamamlandı\n");
    }
    
    printf("   💯 Eş zamanlı çalışma: ✅ Başarılı\n");
    printf("   🔧 Kaynak paylaşımı: ✅ Mutex/Semaphore ile korundu\n");
    printf("   📊 Malzeme yönetimi: ✅ Pipe ile inter-process communication\n");
    printf("   🏗️  Yapısal istikrar: ✅ wait() ile kat sıralı inşaat sağlandı\n");
    printf("   🧵 Thread senkronizasyonu: ✅ pthread_join() ile daire tamamlama\n");
    printf("   🔒 Tesisatı sıralama: ✅ Kat bazında mutex ile ortak sistem korundu\n");
    printf("   🚨 Yangın alarmı: ✅ Paralel kurulum ile hızlı tamamlama\n");
    
    if (malzeme_tukendi) {
        printf("\n⚠️  ÜÇBEY APARTMANI KISMI OLARAK KULLANIMA HAZIR!\n");
        printf("   (Malzeme yetersizliği nedeniyle tüm katlar tamamlanamadı)\n");
    } else {
        printf("\n🏢 ÜÇBEY APARTMANI TAMAMEN KULLANIMA HAZIR!\n");
    }
    
    ciz_apartman();
    return 0;
}