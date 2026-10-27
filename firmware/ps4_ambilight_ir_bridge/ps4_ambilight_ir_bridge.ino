// PS4 Ambilight IR Bridge  -  UNTESTED
// ESP32 that receives DDP packets (what the PS4-Ambilight plugin sends to a
// WLED controller), averages the colour, and "presses" the nearest colour key
// on a 24-key IR remote strip controller using an IR LED.
//
// Wiring: GPIO 4 -> 100 ohm resistor -> IR LED anode (long leg); cathode -> GND.

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include "config.h"   // copy config.example.h to config.h first

const uint8_t IR_PIN = 4;

IRsend irsend(IR_PIN);
WiFiUDP udp;
WebServer server(80);
Preferences prefs;

// NEC codes of the standard 24-key remote (captured from the author's remote).
const uint32_t IR_ON  = 0xF7C03F;
const uint32_t IR_OFF = 0xF740BF;

struct Key { const char* name; uint32_t code; uint8_t r, g, b; };
const Key keys[] = {
  {"Red",          0xF720DF, 255,   0,   0},
  {"Green",        0xF7A05F,   0, 255,   0},
  {"Blue",         0xF7609F,   0,   0, 255},
  {"White",        0xF7E01F, 255, 255, 255},
  {"Orange",       0xF730CF, 255,  80,   0},
  {"Light orange", 0xF708F7, 255, 140,   0},
  {"Yellow",       0xF728D7, 255, 255,   0},
  {"Teal",         0xF7B04F,   0, 200, 200},
  {"Light blue",   0xF78877,   0, 120, 255},
  {"Purple",       0xF7708F, 120,   0, 160},
  {"Violet",       0xF748B7, 170,   0, 170},
  {"Pink",         0xF76897, 255,   0, 128},
};
const int NKEYS = sizeof(keys) / sizeof(keys[0]);

// Settings (saved in flash)
bool enabled = true;
int minInterval = 400;   // ms between IR sends
int offThreshold = 25;   // below this brightness -> strip off

// State
int lastR = 0, lastG = 0, lastB = 0;
String lastKey = "-";
uint32_t current = 0, lastSend = 0, lastPacket = 0, packets = 0;

int nearestKey(int r, int g, int b) {
  float k = 255.0f / max(1, max(r, max(g, b)));
  float rr = r * k, gg = g * k, bb = b * k;
  int best = 0; float bd = 1e9;
  for (int i = 0; i < NKEYS; i++) {
    float m = max(keys[i].r, max(keys[i].g, keys[i].b));
    float c = 255.0f / m;
    float dr = rr - keys[i].r * c, dg = gg - keys[i].g * c, db = bb - keys[i].b * c;
    float d = dr*dr + dg*dg + db*db;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

void sendIR(uint32_t code) { irsend.sendNEC(code, 32); }

String page() {
  String s = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>PS4 Ambilight</title><style>"
    "body{font-family:sans-serif;max-width:480px;margin:auto;padding:12px}"
    "#sw{height:60px;border-radius:8px;border:1px solid #888;margin:8px 0}"
    "button,input[type=submit]{padding:8px 12px;margin:3px;font-size:15px}"
    "label{display:block;margin:8px 0}</style></head><body>"
    "<h2>PS4 Ambilight bridge</h2><div id=sw></div>"
    "<div id=st>loading...</div>"
    "<h3>Settings</h3><form action=/save>");
  s += "<label><input type=checkbox name=en value=1 ";
  if (enabled) s += "checked";
  s += "> Enabled</label>";
  s += "<label>Min ms between IR sends <input type=number name=iv value=" + String(minInterval) + " min=100 max=5000></label>";
  s += "<label>Off below brightness (0-255) <input type=number name=th value=" + String(offThreshold) + " min=0 max=255></label>";
  s += F("<input type=submit value=Save></form><h3>Test IR</h3>"
         "<a href='/send?c=on'><button>On</button></a><a href='/send?c=off'><button>Off</button></a><br>");
  for (int i = 0; i < NKEYS; i++)
    s += "<a href='/send?i=" + String(i) + "'><button>" + keys[i].name + "</button></a>";
  s += F("<script>"
    "async function u(){try{let d=await (await fetch('/status')).json();"
    "document.getElementById('sw').style.background='rgb('+d.r+','+d.g+','+d.b+')';"
    "document.getElementById('st').innerHTML='RGB: '+d.r+','+d.g+','+d.b+'<br>Key: '+d.key+"
    "'<br>Packets: '+d.p+'<br>Last packet: '+(d.age<0?'never':d.age+' ms ago')+'<br>IP: '+d.ip;}catch(e){}}"
    "u();setInterval(u,1000);</script></body></html>");
  return s;
}

void handleStatus() {
  String j = "{\"r\":" + String(lastR) + ",\"g\":" + String(lastG) + ",\"b\":" + String(lastB) +
             ",\"key\":\"" + lastKey + "\",\"p\":" + String(packets) +
             ",\"age\":" + String(packets ? (long)(millis() - lastPacket) : -1) +
             ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", j);
}

void handleSave() {
  enabled = server.hasArg("en");
  minInterval = constrain(server.arg("iv").toInt(), 100, 5000);
  offThreshold = constrain(server.arg("th").toInt(), 0, 255);
  prefs.putBool("en", enabled);
  prefs.putInt("iv", minInterval);
  prefs.putInt("th", offThreshold);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSend() {
  if (server.hasArg("c")) {
    sendIR(server.arg("c") == "on" ? IR_ON : IR_OFF);
  } else if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NKEYS) { sendIR(IR_ON); delay(200); sendIR(keys[i].code); }
  }
  current = 0;  // force resync after a manual test
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  irsend.begin();
  prefs.begin("ambi", false);
  enabled = prefs.getBool("en", true);
  minInterval = prefs.getInt("iv", 400);
  offThreshold = prefs.getInt("th", 25);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    MDNS.begin("ps4ambilight");
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("PS4-Ambilight");
    Serial.println("Fallback hotspot PS4-Ambilight, page at 192.168.4.1");
  }

  server.on("/", []() { server.send(200, "text/html", page()); });
  server.on("/status", handleStatus);
  server.on("/save", handleSave);
  server.on("/send", handleSend);
  server.begin();
  udp.begin(4048);   // DDP port used by the PS4-Ambilight plugin / WLED
}

void loop() {
  server.handleClient();

  int n = udp.parsePacket();
  if (n < 13) return;
  static uint8_t buf[1500];
  int len = udp.read(buf, sizeof(buf));
  int off = (buf[0] & 0x10) ? 14 : 10;   // DDP header size (timecode flag)
  uint32_t sr = 0, sg = 0, sb = 0; int px = 0;
  for (int i = off; i + 2 < len; i += 3) {
    sr += buf[i]; sg += buf[i+1]; sb += buf[i+2]; px++;
  }
  if (!px) return;
  lastR = sr / px; lastG = sg / px; lastB = sb / px;
  lastPacket = millis(); packets++;
  if (!enabled) return;
  if (millis() - lastSend < (uint32_t)minInterval) return;

  uint32_t want; String name;
  if (max(lastR, max(lastG, lastB)) < offThreshold) {
    want = IR_OFF; name = "Off";
  } else {
    int k = nearestKey(lastR, lastG, lastB);
    want = keys[k].code; name = keys[k].name;
  }
  lastKey = name;
  if (want == current) return;
  if (want != IR_OFF && (current == IR_OFF || current == 0)) {
    sendIR(IR_ON); delay(200);
  }
  sendIR(want);
  current = want; lastSend = millis();
}
