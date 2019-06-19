// #include <TimeLib.h>

// char isotimebuf[25];
// #define NowAsIsoTimeString ((snprintf(isotimebuf, 30, "%04d-%02d-%02dT%02d:%02d:%02dZ", year(), month(), day(), hour(), minute(), second())), isotimebuf)
#define NowAsIsoTimeString "now"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <BlynkSimpleEsp8266_SSL.h>

#include <esp8266_peri.h>

// interval in seconds
#define avgInterval 15
#define gasInterval 600
#define metersInterval 3600

#include "secrets.h"

char espHostname[] = HOSTNAME;
char ssid[] = WIFI_SSID;
char password[] = WIFI_PASSWD;
char auth[] = BLYNK_AUTH;

// Configuration for NTP
const char* ntp_primary = "ntp.caiway.nl";
const char* ntp_secondary = "pool.ntp.org";

long mEVLT = 0; // consumption low tariff (0,001kWh)
long mEVHT = 0; // consumption high tariff (0,001kWh)
long mEPLT = 0; // production low tariff (0,001kWh)
long mEPHT = 0; // production high tariff (0,001kWh)
long mEAV = 0;  // actual consumption (0,001kW)
long mEAP = 0;  // actual production (0,001kW)
long mCT = 0;   // actual tariff (1/2)
long mGVT = 0;  // m-bus reading gas (0,001m3)
long mWVT = 0;  // m-bus reading water (0,001m3)
long oEVLT = 0; // consumption low tariff (0,001kWh)
long oEVHT = 0; // consumption high tariff (0,001kWh)
long oEPLT = 0; // production low tariff (0,001kWh)
long oEPHT = 0; // production high tariff (0,001kWh)
long oGVT = 0;  // m-bus reading gas (0,001m3)
long oWVT = 0;  // m-bus reading water (0,001m3)
uint16_t oCrc = 0;
uint16_t iCrc = 0;

boolean newData = false;

int wRSSI = 0;  // The RSSI indicator, scaled from 0 (worse) to 4 (best)

String datagram = "";
String crc = "";
String debugme = "";
String httpdebug = "";
String chipid = "";

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

String tmpTime;

long avgPwr[avgInterval];  // calculating average
short avgCnt = 0;  // items counter
long mi, av, ma;
struct {
  short mCNT;
  long mMIN, mAVG, mMAX;
} avgRec;

void processAverage() {
  av = 0;
  mi = 2147483646L;
  ma = -2147483646L;
  for(int i=0; i < avgCnt; i++) {
    av = av + avgPwr[i];
    if(avgPwr[i] > ma) {
      ma = avgPwr[i];
    }
    if(avgPwr[i] < mi) {
      mi = avgPwr[i];
    }
    avgPwr[i] = 0;
  }
  if(avgCnt > 0) {
    av = av / avgCnt;
  } else {
    av = 0;
  }
  avgRec.mCNT = avgCnt;
  avgCnt = 0;
  avgRec.mMIN = mi;
  avgRec.mAVG = av;
  avgRec.mMAX = ma;
}

BlynkTimer timerAvg;
void avgTimer()
{
  processAverage();
  Blynk.virtualWrite(V1, avgRec.mAVG);
}

BlynkTimer timerMeters;
void metersTimer()
{
  Blynk.virtualWrite(V2, mEVLT);
  Blynk.virtualWrite(V3, mEVHT);
  Blynk.virtualWrite(V4, mEVLT + mEVHT);
  Blynk.virtualWrite(V5, mGVT);
}

String gasTime;
long mLastGVT = 0;
BlynkTimer timerGas;
void gasTimer()
{
  if ( mLastGVT > 0 )
  {
    Blynk.virtualWrite(V6, mGVT - mLastGVT);
  }
  mLastGVT = mGVT;
}

char message[500];
String jsonTemplate = "{\r\n"
  "  \"stroom\": {\r\n"
  "    \"tijdstip\": \"%s\",\r\n"
  "    \"tarief\": \"%s\",\r\n"
  "    \"actueel%02ds\": {\r\n"
  "      \"gemiddeld\": %ld,\r\n"
  "      \"minimum\": %ld,\r\n"
  "      \"maximum\": %ld\r\n"
  "      \"#metingen\": %d\r\n"
  "    },\r\n"
  "    \"meterstand\": {\r\n"
  "      \"hoog\": %ld,\r\n"
  "      \"laag\": %ld\r\n"
  "    }\r\n"
  "  },\r\n"
  "  \"gas\": {\r\n"
  "    \"tijdstip\": \"%s\",\r\n"
  "    \"meterstand\": %ld\r\n"
  "  }\r\n"
  "}\r\n";

