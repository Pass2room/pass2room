#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ตั้งค่าขนาดจอ OLED (ส่วนใหญ่คือ 128x64)
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int LED_PIN = 18;    
const int BUZZER_PIN = 19; 
const int BUTTON_PIN = 4;  

bool isPressed = false; 

void setup() {
  Serial.begin(115200);
  delay(1000);

  // เริ่มต้น I2C สำหรับ ESP32 (SDA=21, SCL=22)
  Wire.begin(21, 22); 

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, HIGH); // Active Low: HIGH = เงียบ

  // เริ่มต้นจอ OLED (Address 0x3C คือค่ามาตรฐานของ OLED)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // หยุดทำงานถ้าหาจอไม่เจอ
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  showWaiting();
  
  Serial.println("System Ready!");
}

void loop() {
  int buttonState = digitalRead(BUTTON_PIN);

  if (buttonState == LOW) { 
    if (!isPressed) {
      Serial.println("EVENT: Button Pressed");
      
      digitalWrite(LED_PIN, HIGH);
      
      // แสดงผลบน OLED
      display.clearDisplay();
      display.setTextSize(2);      // ขนาดตัวอักษรใหญ่ขึ้น
      display.setCursor(10, 10);   // ตั้งตำแหน่ง (x, y)
      display.println("USERNAME");
      display.setCursor(10, 40);
      display.println("UNLOCK");
      display.display();           // ต้องใช้คำสั่งนี้เสมอเพื่อให้ภาพขึ้นจอ
      
      beep(2);
      isPressed = true; 
    }
  } else { 
    if (isPressed) {
      Serial.println("EVENT: Button Released");
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, HIGH);
      
      showWaiting();
      isPressed = false;
    }
  }
  delay(50);
}

void showWaiting() {
  display.clearDisplay();
  display.setTextSize(1);      // ขนาดปกติ
  display.setCursor(0, 10);
  display.println("Waiting...");
  display.setCursor(0, 30);
  display.println("Press the button");
  display.display();           // อัปเดตหน้าจอ
}

void beep(int count) {
  for(int i=0; i<count; i++) {
    digitalWrite(BUZZER_PIN, LOW); 
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
  }
}