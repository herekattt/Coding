/*
 * ============================================
 * ESP32 MASTER - KONTROL PUSAT (WEBSOCKET + ESP-NOW)
 * ============================================
 * Terhubung ke PC Host via WebSocket (WiFi)
 * Mengontrol 2 Client Meja via ESP-NOW
 * 
 * Library yang dibutuhkan:
 * - WebSockets by Markus Sattler (Links2004)
 *   Install dari Library Manager
 * ============================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebSocketsClient.h>

// ============================================
// KONFIGURASI WIFI & WEBSOCKET
// ============================================
const char* ssid     = "NAMA_WIFI_ANDA";      // Ganti dengan SSID WiFi Anda
const char* password = "PASSWORD_WIFI_ANDA";  // Ganti dengan password WiFi

// Alamat server WebSocket (PC Host)
const char* websocket_server = "192.168.1.100"; // Ganti dengan IP PC Host
const uint16_t websocket_port = 81;             // Port WebSocket (default 81)
const char* websocket_path = "/";               // Endpoint path, sesuaikan dengan server

// ============================================
// KONFIGURASI MAC ADDRESS 2 CLIENT
// ============================================
uint8_t clientMAC[2][6] = {
  {0x68, 0xFE, 0x71, 0x16, 0xA6, 0x5C},  // Meja 1
  {0x3C, 0x71, 0xBF, 0x7E, 0x34, 0x0C}   // Meja 2
};

// ============================================
// STRUKTUR DATA KOMUNIKASI ESP-NOW
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
// OBJEK WEBSOCKET
// ============================================
WebSocketsClient webSocket;

// ============================================
// BUFFER SERIAL (untuk debugging)
// ============================================
String serialBuffer = "";

// ============================================
// CALLBACK ESP-NOW
// ============================================

// Callback Terima Data dari Client
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

  // Kirim informasi ACK ke PC Host melalui WebSocket
  String ackMsg = "ACK," + String(ackData.meja_id) + "," +
                  String(ackData.status) + "," + String(ackData.ack) + "," +
                  String(ackData.nama);
  webSocket.sendTXT(ackMsg);
  
  // Juga tampilkan di Serial untuk debugging
  Serial.print("[WEB] Kirim ke PC: ");
  Serial.println(ackMsg);
}

// Callback Pengiriman Data (Core ESP32 3.x)
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW] Status kirim perintah: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sukses" : "Gagal");
}

// ============================================
// FUNGSI KIRIM PERINTAH KE CLIENT (ESP-NOW)
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
    // Kirim feedback ke PC bahwa perintah telah dikirim
    String feedback = "SEND," + String(mejaID) + "," + String(perintah) + ",OK";
    webSocket.sendTXT(feedback);
  } else {
    Serial.println("[MASTER] Gagal mengirim perintah!");
    String feedback = "SEND," + String(mejaID) + "," + String(perintah) + ",FAIL";
    webSocket.sendTXT(feedback);
  }
}

// ============================================
// FUNGSI PROSES PERINTAH (dari WebSocket / Serial)
// ============================================
void prosesPerintah(String input) {
  input.trim();
  input.toUpperCase();

  Serial.print("[CMD] Perintah diterima: ");
  Serial.println(input);

  // Kirim status semua meja jika diminta
  if (input == "STATUS") {
    Serial.println("========================================");
    Serial.println("          STATUS SEMUA MEJA");
    Serial.println("========================================");
    String statusMsg = "STATUS";
    for (int i = 0; i < 2; i++) {
      Serial.print("Meja ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(statusMeja[i] ? "ON" : "OFF");
      statusMsg += "," + String(statusMeja[i] ? "1" : "0");
    }
    Serial.println("========================================");
    
    // Kirim status ke PC
    webSocket.sendTXT(statusMsg);
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
// CALLBACK WEBSOCKET (dari PC Host)
// ============================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WEB] Terputus dari server WebSocket");
      break;
    case WStype_CONNECTED:
      Serial.println("[WEB] Terhubung ke server WebSocket");
      // Kirim pesan sambutan
      webSocket.sendTXT("MASTER_ONLINE");
      break;
    case WStype_TEXT:
      {
        String pesan = String((char*)payload).substring(0, length);
        Serial.print("[WEB] Pesan dari PC: ");
        Serial.println(pesan);
        // Proses perintah yang sama seperti dari Serial
        prosesPerintah(pesan);
      }
      break;
    case WStype_BIN:
      Serial.println("[WEB] Data biner diterima (tidak diproses)");
      break;
    case WStype_ERROR:
      Serial.println("[WEB] Error pada WebSocket");
      break;
    default:
      break;
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("   ESP32 MASTER - KONTROL PUSAT (WS)");
  Serial.println("========================================");

  // ---- Koneksi WiFi ----
  Serial.print("Menghubungkan ke WiFi ");
  Serial.print(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi terhubung, IP: ");
  Serial.println(WiFi.localIP());

  // Matikan WiFi power save untuk stabilitas ESP-NOW
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Atur channel WiFi sama dengan AP (penting untuk ESP-NOW)
  esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
  Serial.print("Channel WiFi: ");
  Serial.println(WiFi.channel());

  // ---- Inisialisasi ESP-NOW ----
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Gagal inisialisasi ESP-NOW!");
    return;
  }

  // Register callback ESP-NOW
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  // Register semua Client sebagai peer
  Serial.println("Mendaftarkan 2 Client sebagai peer...");
  for (int i = 0; i < 2; i++) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, clientMAC[i], 6);
    peerInfo.channel = WiFi.channel(); // Gunakan channel yang sama dengan WiFi
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

  // ---- Inisialisasi WebSocket ----
  Serial.println("Menghubungkan ke server WebSocket...");
  webSocket.begin(websocket_server, websocket_port, websocket_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // Reconnect tiap 5 detik jika putus

  Serial.println();
  Serial.println("[OK] ESP32 Master siap!");
  Serial.println("Protokol WebSocket aktif");
  Serial.println("========================================");
  Serial.println("Format perintah dari PC (via WebSocket):");
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
  // Selalu proses WebSocket (termasuk reconnect)
  webSocket.loop();

  // Cek koneksi WiFi, reconnect jika putus
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Terputus, mencoba reconnect...");
    WiFi.reconnect();
    delay(1000);
  }

  // Baca data dari Serial (untuk debugging / perintah lokal)
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n') {
      prosesPerintah(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
    }
  }

  delay(10);
}