void handleJson() {
  snprintf(message, 500, jsonTemplate.c_str(), tmpTime.c_str(),
    mCT == 2 ? "hoog" : "laag", avgInterval,
    avgRec.mAVG, avgRec.mMIN, avgRec.mMAX, avgRec.mCNT,
    mEVLT, mEVHT, gasTime.c_str(), mGVT);
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

void handleDebug() {
  String message = "Server up and running\r\n";
  message += "ResetReason: " + String(ESP.getResetReason()) + "\r\n";
  message += "ms since on: " + String(millis()) + "\r\n\n";
  message += datagram;
  message += "\n\r\n\r-------- CRC --------\n\r";
  message += "Message CRC: " + crc;
  message += "\r\nBerekende CRC: " + String(oCrc, HEX);
  message += "\n\r-------- Debug Data ----------\n\r";
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

void setup(void) {
  Serial.begin(115200);

  // Invert UART0 RXD pin using direct peripheral register write
  USC0(0) |= 1UL << UCRXI; 

  twi_setClock(400000);
  
  WiFi.hostname(espHostname);
  WiFi.mode(WIFI_STA);
  Blynk.begin(auth, ssid, password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

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
  // setTime(time(nullptr));
  
  httpUpdater.setup(&server);
  server.on("/", handleJson);
  server.on("/debug", handleDebug);
  server.on("/reset", handleReset);
  server.onNotFound(handleJson);

  server.begin();
  Serial.println("");
  Serial.print("HTTP server started @ ");
  Serial.println(NowAsIsoTimeString);

  timerAvg.setInterval(1000L * avgInterval, avgTimer);
  timerGas.setInterval(1000L * gasInterval, gasTimer);
  timerMeters.setInterval(1000L * metersInterval, metersTimer);
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

bool checkCRC() {
  iCrc = strtol(crc.c_str(), NULL, 16);
  return iCrc == oCrc;
}


void decodeDatagram() {
  int x, y;
  String t;
  long tmpVal;

  // time from meter
  x = datagram.indexOf("0-0:1.0.0(");
  if(x >= 0) {
    y = datagram.indexOf(")", x);
    if(y > 0 && y < x + 24) {
      t = datagram.substring(x + 10, y);
      tmpTime = "20" + t.substring(0, 2) + "-" + t.substring(2, 4) + "-" + t.substring(4, 6) + "T" + t.substring(6, 8) + ":" + t.substring(8, 10) + ":" + t.substring(10, 12);
      if(t.indexOf("S") > 0) {
        tmpTime += "+2:00";
      } else {
        tmpTime += "+1:00";
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
      if(tmpVal >= oEVLT && tmpVal >= 0) {
        oEVLT = mEVLT;
        mEVLT = tmpVal;
        newData = true;
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
      if(tmpVal >= oEVHT && tmpVal >= 0) {
        oEVHT = mEVHT;
        mEVHT = tmpVal;
        newData = true;
      }
    }
  }
/*
  // production low tariff
  x = datagram.indexOf("1-0:2.8.1(");
  if(x >= 0) {
    y = datagram.indexOf("*kWh)", x);
    if(y > 0 && y < x + 22) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= oEPLT && tmpVal >= 0) {
        oEPLT = mEPLT;
        mEPLT = tmpVal;
        newData = true;
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
      if(tmpVal >= oEPHT && tmpVal >= 0) {
        oEPHT = mEPHT;
        mEPHT = tmpVal;
        newData = true;
      }
    }
  }
*/
  // actual consumption
  x = datagram.indexOf("1-0:1.7.0(");
  if(x >= 0) {
    y = datagram.indexOf("*kW)", x);
    if(y > 0 && y < x + 18) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEAV = tmpVal;
        newData = true;
      }
    }
  }
/*
  // actual production
  x = datagram.indexOf("1-0:2.7.0(");
  if(x >= 0) {
    y = datagram.indexOf("*kW)", x);
    if(y > 0 && y < x + 18) {
      t = datagram.substring(x + 10, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= 0) {
        mEAP = tmpVal;
        newData = true;
      }
    }
  }
*/
  // actual tariff
  x = datagram.indexOf("0-0:96.14.0(");
  if(x >= 0) {
    y = datagram.indexOf(")", x);
    if(y > 0 && y < x + 18) {
      t = datagram.substring(x + 12, y);
      mCT = (long) (t.toInt());
      newData = true;
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
        gasTime += "+2:00";
      } else {
        gasTime += "+1:00";
      }
    }

    x = datagram.indexOf(")(", x);
    y = datagram.indexOf("*m3)", x);
    if(y > 0 && y < x + 13) {
      t = datagram.substring(x + 2, y);
      tmpVal = (long) (t.toFloat() * 1000.0);
      if(tmpVal >= oGVT && tmpVal >= 0) {
        oGVT = mGVT;
        mGVT = tmpVal;
        newData = true;
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
      if(tmpVal >= oWVT && tmpVal >= 0) {
        oWVT = mWVT;
        mWVT = tmpVal;
        newData = true;
      }
    }
  }
*/
  if( avgCnt < avgInterval ) {
    avgPwr[avgCnt] = mEAV - mEAP;
    avgCnt++;
  }
}

// 0 = waiting for /
// 1 = waiting for !
// 2 = waiting for \r
uint8_t state = 0;

void loop(void) {
  server.handleClient();
  Blynk.run();
  timerAvg.run(); // Stuur iedere 15s gemiddeld verbruik
  timerGas.run(); // Stuur iedere 10m gasverbruik
  timerMeters.run(); // Stuur ieder uur de meterstanden

  while (Serial.available()) {
    unsigned char c = Serial.read();
    if(c == '/') {
      datagram = "";
      oCrc = 0;
      newData = false;
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
          // Process datagram
          decodeDatagram();
        }
        state = 0;
      }
      else
        crc += String((char) c);
    }
  }
}
