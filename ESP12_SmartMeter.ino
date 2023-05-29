#include <TimeLib.h>

char isotimebuf[25];
#define NowAsIsoTimeString ((snprintf(isotimebuf, 30, "%04d-%02d-%02dT%02d:%02d:%02dZ", year(), month(), day(), hour(), minute(), second())), isotimebuf)

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <BlynkSimpleEsp8266.h>
#include <AsyncMqtt_Generic.h>
#include <esp8266_peri.h>

#include "secrets.h"

#define BLYNK_NO_BUILTIN
#define BLYNK_NO_FLOAT

char espHostname[] = HOSTNAME;
char ssid[] = WIFI_SSID;
char password[] = WIFI_PASSWD;

// Configuration for NTP
const char* ntp_primary = "ntp.caiway.nl";
const char* ntp_secondary = "pool.ntp.org";

// Topic on MQTT broker
#define MQTT_TOPIC "thuis/meterkast/actuals"

long mEVLT = 0; // consumption low tariff (0,001kWh)
long mEVHT = 0; // consumption high tariff (0,001kWh)
long mEPLT = 0; // production low tariff (0,001kWh)
long mEPHT = 0; // production high tariff (0,001kWh)
long mEAV = 0;  // actual consumption (0,001kW)
long mEAP = 0;  // actual production (0,001kW)
long mCT = 0;   // actual tariff (1/2)
long mGVT = 0;  // m-bus reading gas (0,001m3)
long mWVT = 0;  // m-bus reading water (0,001m3)
uint16_t oCrc = 0;
uint16_t iCrc = 0;

int wRSSI = 0;  // The RSSI indicator, scaled from 0 (worse) to 4 (best)

String datagram = "";
String crc = "";
String debugme = "";
String httpdebug = "";
String chipid = "";

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
AsyncMqttClient mqttClient;
BlynkTimer connectTimer;

String tmpTime;
String gasTime;

static bool mqttConnected = false;
void onMqttConnect(bool sessionPresent)
{
  mqttConnected = true;
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  mqttConnected = false;
}

void connectTimerEvent()
{
  if ( !mqttConnected && WiFi.isConnected() )
  {
    mqttClient.connect();
    mqttConnected = mqttClient.connected();
  }
}

void setup(void) {
  Serial.begin(115200);

  // Invert UART0 RXD pin using direct peripheral register write
  USC0(0) |= 1UL << UCRXI; 

  twi_setClock(400000);
  
  Serial.println("");

  WiFi.hostname(espHostname);
  WiFi.mode(WIFI_AP_STA);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Setup NTP client
  configTime(0, 0, ntp_primary, ntp_secondary);
  Serial.print("Waiting on time sync.");
  while (time(nullptr) < 1510644967) {
    delay(10);
    Serial.print(".");
  }
  setTime(time(nullptr));
  
  httpUpdater.setup(&server);
  server.on("/", handleJson);
  server.on("/debug", handleDebug);
  server.on("/reset", handleReset);
  server.onNotFound(handleJson);

  server.begin();
  Serial.println("");
  Serial.print("HTTP server started @ ");
  Serial.println(NowAsIsoTimeString);


  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWD);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  connectTimer.setInterval(30000L, connectTimerEvent);
}

// CRC-16-IBM calculation
#define POLY 0xA001
uint16_t crc16_update(uint16_t crc16, unsigned char c)
{
    crc16 ^= c;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    crc16 = crc16 & 1 ? (crc16 >> 1) ^ POLY : crc16 >> 1;
    return crc16;
}

char message[500];

// 0 = reading P1
// 1 = decode datagram
// 2 = publish to blynk
// 3 = compose json
// 4 = publish to mqtt
uint8_t phase = 0;

// Returns phase
uint8_t readSerial(unsigned long startOfLoop)
{
  // 0 = waiting for /
  // 1 = waiting for !
  // 2 = waiting for \r
  static uint8_t state = 0;
  uint8_t retphase = 0;

  // Max looptime is 15 milliseconds.
  // Stay on the safe side.
  while ( ((millis() - startOfLoop) <= 14) && Serial.available() ) {
    unsigned char c = Serial.read();
    if(c == '/') {
      datagram = "";
      oCrc = 0;
      state = 1;
    }
    switch ( state ) {
    case 1:
      if(c == '!') {
        crc = "";
        state = 2;
      }
      datagram += String((char) c);
      oCrc = crc16_update(oCrc, c);
      break;
    case 2:
      if(c == '\r') {
        if ( checkCRC() )
        {
          retphase = 1;
        }
        state = 0;
      }
      else
        crc += String((char) c);
    }
  }
  return retphase;
}

