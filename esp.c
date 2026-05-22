#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// CẤU HÌNH WIFI
const char* ssid = "";
const char* password = "";

//CẤU HÌNH FIREBASE
#define FIREBASE_HOST "redtime-c3ac7-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "kJX8U4bM9ip1pnTjQsVhtgSVF33kAOghcMmhSkMV"

//CẤU HÌNH TELEGRAM
#define BOT_TOKEN "8792433725:AAFrjfJvMh3wY3-qrwgdENGk5_elIcMeFtg" 
#define CHAT_ID "6825728761"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

//CẤU HÌNH THỜI GIAN NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000); // Múi giờ VN (UTC+7): 7 * 3600 = 25200

#define BUZZER D5
#define LED D6
int threshold = 200;

// Các biến cờ trạng thái
bool isNotified = false; 
bool isAlertSaved = false;

// Biến lưu thời gian đếm 2 tiếng (2 giờ = 7200000 ms)
unsigned long previousMillis = 0;
const long interval2Hours = 7200000; 

// Hàm gửi tin nhắn Telegram
void sendTelegramMessage(String message) {
  WiFiClientSecure client;
  client.setInsecure(); // Bỏ qua xác thực chứng chỉ SSL
  HTTPClient http;

  // Ghép chuỗi URL API của Telegram
  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage?chat_id=" + String(CHAT_ID) + "&text=" + message;

  Serial.print("Dang gui Telegram... ");
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("OK!");
    } else {
      Serial.printf("Loi: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println("Khong the ket noi den Telegram");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("Dang ket noi WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi OK");

  // Cấu hình Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  fbdo.setResponseSize(1024);
  
  // Khởi động đồng bộ thời gian NTP
  timeClient.begin();
}

void loop() {
  // Cập nhật thời gian liên tục
  timeClient.update(); 
  
  // Lấy thời gian để tạo đường dẫn Firebase
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime);
  
  char datePath[20];
  // Tạo định dạng thư mục: Năm/Tháng/Ngày
  sprintf(datePath, "%04d/%02d/%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
  String currentPath = String(datePath); 
  String currentTime = timeClient.getFormattedTime(); // Định dạng HH:MM:SS

  int sensorValue = analogRead(A0);
  int value = (sensorValue > threshold) ? 1 : 0;
  digitalWrite(BUZZER, value);
  digitalWrite(LED, value);

  //TẠO GÓI DỮ LIỆU ĐỂ GỬI LÊN FIREBASE
  FirebaseJson json;
  json.set("analog", sensorValue);
  json.set("canh_bao", value);
  json.set("thoi_gian", currentTime);

  // 1. GỬI DỮ LIỆU THỰC TẾ (REALTIME) MỖI 3 GIÂY
  Firebase.setJSON(fbdo, "/thiet_bi_1/realtime", json);

  // 2. LƯU LỊCH SỬ MỖI 2 GIỜ (Vào nhánh history/Năm/Tháng/Ngày)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval2Hours) {
    previousMillis = currentMillis;
    Firebase.pushJSON(fbdo, "/thiet_bi_1/history/" + currentPath, json);
    Serial.println("Đã lưu lịch sử định kỳ 2 giờ.");
  }

  // 3. XỬ LÝ GỬI TELEGRAM VÀ LƯU CẢNH BÁO FIREBASE
  if (value == 1 && !isNotified) {
    // Có khói -> Gửi Telegram
    sendTelegramMessage("🚨 CẢNH BÁO: Phát hiện khói lúc " + currentTime + "! Mức độ: " + String(sensorValue));
    isNotified = true; 
    
    // Lưu vào nhánh alerts của Firebase 
    if (!isAlertSaved) {
      Firebase.pushJSON(fbdo, "/thiet_bi_1/alerts/" + currentPath, json);
      isAlertSaved = true;
      Serial.println("Đã lưu lịch sử cảnh báo khói.");
    }
  } 
  else if (value == 0 && isNotified) {
    // Đã hết khói -> Gửi Telegram thông báo an toàn
    sendTelegramMessage("✅ An toàn: Khói đã tan lúc " + currentTime + ".");
    isNotified = false; 
    isAlertSaved = false; // Reset lại để chu kỳ sau có khói lại lưu tiếp
  }

  delay(3000);
}