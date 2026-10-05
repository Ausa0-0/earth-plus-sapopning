#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>
#include "secrets.h"

// ======= User config =======
const char* WIFI_SSID = PROJECT_WIFI_SSID;
const char* WIFI_PASS = PROJECT_WIFI_PASS;

// Pins
const int SOIL_PIN = 32; // H1 soil sensor AOUT
const int DHT_PIN = 4;   // DHT data pin
const int PUMP_GATE_PIN = 27; // IRLZ44N MOSFET gate   control
const int SW1_PIN = 13; // Select Auto mode
const int SW2_PIN = 14; // Toggle pump in Manual mode
const int SW3_PIN = 25; // Select Manual mode

// Calibration for soil sensor (raw ADC values)
// Adjust these values for your sensor: dry -> near-air reading, wet -> submerged in water
const int SOIL_RAW_DRY = 3000; // raw value when dry (adjust)
const int SOIL_RAW_WET = 1200; // raw value when wet (adjust)

// Thresholds
const int THRESHOLD_DRY = 35; // percent or equal -> consider dry -> start pump

// Globals
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool displayReady = false;
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
DHT dht(DHT_PIN, DHT22);

bool autoMode = true;
bool pumpState = false;

unsigned long lastSensorMillis = 0;
unsigned long sensorInterval = 2000; // ms
float lastTemp = 0.0;
bool lastTempValid = false;
int lastSoilPercent = -1;
int lastGoodSoilPercent = -1;
uint8_t consecutiveSoilFailures = 0;
const uint8_t soilFailureLimit = 3;

// Debounce
unsigned long lastSw1 = 0, lastSw2 = 0, lastSw3 = 0;
const unsigned long debounce = 200;
bool sw1WasPressed = false;
bool sw2WasPressed = false;
bool sw3WasPressed = false;

void setPump(bool on) {
  pumpState = on;
  digitalWrite(PUMP_GATE_PIN, on ? HIGH : LOW);
}

void handleSerialPumpCommands() {
  while (Serial.available() > 0) {
    const char command = Serial.read();
    if (command == '1') {
      autoMode = false;
      setPump(true);
      Serial.println("Manual mode: pump ON");
    } else if (command == '0') {
      autoMode = false;
      setPump(false);
      Serial.println("Manual mode: pump OFF");
    }
  }
}

int readSoilRaw() {
  const int sampleCount = 8;
  uint32_t total = 0;
  for (int sample = 0; sample < sampleCount; sample++) {
    total += analogRead(SOIL_PIN);
    delay(5);
  }
  return total / sampleCount;
}

int soilPercentFromRaw(int raw) {
  if (raw == 0) return -1;
  // constrain and map to 0-100
  raw = constrain(raw, SOIL_RAW_WET, SOIL_RAW_DRY);
  int pct = map(raw, SOIL_RAW_WET, SOIL_RAW_DRY, 100, 0); // wet->100, dry->0
  pct = constrain(pct, 0, 100);
  return pct;
}

String jsonStatus() {
  String s = "{";
  s += "\"temp\":" + String(lastTemp, 1) + ",";
  s += "\"tempValid\":" + String(lastTempValid ? 1 : 0) + ",";
  s += "\"soil\":" + String(lastSoilPercent) + ",";
  s += "\"pump\":" + String(pumpState ? 1 : 0) + ",";
  s += "\"auto\":" + String(autoMode ? 1 : 0);
  s += "}";
  return s;
}

// Websocket event
void onWsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg;
    msg.reserve(length);
    for (size_t i = 0; i < length; i++) {
      msg += static_cast<char>(payload[i]);
    }
    if (msg == "manual_mode") {
      autoMode = false;
    } else if (msg == "auto_mode" || msg == "auto_on") {
      autoMode = true;
    } else if (msg == "pump_on" && !autoMode) {
      setPump(true);
    } else if (msg == "pump_off" && !autoMode) {
      setPump(false);
    }
    // Reply current status
   String status = jsonStatus();
   webSocket.broadcastTXT(status);
  }
}

// Serve web UI
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Auto Watering</title>
  <style>body{font-family:Arial;max-width:420px;margin:8px;} .big{font-size:1.4em}</style>
