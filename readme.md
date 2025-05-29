# ÜÇBEY APARTMANI İNŞAAT SİMÜLASYONU

## 🏗️ PROJE ADI:
Çok Katlı Bir Apartmanın İnşası Üzerinden Process, Thread ve Senkronizasyon Kavramlarının Modellenmesi

---

## 👨‍💻 HAZIRLAYANLAR:
- Levent Kutay SEZER — 22360859013  
- Anıl SÜRMELİ — 22360859018


---

## 1. GİRİŞ

Bu projede, çok katlı bir apartman inşaat süreci üzerinden process, thread ve senkronizasyon kavramları C dili ile modellenmiştir.  
Projede sınırlı kaynakların (vinç, asansör, tesisatçı vb.) birden fazla iş parçacığı tarafından nasıl kullanılacağı gösterilmiş ve `mutex`, `semaphore`, `pipe` gibi araçlarla senkronizasyon sağlanmıştır.

---

## 2. MİMARİ YAPI VE FONKSİYONLAR

### ✅ Kullanılan Teknolojiler
- **Dil**: C
- **Kütüphaneler**: `pthread.h`, `semaphore.h`, `unistd.h`, `stdio.h`, `stdlib.h`
- **IPC**: `pipe()`, `fork()`

### ✅ Paralellik Yapısı
- Her daire bir `thread` ile inşa edilir.
- Her kat bir `process` ile temsil edilir.
- Kaynaklar `mutex` ve `semaphore` ile korunur.

### 🔍 Temel Parametreler
- `KAT_SAYISI = 10`
- `DAIRE_SAYISI = 4`
- `DAIRE_MALZEME = 2`
- `MAX_BUFFER = 512`

### 🔧 Fonksiyonlar

| Fonksiyon | Açıklama |
|----------|----------|
| `guvenli_yazdir()` | Konsola thread-safe yazı basar |
| `malzeme_islem()` | Malzeme kontrolü ve kullanımı (pipe ile iletişim) |
| `kaynak_kullan()` | Vinç ve asansör gibi kaynakları korur |
| `tesisati_kur()` | Su ve elektrik tesisatını sırayla kurar |
| `yangin_alarm_kur()` | Paralel çalışan yangın alarm sistemini kurar |
| `daire_insa_et()` | Her dairenin inşaat süreci |
| `kat_insa_et()` | 4 dairelik bir katın inşası |
| `process_senkronizasyon_baslat()` | Mutex ve semaforları başlatır |
| `process_senkronizasyon_temizle()` | Mutex ve semaforları yok eder |
| `malzeme_sunucu_calistir()` | Merkezi malzeme deposunu yönetir |
| `main()` | Projenin genel yürütücüsüdür |

---

## 3. YARIŞ KOŞULLARI, SENKRONİZASYON VE PERFORMANS ANALİZİ

### 🔐 Mutex Kullanımı
- `vinc_mutex`, `asansor_mutex`, `konsol_mutex` gibi kaynaklar tek seferde tek thread tarafından kullanılabilir.

### 🚦 Semafor Kullanımı
- `tesisatci_sem`, `elektrikci_sem`, `yangin_alarm_sem` ile sınırlı sayıda işçi modeli uygulanır.

### ⛔ Yarış Koşulları Önleme
- Mutex sıralaması dikkatle tasarlanmıştır.
- Kaynak paylaşımı sırasında çakışmalar ve deadlock ihtimalleri önlenmiştir.

### 📈 Performans Ölçümü
- `gettimeofday()` fonksiyonu ile işlem süreleri ölçülüp darboğazlar analiz edilmiştir.

---

## 4. KARŞILAŞILAN ZORLUKLAR

- Thread senkronizasyonunda başlangıçta karmaşalar yaşanmıştır.
- Pipe yapısının doğru kullanılmaması sonucu hatalar oluşmuştur.
- Konsol çıktılarında karışıklıklar hata ayıklamayı zorlaştırmıştır.
- Deadlock ihtimali nedeniyle kod düzenlemeleri yapılmıştır.

---

## 5. GELİŞTİRİLEBİLİR YAPILAR

- **Grafiksel Arayüz (GUI)**: Kaynaklar ve süreçler gerçek zamanlı izlenebilir.
- **Loglama Sistemi**: Tüm işlemler tarih/saat bazlı kayıt altına alınabilir.
- **Yapay Zekâ Tabanlı Modüller**: Kaynak kullanım tahminleme sistemleri geliştirilebilir.

---

## 6. SONUÇ

Bu proje ile işletim sistemlerinde çok iş parçacıklı ve çok süreçli yapıların gerçek dünya analojileriyle nasıl uygulanabileceği gösterilmiştir.  
Kaynak yönetimi, eş zamanlılık ve senkronizasyon gibi temel konular C diliyle başarıyla modellenmiş ve uygulanmıştır.  
Projede yapılandırılan model; mühendislikte karşılaşılabilecek karmaşık sistemlerin yazılımla nasıl kontrol altına alınabileceğini göstermektedir.

---

## 📁 Proje Raporu

Resmî raporu PDF olarak görüntülemek için:
[📄 `insaat.pdf`](./insaat.pdf)

---

## 🏁 Projeyi Çalıştırmak

```bash
gcc -o apartman proje.c -lpthread
./apartman
