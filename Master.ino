/*
 * ============================================
 * ESP32 MASTER - KONTROL PUSAT (REVISI PERBAIKAN)
 * ============================================
 * Terhubung ke Komputer via Serial/UART
 * Mengontrol 2 Client Meja via ESP-NOW
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ============================================
// KONFIGURASI MAC ADDRESS 2 CLIENT
// ============================================
// Ganti dengan MAC Address asli dari hasil pembacaan
uint8_t clientMAC[2][6] = {
  {0x68, 0xFE, 0x71, 0x16, 0xA6, 0x5C},  // Meja 1
  {0x3C, 0x71, 0xBF, 0x7E, 0x34, 0x0C}   // Meja 2
}; 

// ============================================
// STRUKTUR DATA KOMUNIKASI
// ============================================
typedef struct __attribute__((packed)) {
  uint8_t  meja_id;
  uint8_t  perintah;     // 1 = ON, 0 = OFF
  uint16_t checksum;
} PerintahData;

typedef struct __attribute__((packed)) {
  uint8_t  meja_id;
  uint8_t  status;
  uint8_t  ack;
  char     nama[10];
} AckData;

PerintahData cmdData;
AckData ackData;

// ============================================
// VARIABEL STATUS
// ============================================
bool statusMeja[2] = {false, false};

// ============================================
// BUFFER SERIAL
// ============================================
String serialBuffer = "";

// ============================================
// CALLBACK ESP-NOW
// ============================================

// Callback Terima Data
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  memcpy(&ackData, incomingData, sizeof(ackData));
  
  Serial.print("[ESP-NOW] ACK diterima dari ");
  Serial.print(ackData.nama);
  Serial.print(" | Status Relay: ");
  Serial.print(ackData.status == 1 ? "ON" : "OFF");
  Serial.print(" | ACK: ");
  Serial.println(ackData.ack == 1 ? "SUKSES" : "GAGAL");
  
  // Update status internal
  if (ackData.meja_id >= 1 && ackData.meja_id <= 2) {
    statusMeja[ackData.meja_id - 1] = (ackData.status == 1);
  }
  
  // Forward ke Serial UART untuk komputer
  Serial.print("ACK,");
  Serial.print(ackData.meja_id);
  Serial.print(",");
  Serial.print(ackData.status);
  Serial.print(",");
  Serial.print(ackData.ack);
  Serial.print(",");
  Serial.println(ackData.nama);
}

// Callback Pengiriman Data (Disesuaikan dengan Core 3.x)
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW] Status kirim perintah: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sukses" : "Gagal");
}

// ============================================
// FUNGSI KIRIM PERINTAH KE CLIENT
// ============================================
void kirimPerintah(uint8_t mejaID, uint8_t perintah) {
  if (mejaID < 1 || mejaID > 2) {
    Serial.println("[ERROR] ID Meja tidak valid! (1-2)");
    return;
  }
  
  cmdData.meja_id = mejaID;
  cmdData.perintah = perintah;
  cmdData.checksum = mejaID + perintah;  // Validasi sederhana
  
  uint8_t *targetMAC = clientMAC[mejaID - 1];
  
  Serial.print("[MASTER] Mengirim perintah ke Meja ");
  Serial.print(mejaID);
  Serial.print(": ");
  Serial.println(perintah == 1 ? "ON" : "OFF");
  
  esp_err_t result = esp_now_send(targetMAC, (uint8_t *)&cmdData, sizeof(cmdData));
  
  if (result == ESP_OK) {
    Serial.println("[MASTER] Perintah berhasil masuk antrean kirim.");
  } else {
    Serial.println("[MASTER] Gagal mengirim perintah!");
  }
}

// ============================================
// FUNGSI PROSES PERINTAH SERIAL DARI KOMPUTER
// ============================================
void prosesSerial(String input) {
  input.trim();
  input.toUpperCase();
  
  Serial.print("[SERIAL] Perintah diterima: ");
  Serial.println(input);
  
  if (input == "STATUS") {
    Serial.println("========================================");
    Serial.println("          STATUS SEMUA MEJA");
    Serial.println("========================================");
    for (int i = 0; i < 2; i++) {
      Serial.print("Meja ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(statusMeja[i] ? "ON" : "OFF");
    }
    Serial.println("========================================");
    return;
  }
  
  // Format: ALL,ON atau ALL,OFF
  if (input.startsWith("ALL,")) {
    String cmd = input.substring(4);
    uint8_t val = (cmd == "ON") ? 1 : 0;
    Serial.print("[MASTER] Broadcast perintah ke SEMUA meja: ");
    Serial.println(cmd);
    
    for (int i = 1; i <= 2; i++) {
      kirimPerintah(i, val);
      delay(50);  // Jeda antar pengiriman
    }
    return;
  }
  
  // Format: M1,ON atau M2,OFF
  if (input.startsWith("M") && input.indexOf(",") > 0) {
    int commaIndex = input.indexOf(",");
    String idStr = input.substring(1, commaIndex);
    String cmd = input.substring(commaIndex + 1);
    
    int mejaID = idStr.toInt();
    uint8_t val = (cmd == "ON") ? 1 : 0;
    
    if (mejaID >= 1 && mejaID <= 2) {
      kirimPerintah(mejaID, val);
    } else {
      Serial.println("[ERROR] ID Meja harus 1-2!");
    }
    return;
  }
  
  Serial.println("[ERROR] Format perintah tidak dikenali!");
  Serial.println("Format valid: M1,ON | M2,OFF | ALL,ON | ALL,OFF | STATUS");
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("      ESP32 MASTER - KONTROL PUSAT");
  Serial.println("========================================");
  
  // Inisialisasi WiFi mode STA untuk ESP-NOW
  WiFi.mode(WIFI_STA);
  
  // Kunci Wi-Fi di Channel 1 agar sinkron dengan Client
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
  
  Serial.print("Master MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println();
  
  // Inisialisasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Gagal inisialisasi ESP-NOW!");
    return;
  }
  
  // Register callback
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);
  
  // Register semua Client sebagai peer
  Serial.println("Mendaftarkan 2 Client sebagai peer...");
  for (int i = 0; i < 2; i++) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, clientMAC[i], 6);
    peerInfo.channel = 1; // Ubah ke 1 agar sama dengan Client
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.print("  [OK] Meja ");
      Serial.print(i + 1);
      Serial.print(" -> MAC: ");
      for (int j = 0; j < 6; j++) {
        if (j > 0) Serial.print(":");
        Serial.printf("%02X", clientMAC[i][j]);
      }
      Serial.println();
    } else {
      Serial.print("  [FAIL] Meja ");
      Serial.print(i + 1);
      Serial.println(" gagal didaftarkan!");
    }
  }
  
  Serial.println();
  Serial.println("[OK] ESP32 Master siap!");
  Serial.println("Protokol Serial UART aktif (115200 baud)");
  Serial.println("========================================");
  Serial.println("Format perintah dari komputer:");
  Serial.println("  M1,ON    -> Meja 1 ON");
  Serial.println("  M2,OFF   -> Meja 2 OFF");
  Serial.println("  ALL,ON   -> Semua meja ON");
  Serial.println("  ALL,OFF  -> Semua meja OFF");
  Serial.println("  STATUS   -> Cek status semua meja");
  Serial.println("========================================");
}

// ============================================
// LOOP
// ============================================
void loop() {
  // Baca data dari Serial (komputer)
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n') {
      prosesSerial(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
    }
  }
  
  delay(10);
}