void loop(void) {
  unsigned long  startofloop = millis();
  connectTimer.run();
  server.handleClient();
  Blynk.run();

  if ((millis() - startofloop) <= 14)
  {
    static char buf[25];
    switch (phase) {
      case 0:
          phase = readSerial(startofloop);
          break;
      case 1:
          // Process datagram
          decodeDatagram();
          phase = 2;
          break;
      case 2:
          Blynk.virtualWrite(V0, mEAP - mEAV);
          phase = 3;
          break;
      case 3:
          composeJson();
          phase = 4;
          break;
      case 4:
          if ( mqttConnected )
          {
            mqttClient.publish(MQTT_TOPIC, 0, true, message);
          }
          phase = 0;
          break;
    }
  }
}

void handleDebug() {
  String message = "Server up and running\r\n";
  message += "ResetReason: " + String(ESP.getResetReason()) + "\r\n";
  message += "ms since on: " + String(millis()) + "\r\n\n";
  message += datagram;
  message += "\n\r\n\r-------- CRC --------\n\r";
  message += "Message CRC: " + crc;
  message += "\r\nBerekende CRC: " + String(oCrc, HEX);
  message += "\n\r-------- Debug Data ----------\n\r";
  message += "mqttConnected: " + String(mqttConnected) + "\n\r";
  message += debugme;
  message += "\n\r-------- Network Info --------\n\r";
  message += "Time: ";
  message += NowAsIsoTimeString;
  message += "\r\nMAC Addr: ";
  message += String(WiFi.macAddress());
  checkRSSI();
  message += "\r\nRSSI: ";
  message += String(WiFi.RSSI());
  message += "dBm (Indicator ";
  message += String(wRSSI);
  message += ")\r\nChip info: 0x";
  message += String(ESP.getChipId(), HEX);
  message += "\n\r\n\r-------- HTTP Data -----------\n\r";
  message += httpdebug;
  server.send(200, "text/plain", message);
}


void handleReset() {
  String message = "Resetting in 5 seconds...\n\r";
  server.send(200, "text/plain", message);
  delay(5000);
  ESP.restart();
}

String jsonTemplate = "{\r\n"
  "  \"stroom\": {\r\n"
  "    \"tijdstip\": \"%s\",\r\n"
  "    \"tarief\": \"%s\",\r\n"
  "    \"consumptie\": {\r\n"
  "      \"nu\": %ld,\r\n"
  "      \"meterstand\": {\r\n"
  "        \"hoog\": %ld,\r\n"
  "        \"laag\": %ld\r\n"
  "      }\r\n"
  "    },\r\n"
  "    \"productie\": {\r\n"
  "      \"nu\": %ld,\r\n"
  "      \"meterstand\": {\r\n"
  "        \"hoog\": %ld,\r\n"
  "        \"laag\": %ld\r\n"
  "      }\r\n"
  "    }\r\n"
  "  },\r\n"
  "  \"gas\": {\r\n"
  "    \"tijdstip\": \"%s\",\r\n"
  "    \"meterstand\": %ld\r\n"
  "  }\r\n"
  "}\r\n";

void composeJson() {
  snprintf(message, 500, jsonTemplate.c_str(), tmpTime.c_str(),
    mCT == 2 ? "hoog" : "laag",
    mEAV, mEVHT, mEVLT, mEAP, mEPHT, mEPLT, gasTime.c_str(), mGVT);
}

void handleJson() {
  composeJson();
  server.send(200, "application/json", message);
}