</head>
<body>
  <h2>Auto Watering</h2>
  <div class="big">Temperature: <span id="temp">--</span> °C</div>
  <div class="big">Soil: <span id="soil">--</span> %</div>
  <div>Status: <span id="status">--</span></div>
  <hr>
  <button onclick="send('manual_mode')">Manual Mode</button>
  <button onclick="send('auto_mode')">Auto Mode</button>
  <button onclick="send('pump_on')">Pump ON</button>
  <button onclick="send('pump_off')">Pump OFF</button>

  <script>
    let ws;
    function init(){
      const host = location.hostname;
      ws = new WebSocket('ws://'+host+':81/');
      ws.onopen = ()=>{ console.log('ws open'); };
      ws.onmessage = (evt)=>{ try{ const j=JSON.parse(evt.data); document.getElementById('temp').innerText=j.tempValid?j.temp:'--'; document.getElementById('soil').innerText=j.soil>=0?j.soil:'--'; document.getElementById('status').innerText='Mode: '+(j.auto?'Auto':'Manual')+' | Pump: '+(j.auto?'Auto':(j.pump?'ON':'OFF')); }catch(e){} };
      ws.onclose = ()=>{ setTimeout(init,2000); };
    }
    function send(cmd){ if(ws && ws.readyState===1) ws.send(cmd); }
    window.onload = init;
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  // pins
  pinMode(PUMP_GATE_PIN, OUTPUT);
  setPump(false);
  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(SW3_PIN, INPUT_PULLUP);
  sw1WasPressed = digitalRead(SW1_PIN) == LOW;
  sw2WasPressed = digitalRead(SW2_PIN) == LOW;
  sw3WasPressed = digitalRead(SW3_PIN) == LOW;

  Wire.begin(21, 22);
  uint8_t oledAddress = 0;
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) {
    oledAddress = 0x3C;
  } else {
    Wire.beginTransmission(0x3D);
    if (Wire.endTransmission() == 0) {
      oledAddress = 0x3D;
    }
  }
  if (oledAddress != 0 && display.begin(SSD1306_SWITCHCAPVCC, oledAddress)) {
    displayReady = true;
    Serial.printf("OLED found at 0x%02X\n", oledAddress);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Plant Watering");
    display.println("Starting...");
    display.display();
  } else {
    Serial.println("OLED not found at 0x3C or 0x3D");
  }

  dht.begin();

  // WiFi connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nCannot connect, start AP");
    WiFi.softAP("AutoWater_AP");
    Serial.print("AP IP:");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.print("\nConnected. IP: ");
    Serial.println(WiFi.localIP());
  }

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWsEvent);

  // initial sensor read
  lastSensorMillis = 0;
}

void loop() {
  server.handleClient();
  webSocket.loop();
  handleSerialPumpCommands();

  unsigned long now = millis();

  // Handle debounced mode and pump press edges (active LOW).
  bool sw1Pressed = digitalRead(SW1_PIN) == LOW;
  bool sw2Pressed = digitalRead(SW2_PIN) == LOW;
  bool sw3Pressed = digitalRead(SW3_PIN) == LOW;
  if (sw2Pressed && !sw2WasPressed && now - lastSw2 > debounce) {
    lastSw2 = now;
    if (!autoMode) {
      setPump(!pumpState);
      Serial.printf("SW2: Manual pump %s\n", pumpState ? "ON" : "OFF");
    } else {
      Serial.println("SW2: ignored in Auto mode");
    }
  }
  if (sw3Pressed && !sw3WasPressed && now - lastSw3 > debounce) {
    lastSw3 = now;
    autoMode = false;
    Serial.println("SW3: Manual mode");
  }
  if (sw1Pressed && !sw1WasPressed && now - lastSw1 > debounce) {
    lastSw1 = now;
    autoMode = true;
    Serial.println("SW1: Auto mode");
  }
  sw1WasPressed = sw1Pressed;
  sw2WasPressed = sw2Pressed;
  sw3WasPressed = sw3Pressed;

  if (now - lastSensorMillis >= sensorInterval) {
    lastSensorMillis = now;
    // read sensors
    int soilRaw = readSoilRaw();
    int soil = soilPercentFromRaw(soilRaw);
    float temp = dht.readTemperature();
    lastTempValid = !isnan(temp);
    if (lastTempValid) lastTemp = temp;
    if (soil >= 0) {
      lastGoodSoilPercent = soil;
      lastSoilPercent = soil;
      consecutiveSoilFailures = 0;
    } else {
      if (consecutiveSoilFailures < soilFailureLimit) {
        consecutiveSoilFailures++;
      }
      if (consecutiveSoilFailures >= soilFailureLimit) {
        lastSoilPercent = -1;
      } else if (lastGoodSoilPercent >= 0) {
        lastSoilPercent = lastGoodSoilPercent;
      }
    }
    Serial.printf("Soil raw=%d soil=%d%%, ", soilRaw, soil);
    if (lastTempValid) {
      Serial.printf("DHT22=%.1f C\n", lastTemp);
    } else {
      Serial.println("DHT22 read failed (check GPIO4, power, and GND)");
    }
    if (soilRaw == 0) Serial.println("Check H1 AOUT signal and selected ADC pin");

    // In Auto mode, pump only when the sensor reports dry soil.
    if (autoMode) {
      setPump(soil >= 0 && soil <= THRESHOLD_DRY);
    }

    if (displayReady) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("T:");
      if (lastTempValid) {
        display.print(lastTemp, 1); display.print("C ");
      } else {
        display.print("-- ");
      }
      display.print("Mode:");
      display.println(autoMode ? "Auto" : "Manual");
      display.print("Soil:");
      if (lastSoilPercent >= 0) display.print(lastSoilPercent);
      else display.print("--");
      display.print("% Pump:");
      if (autoMode) display.println("Auto");
      else display.println(pumpState ? "ON" : "OFF");
      display.display();
    }

    // Broadcast over websocket
    String status = jsonStatus();
    webSocket.broadcastTXT(status);
  }
}
