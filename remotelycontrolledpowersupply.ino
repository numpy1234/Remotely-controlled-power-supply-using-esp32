#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_GFX_Library.h>

// --- Pins ---
const int dacPin = 25;
const int switchPin = 33;    
const int btnUpSmall = 34;   
const int btnUpBig = 26;     
const int btnDownSmall = 27; 
const int btnDownBig = 14;   

#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

// --- Display Pins (8-bit Parallel) ---
#define TFT_DC 13
#define TFT_CS 15
#define TFT_WR 22
#define TFT_RD -1    
#define TFT_RST 23
#define TFT_D0 2
#define TFT_D1 4
#define TFT_D2 32
#define TFT_D3 12
#define TFT_D4 5
#define TFT_D5 18
#define TFT_D6 19
#define TFT_D7 21

// --- System ---
const float systemGain = 4.56;
const float maxVoltage = 15.0; 
const float minVoltage = 0.0;

// --- State ---
float targetV = 5.0;
bool isManual = false;
bool lastUpSmall = HIGH, lastUpBig = HIGH, lastDownSmall = HIGH, lastDownBig = HIGH;

unsigned long lastDebounceTime = 0;
const int debounceDelay = 50; 

// --- Display Initialization ---
Arduino_DataBus *bus = new Arduino_ESP32PAR8(TFT_DC, TFT_CS, TFT_WR, TFT_RD, TFT_D0, TFT_D1, TFT_D2, TFT_D3, TFT_D4, TFT_D5, TFT_D6, TFT_D7);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST);

AsyncWebServer server(80);

// --- HTML Page ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial; text-align: center; background: #111; color: #00ffcc; }
.container { margin-top: 50px; border: 2px solid #00ffcc; display: inline-block; padding: 20px; border-radius: 10px; }
.slider { width: 80%; accent-color: #00ffcc; }
</style>
</head>
<body>
<div class="container">
  <h2>Output Voltage Control</h2>
  <h1 id="val">5.0 V</h1>
  <input type="range" min="0" max="150" value="50" class="slider" id="slider">
</div>
<script>
let s = document.getElementById("slider");
s.oninput = function() {
  let v = (this.value / 10).toFixed(1);
  document.getElementById("val").innerHTML = v + " V";
  fetch(`/set?v=${v}`);
}
setInterval(() => {
  fetch('/status').then(r => r.text()).then(d => {
    if(document.activeElement.id !== "slider") {
      document.getElementById("val").innerHTML = d + " V";
      s.value = parseFloat(d) * 10;
    }
  });
}, 500);
</script>
</body>
</html>
)rawliteral";

void updateScreen() {
  gfx->fillScreen(BLACK);
  gfx->setCursor(10, 10);
  gfx->setTextColor(CYAN);
  gfx->setTextSize(2);
  gfx->println("NSUT EDW SUPPLY");
  gfx->drawFastHLine(0, 35, 320, WHITE);

  gfx->setCursor(70, 80);
  gfx->setTextColor(YELLOW);
  gfx->setTextSize(7);
  gfx->print(targetV, 1);
  gfx->setTextSize(3);
  gfx->print(" V");

  gfx->setCursor(10, 200);
  gfx->setTextSize(2);
  if (isManual) {
    gfx->setTextColor(RED);
    gfx->print("MODE: MANUAL");
  } else {
    gfx->setTextColor(GREEN);
    gfx->print("MODE: WEB CONTROL");
  }
}

void applyVoltage(float v) {
  float correctedV = v-0.2;
  
  // Apply -0.25V offset logic for range 0.3 to 3.4
  if (correctedV >= 0.3 && correctedV <= 3.4) {
    correctedV = correctedV-0.2;
  }
  if (correctedV >= 7.5 && correctedV <= 10.5) {
    correctedV = correctedV+0.15;
  }
  if (correctedV > 10.5) {
    correctedV = correctedV+0.2;
  }
  
  if (correctedV < 0.0) correctedV = 0.0;

  int dacValue = (int)((correctedV / systemGain / 3.3) * 255);
  dacValue = constrain(dacValue, 0, 255);
  dacWrite(dacPin, dacValue);
  
  Serial.printf("Target: %.1fV | Corrected: %.2fV | DAC: %d\n", v, correctedV, dacValue);
  updateScreen(); 
}

void handleButtons() {
  if (millis() - lastDebounceTime < debounceDelay) return;

  bool curUpSmall = digitalRead(btnUpSmall);
  bool curUpBig = digitalRead(btnUpBig);
  bool curDownSmall = digitalRead(btnDownSmall);
  bool curDownBig = digitalRead(btnDownBig);

  bool changed = false;

  if (curUpSmall == LOW && lastUpSmall == HIGH) { targetV += 0.1; changed = true; }
  if (curUpBig == LOW && lastUpBig == HIGH)      { targetV += 1.0; changed = true; }
  if (curDownSmall == LOW && lastDownSmall == HIGH) { targetV -= 0.1; changed = true; }
  if (curDownBig == LOW && lastDownBig == HIGH)     { targetV -= 1.0; changed = true; }

  if (changed) {
    targetV = constrain(targetV, minVoltage, maxVoltage);
    applyVoltage(targetV);
    lastDebounceTime = millis();
  }

  lastUpSmall = curUpSmall;
  lastUpBig = curUpBig;
  lastDownSmall = curDownSmall;
  lastDownBig = curDownBig;
}

void setup() {
  Serial.begin(115200);

  if (!gfx->begin()) Serial.println("LCD Error");
  gfx->setRotation(3);
  gfx->fillScreen(BLACK);

  pinMode(switchPin, INPUT_PULLUP);
  pinMode(btnUpSmall, INPUT_PULLUP);
  pinMode(btnUpBig, INPUT_PULLUP);
  pinMode(btnDownSmall, INPUT_PULLUP);
  pinMode(btnDownBig, INPUT_PULLUP);

  WiFi.softAP("PowerSupply_Control", "12345678");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *f){ f->send_P(200, "text/html", index_html); });
  
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *f){
    if (!isManual && f->hasParam("v")) { 
      float val = f->getParam("v")->value().toFloat(); 
      targetV = constrain(val, minVoltage, maxVoltage);
      applyVoltage(targetV); 
    }
    f->send(200, "text/plain", "OK");
  });
  
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *f){ f->send(200, "text/plain", String(targetV, 1)); });

  server.begin();
  applyVoltage(targetV); 
}

void loop() {
  bool currentMode = (digitalRead(switchPin) == LOW);
  if (currentMode != isManual) {
    isManual = currentMode;
    updateScreen(); 
  }

  if (isManual) {
    handleButtons();
  }
  delay(10);
}