void checkRSSI() {
  int x = WiFi.RSSI();
  wRSSI = 0;
  if(x < -30) { // -35dBm is over saturated signal
    if(x > -55) {
      wRSSI = 4; // Best quality connection
    } else if(x > -65) {
      wRSSI = 3; // Usable connection
    } else if(x > -72) {
      wRSSI = 2; // Poor connection, high latencies and drops
    } else if(x > -80) {
      wRSSI = 1; // Worst connection, better not use this one
    }
  }
}

bool checkCRC() {
  iCrc = strtol(crc.c_str(), NULL, 16);
  return iCrc == oCrc;
}

void decodeDatagram() {
  int x, y;
  String t;
  char inChar;
  long tmpVal;

  // time from meter
  x = datagram.indexOf("0-0:1.0.0(");
  if(x >= 0) {
    y = datagram.indexOf(")", x);
    if(y > 0 && y < x + 24) {
      t = datagram.substring(x + 10, y);
      tmpTime = "20" + t.substring(0, 2) + "-" + t.substring(2, 4) + "-" + t.substring(4, 6) + "T" + t.substring(6, 8) + ":" + t.substring(8, 10) + ":" + t.substring(10, 12);
      if(t.indexOf("S") > 0) {
        tmpTime += "+02:00";
      } else {
        tmpTime += "+01:00";
      }
    }
  }

  // consumption low tariff
  x = datagram.indexOf("1-0:1.8.1(");
  if(x >= 0) {
    y = datagram.indexOf("*kWh)", x);
    if(y > 0 && y < x + 22) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEVLT = tmpVal;
      }
    }
  }

  // consumption high tariff
  x = datagram.indexOf("1-0:1.8.2(");
  if(x >= 0) {
    y = datagram.indexOf("*kWh)", x);
    if(y > 0 && y < x + 22) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEVHT = tmpVal;
      }
    }
  }

  // production low tariff
  x = datagram.indexOf("1-0:2.8.1(");
  if(x >= 0) {
    y = datagram.indexOf("*kWh)", x);
    if(y > 0 && y < x + 22) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEPLT = tmpVal;
      }
    }
  }

  // production high tariff
  x = datagram.indexOf("1-0:2.8.2(");
  if(x >= 0) {
    y = datagram.indexOf("*kWh)", x);
    if(y > 0 && y < x + 22) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEPHT = tmpVal;
      }
    }
  }

  // actual consumption
  x = datagram.indexOf("1-0:1.7.0(");
  if(x >= 0) {
    y = datagram.indexOf("*kW)", x);
    if(y > 0 && y < x + 18) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEAV = tmpVal;
      }
    }
  }

  // actual production
  x = datagram.indexOf("1-0:2.7.0(");
  if(x >= 0) {
    y = datagram.indexOf("*kW)", x);
    if(y > 0 && y < x + 18) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEAP = tmpVal;
      }
    }
  }

  // actual tariff
  x = datagram.indexOf("0-0:96.14.0(");
  if(x >= 0) {
    y = datagram.indexOf(")", x);
    if(y > 0 && y < x + 18) {
      t = datagram.substring(x + 12, y);
      mCT = (long) (t.toInt());
    }
  }
  
  // actual gas consumption including timestamp
  x = datagram.indexOf("0-1:24.2.1(");
  if(x >= 0) {
    y = datagram.indexOf(")", x);
    if(y > 0 && y < x + 25) {
      t = datagram.substring(x + 11, y);
      gasTime = "20" + t.substring(0, 2) + "-" + t.substring(2, 4) + "-" + t.substring(4, 6) + "T" + t.substring(6, 8) + ":" + t.substring(8, 10) + ":" + t.substring(10, 12);
      if(t.indexOf("S") > 0) {
        gasTime += "+02:00";
      } else {
        gasTime += "+01:00";
      }
    }

    x = datagram.indexOf(")(", x);
    y = datagram.indexOf("*m3)", x);
    if(y > 0 && y < x + 13) {
      t = datagram.substring(x + 2, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mGVT = tmpVal;
      }
    }
  }
/*
  // actual water consumption
  x = datagram.indexOf("0-2:96.1.0(");
  if(x >= 0) {
    x = datagram.indexOf(")(", x);
    y = datagram.indexOf("*m3)", x);
    if(y > 0 && y < x + 13) {
      t = datagram.substring(x + 2, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mWVT = tmpVal;
      }
    }
  }
*/
}
