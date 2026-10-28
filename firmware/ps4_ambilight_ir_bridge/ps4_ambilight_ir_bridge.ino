// PS4 Ambilight IR Bridge
// ESP32 that receives DDP packets (what the PS4-Ambilight plugin sends to a
// WLED controller), picks the most vibrant colour on screen, smooths it, and
// "presses" the nearest colour key on a 24-key IR remote strip controller.

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include "config.h"   // copy config.example.h to config.h first

// ===================== EDIT ME =====================
// GPIO connected to the IR emitter's signal pin. This is only the DEFAULT:
// the web page can change it (saved in flash, then the ESP32 restarts), and a
// value saved from the web page overrides this one.
#define DEFAULT_IR_PIN 4
// ===================================================

IRsend* irsend = nullptr;
int irPin = DEFAULT_IR_PIN;
WiFiUDP udp;
WebServer server(80);
Preferences prefs;

// NEC codes of the standard 24-key remote.
const uint32_t IR_ON  = 0xF7C03F;
const uint32_t IR_OFF = 0xF740BF;

struct Key { const char* name; uint32_t code; uint8_t r, g, b; };
const Key keys[] = {
  {"Red",          0xF720DF, 255,   0,   0},
  {"Green",        0xF7A05F,   0, 255,   0},
  {"Blue",         0xF7609F,   0,   0, 255},
  {"White",        0xF7E01F, 255, 255, 255},   // index 3: used for low-saturation scenes
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
const int WHITE_IDX = 3;
float keyHue[NKEYS];
int ring[NKEYS], ringLen = 0, ringPos[NKEYS];   // coloured keys sorted by hue

const int TGT_NONE = -1, TGT_OFF = -2;

// ---- Settings (saved in flash) ----
bool enabled = true;
int mode = 2;            // 0 plain average, 1 vivid average, 2 dominant vibrant colour
int whiteBelow = 15;     // saturation (%) below this -> White key
int offThreshold = 25;   // brightness below this -> strip off
int minInterval = 80;    // ms between IR sends (one NEC frame takes ~68 ms)
int smoothMs = 120;      // colour smoothing time constant, 0 = none
int glide = 4;           // walk through neighbouring keys if target is within N steps, 0 = jump

// ---- State ----
float smH = 0, smS = 0, smBr = 0;   // smoothed hue (deg), saturation (0-1), brightness
bool haveSm = false;
uint32_t lastProc = 0;
int dispR = 0, dispG = 0, dispB = 0;
int tgtKey = TGT_NONE;
int curKey = -1;          // key believed to be on the strip, -1 unknown
bool stripOn = false;
uint32_t lastSend = 0, lastPacket = 0, packets = 0, irSent = 0, lastCode = 0;
uint32_t pkRate = 0, irRate = 0, restartAt = 0;

void rgbToHsv(float r, float g, float b, float &h, float &s, float &v) {
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
  float d = mx - mn;
  v = mx / 255.0f;
  s = (mx <= 0) ? 0 : d / mx;
  if (d <= 0) h = 0;
  else if (mx == r) h = fmodf((g - b) / d, 6.0f);
  else if (mx == g) h = (b - r) / d + 2.0f;
  else h = (r - g) / d + 4.0f;
  h *= 60.0f;
  if (h < 0) h += 360.0f;
}

void hsvToRgb(float h, float s, float v, int &r, int &g, int &b) {
  float c = v * s, hp = h / 60.0f;
  float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
  float r1 = 0, g1 = 0, b1 = 0;
  if (hp < 1) { r1 = c; g1 = x; }
  else if (hp < 2) { r1 = x; g1 = c; }
  else if (hp < 3) { g1 = c; b1 = x; }
  else if (hp < 4) { g1 = x; b1 = c; }
  else if (hp < 5) { r1 = x; b1 = c; }
  else { r1 = c; b1 = x; }
  float m = v - c;
  r = (int)((r1 + m) * 255); g = (int)((g1 + m) * 255); b = (int)((b1 + m) * 255);
}

float hueDist(float a, float b) {
  float d = fabsf(a - b);
  return d > 180.0f ? 360.0f - d : d;
}

// Nearest key by hue, with hysteresis so it doesn't flicker on borders.
int pickKey(float h, float s, int prev) {
  float sp = s * 100.0f;
  if (sp < whiteBelow + (prev == WHITE_IDX ? 4 : -4)) return WHITE_IDX;
  int best = 0; float bd = 1e9;
  for (int i = 0; i < NKEYS; i++) {
    if (i == WHITE_IDX) continue;
    float d = hueDist(h, keyHue[i]);
    if (d < bd) { bd = d; best = i; }
  }
  if (prev >= 0 && prev != WHITE_IDX && hueDist(h, keyHue[prev]) <= bd + 10.0f) return prev;
  return best;
}

// Next key to send when moving from cur to tgt (glides through neighbours).
int nextStep(int cur, int tgt) {
  if (cur < 0 || cur == WHITE_IDX || tgt == WHITE_IDX || glide <= 0) return tgt;
  int a = ringPos[cur], b = ringPos[tgt];
  int d = b - a;
  if (d > ringLen / 2) d -= ringLen; else if (d < -ringLen / 2) d += ringLen;
  int ad = d < 0 ? -d : d;
  if (ad <= 1 || ad > glide) return tgt;
  return ring[(a + (d > 0 ? 1 : -1) + ringLen) % ringLen];
}

void sendIR(uint32_t code) {
  irsend->sendNEC(code, 32);
  irSent++;
  lastCode = code;
  lastSend = millis();
}

bool validPin(int p) {
  return p >= 0 && p <= 33 && !(p >= 6 && p <= 11) && p != 1 && p != 3;
}

// ---------------- Frame processing ----------------
const int NB = 24;   // hue bins of 15 degrees

void processFrame(const uint8_t* buf, int len) {
  int off = (buf[0] & 0x10) ? 14 : 10;   // DDP header size (timecode flag)
  uint32_t sr = 0, sg = 0, sb = 0, wr = 0, wg = 0, wb = 0, wt = 0, colorful = 0;
  uint32_t binW[NB] = {0}, binR[NB] = {0}, binG[NB] = {0}, binB[NB] = {0};
  int px = 0;
  for (int i = off; i + 2 < len; i += 3) {
    uint32_t r = buf[i], g = buf[i + 1], b = buf[i + 2];
    sr += r; sg += g; sb += b;
    uint32_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint32_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint32_t c = mx - mn;
    uint32_t w = ((c * c) >> 4) + 1;   // colourful pixels count more
    wr += r * w; wg += g * w; wb += b * w; wt += w;
    if (mx >= 50 && c * 100 >= mx * 35) {   // clearly coloured pixel
      float h, s, v;
      rgbToHsv(r, g, b, h, s, v);
      int bin = ((int)(h / (360.0f / NB))) % NB;
      binW[bin] += w; binR[bin] += r * w; binG[bin] += g * w; binB[bin] += b * w;
      colorful++;
    }
    px++;
  }
  if (!px) return;
  float ar = sr / (float)px, ag = sg / (float)px, ab = sb / (float)px;
  float brt = fmaxf(ar, fmaxf(ag, ab));

  float cr = ar, cg = ag, cb = ab;
  if (mode == 1) {
    cr = wr / (float)wt; cg = wg / (float)wt; cb = wb / (float)wt;
  } else if (mode == 2 && colorful * 100 >= (uint32_t)px * 5) {
    // dominant hue: best bin together with its two neighbours
    int bestBin = 0; uint32_t bestScore = 0;
    for (int i = 0; i < NB; i++) {
      uint32_t sc = binW[(i + NB - 1) % NB] + binW[i] + binW[(i + 1) % NB];
      if (sc > bestScore) { bestScore = sc; bestBin = i; }
    }
    uint32_t tw = 0, tr = 0, tg = 0, tb = 0;
    for (int k = -1; k <= 1; k++) {
      int i = (bestBin + k + NB) % NB;
      tw += binW[i]; tr += binR[i]; tg += binG[i]; tb += binB[i];
    }
    if (tw) { cr = tr / (float)tw; cg = tg / (float)tw; cb = tb / (float)tw; }
  }

  // Smooth in HSV (hue along the shortest arc) so a blue -> yellow change
  // never passes through grey/White. Time based, independent of packet rate.
  float ch, cs, cv;
  rgbToHsv(cr, cg, cb, ch, cs, cv);
  uint32_t now = millis();
  float dt = haveSm ? (float)(now - lastProc) : 0.0f;
  if (dt > 250.0f) dt = 250.0f;
  float a = (!haveSm || smoothMs <= 0) ? 1.0f : 1.0f - expf(-dt / smoothMs);
  if (!haveSm) { smH = ch; smS = cs; smBr = brt; }
  else {
    if (cs > 0.05f) {
      float dh = ch - smH;
      if (dh > 180.0f) dh -= 360.0f; else if (dh < -180.0f) dh += 360.0f;
      smH += dh * a;
      if (smH < 0) smH += 360.0f; else if (smH >= 360.0f) smH -= 360.0f;
    }
    smS += (cs - smS) * a;
    smBr += (brt - smBr) * a;
  }
  haveSm = true; lastProc = now;

  bool dark = smBr < (tgtKey == TGT_OFF ? offThreshold + 8 : offThreshold);
  tgtKey = dark ? TGT_OFF : pickKey(smH, smS, tgtKey);
  // swatch on the web page: the smoothed colour at full vibrance
  hsvToRgb(smH, fminf(1.0f, smS * 2.5f), dark ? fminf(1.0f, smBr / 255.0f * 4) : 1.0f, dispR, dispG, dispB);
}

// Sends at most one IR code per minInterval toward the target.
void serviceIR() {
  if (!enabled || tgtKey == TGT_NONE) return;
  if (millis() - lastSend < (uint32_t)minInterval) return;
  if (tgtKey == TGT_OFF) {
    if (stripOn) { sendIR(IR_OFF); stripOn = false; curKey = -1; }
    return;
  }
  if (!stripOn) { sendIR(IR_ON); stripOn = true; curKey = -1; return; }
  if (curKey == tgtKey) return;
  int next = nextStep(curKey, tgtKey);
  sendIR(keys[next].code);
  curKey = next;
}

// ---------------- Web page ----------------
const char PAGE[] PROGMEM = R"rawliteral(<!doctype html><html><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>PS4 Ambilight</title><style>
:root{--bg:#0f1115;--card:#181b22;--fg:#e8eaf0;--mut:#8b92a5;--acc:#4c8dff;--ok:#2ecc71;--bad:#e74c3c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,sans-serif}
main{max-width:560px;margin:auto;padding:16px}
h1{font-size:22px;margin:4px 0 14px}
.card{background:var(--card);border-radius:14px;padding:14px;margin-bottom:14px}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);margin:0 0 10px}
.sw{display:flex;gap:10px}
.sw div{flex:1;height:76px;border-radius:10px;display:flex;align-items:flex-end;padding:6px 8px;font-size:12px;border:1px solid #ffffff22;transition:background .25s}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.chip{background:#ffffff12;border-radius:20px;padding:4px 10px;font-size:13px}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:var(--bad)}
.chip.on .dot{background:var(--ok)}
.row{margin:14px 0}
.row label{display:flex;justify-content:space-between;font-size:14px;margin-bottom:6px}
.row output{color:var(--mut)}
.hint{font-size:12px;color:var(--mut);margin-top:4px}
input[type=range]{width:100%}
input[type=number],select{width:100%;padding:10px;border-radius:10px;background:#ffffff12;color:var(--fg);border:1px solid #ffffff22;font-size:15px}
.keys{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.key{padding:14px 4px;border:1px solid #ffffff30;border-radius:10px;font-weight:600;font-size:14px}
.pw{display:flex;gap:8px;margin-bottom:10px}
.pw button{flex:1;padding:12px;border-radius:10px;border:1px solid #ffffff30;background:#ffffff12;color:var(--fg);font-size:15px}
.btn{background:var(--acc);color:#fff;border:0;border-radius:10px;padding:13px;font-size:16px;width:100%;margin-top:6px}
.toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%);background:#000d;padding:8px 16px;border-radius:20px;opacity:0;transition:opacity .3s;pointer-events:none}
.toast.show{opacity:1}
</style></head><body><main>
<h1>PS4 Ambilight bridge</h1>

<div class=card><h2>Live</h2>
<div class=sw><div id=scr>Vibrant colour</div><div id=stp>Strip key</div></div>
<div class=chips>
<span class=chip id=cs><i class=dot></i><span id=cst>No signal</span></span>
<span class=chip>Strip <b id=key>-</b></span>
<span class=chip>Target <b id=tgt>-</b></span>
<span class=chip>PS4 <b id=fps>0</b>/s</span>
<span class=chip>IR <b id=irps>0</b>/s</span>
<span class=chip>IR total <b id=ir>0</b></span>
<span class=chip>Last code <b id=code>-</b></span>
<span class=chip>IP <b id=ip>-</b></span>
</div></div>

<div class=card><h2>Colour</h2>
<div class=row><label><span>Enabled</span><input type=checkbox id=en></label></div>
<div class=row><label>Colour mode</label>
<select id=mode><option value=2>Dominant vibrant colour</option><option value=1>Vivid average</option><option value=0>Plain average</option></select>
<div class=hint>Dominant picks the strongest colour on screen instead of mixing everything into grey. Colours are matched by hue; raise the White slider if grey scenes show a colour.</div></div>
<div class=row><label><span>Show White below saturation</span><output id=whO></output></label>
<input type=range id=wh min=0 max=60 step=1></div>
<div class=row><label><span>Strip off below brightness</span><output id=thO></output></label>
<input type=range id=th min=0 max=120 step=1></div>
</div>

<div class=card><h2>Speed and transitions</h2>
<div class=row><label><span>Smoothing</span><output id=smO></output></label>
<input type=range id=sm min=0 max=1000 step=10>
<div class=hint>Higher = calmer, lower = snappier.</div></div>
<div class=row><label><span>Glide steps</span><output id=glO></output></label>
<input type=range id=gl min=0 max=6 step=1>
<div class=hint>Walk through neighbouring colours instead of jumping. 0 = always jump.</div></div>
<div class=row><label><span>Min time between IR sends</span><output id=ivO></output></label>
<input type=range id=iv min=60 max=1000 step=10>
<div class=hint>One IR code takes about 68 ms, so 80 ms is roughly the fastest.</div></div>
<button class=btn id=save>Save settings</button></div>

<div class=card><h2>IR emitter pin</h2>
<div class=row><label><span>GPIO number</span></label><input type=number id=pin min=0 max=33></div>
<div class=hint>Saving restarts the ESP32. Avoid 1, 3 and 6 to 11.</div>
<button class=btn id=savepin>Save pin and restart</button></div>

<div class=card><h2>Test IR</h2>
<div class=pw><button id=bon>Power on</button><button id=boff>Power off</button></div>
<div class=keys id=keys></div></div>
</main><div class=toast id=toast></div>
<script>
const $=id=>document.getElementById(id);
const j=async u=>(await fetch(u)).json();
const lum=(r,g,b)=>.299*r+.587*g+.114*b;
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),1500);}
function send(q){fetch('/send?'+q).then(()=>toast('IR sent')).catch(()=>toast('Failed'));}
const pairs=[['wh','whO','%'],['th','thO',''],['sm','smO',' ms'],['gl','glO',''],['iv','ivO',' ms']];
pairs.forEach(([a,o,u])=>{$(a).oninput=()=>{$(o).textContent=$(a).value+u};});
async function init(){
 const k=await j('/keys');
 k.forEach((x,i)=>{const b=document.createElement('button');b.className='key';b.textContent=x.n;
  b.style.background='rgb('+x.r+','+x.g+','+x.b+')';b.style.color=lum(x.r,x.g,x.b)>140?'#000':'#fff';
  b.onclick=()=>send('i='+i);$('keys').appendChild(b);});
 const s=await j('/settings');
 $('en').checked=s.en;$('mode').value=s.mode;$('wh').value=s.wh;$('th').value=s.th;
 $('sm').value=s.sm;$('gl').value=s.gl;$('iv').value=s.iv;$('pin').value=s.pin;
 pairs.forEach(([a,o,u])=>{$(o).textContent=$(a).value+u;});
 $('bon').onclick=()=>send('c=on');$('boff').onclick=()=>send('c=off');
 $('save').onclick=()=>{
  const q='en='+($('en').checked?1:0)+'&mode='+$('mode').value+'&wh='+$('wh').value+
   '&th='+$('th').value+'&sm='+$('sm').value+'&gl='+$('gl').value+'&iv='+$('iv').value;
  fetch('/save?'+q).then(()=>toast('Saved')).catch(()=>toast('Failed'));};
 $('savepin').onclick=()=>{
  fetch('/pin?pin='+$('pin').value).then(r=>{
   if(r.ok){toast('Restarting...');setTimeout(()=>location.reload(),6000);}else toast('Invalid pin');
  }).catch(()=>toast('Failed'));};
 tick();setInterval(tick,600);
}
async function tick(){try{const d=await j('/status');
 $('scr').style.background='rgb('+d.r+','+d.g+','+d.b+')';
 $('scr').style.color=lum(d.r,d.g,d.b)>140?'#000':'#fff';
 $('stp').style.background='rgb('+d.kr+','+d.kg+','+d.kb+')';
 $('stp').style.color=lum(d.kr,d.kg,d.kb)>140?'#000':'#fff';
 $('key').textContent=d.key;$('tgt').textContent=d.tgt;$('fps').textContent=d.fps;$('irps').textContent=d.irps;
 $('ir').textContent=d.ir;$('code').textContent=d.code;$('ip').textContent=d.ip;
 const live=d.age>=0&&d.age<2000;$('cs').classList.toggle('on',live);
 $('cst').textContent=live?'Receiving':(d.age<0?'No signal yet':'Signal lost');
}catch(e){}}
init();
</script></body></html>)rawliteral";

String nameOf(int idx) {
  if (idx >= 0) return String(keys[idx].name);
  if (idx == TGT_OFF) return String("Off");
  return String("-");
}

void handleStatus() {
  char buf[480];
  int kr = 0, kg = 0, kb = 0;
  String stripName = "Off";
  if (stripOn) {
    stripName = curKey >= 0 ? String(keys[curKey].name) : String("On");
    if (curKey >= 0) { kr = keys[curKey].r; kg = keys[curKey].g; kb = keys[curKey].b; }
    else { kr = kg = kb = 90; }
  }
  snprintf(buf, sizeof(buf),
    "{\"r\":%d,\"g\":%d,\"b\":%d,\"kr\":%d,\"kg\":%d,\"kb\":%d,\"key\":\"%s\",\"tgt\":\"%s\","
    "\"p\":%lu,\"age\":%ld,\"ir\":%lu,\"fps\":%lu,\"irps\":%lu,\"code\":\"%06lX\",\"ip\":\"%s\"}",
    dispR, dispG, dispB, kr, kg, kb, stripName.c_str(), nameOf(tgtKey).c_str(),
    (unsigned long)packets, packets ? (long)(millis() - lastPacket) : -1L,
    (unsigned long)irSent, (unsigned long)pkRate, (unsigned long)irRate,
    (unsigned long)(lastCode & 0xFFFFFF), WiFi.localIP().toString().c_str());
  server.send(200, "application/json", buf);
}

void handleSettings() {
  char buf[220];
  snprintf(buf, sizeof(buf),
    "{\"en\":%s,\"mode\":%d,\"wh\":%d,\"th\":%d,\"sm\":%d,\"gl\":%d,\"iv\":%d,\"pin\":%d}",
    enabled ? "true" : "false", mode, whiteBelow, offThreshold, smoothMs, glide, minInterval, irPin);
  server.send(200, "application/json", buf);
}

void handleKeys() {
  String j = "[";
  for (int i = 0; i < NKEYS; i++) {
    if (i) j += ",";
    j += "{\"n\":\"" + String(keys[i].name) + "\",\"r\":" + keys[i].r +
         ",\"g\":" + keys[i].g + ",\"b\":" + keys[i].b + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handleSave() {
  enabled = server.arg("en") == "1";
  mode = constrain(server.arg("mode").toInt(), 0, 2);
  whiteBelow = constrain(server.arg("wh").toInt(), 0, 60);
  offThreshold = constrain(server.arg("th").toInt(), 0, 120);
  smoothMs = constrain(server.arg("sm").toInt(), 0, 1000);
  glide = constrain(server.arg("gl").toInt(), 0, 6);
  minInterval = constrain(server.arg("iv").toInt(), 60, 1000);
  prefs.putBool("en", enabled);
  prefs.putInt("mode", mode);
  prefs.putInt("wh", whiteBelow);
  prefs.putInt("th", offThreshold);
  prefs.putInt("sm", smoothMs);
  prefs.putInt("gl", glide);
  prefs.putInt("iv", minInterval);
  server.send(204, "text/plain", "");
}

void handlePin() {
  int p = server.arg("pin").toInt();
  if (!validPin(p)) { server.send(400, "text/plain", "bad pin"); return; }
  prefs.putInt("pin", p);
  server.send(200, "text/plain", "ok");
  restartAt = millis() + 700;
}

void handleSend() {
  if (server.hasArg("c")) {
    if (server.arg("c") == "on") { sendIR(IR_ON); stripOn = true; curKey = -1; }
    else { sendIR(IR_OFF); stripOn = false; curKey = -1; }
  } else if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NKEYS) {
      if (!stripOn) { sendIR(IR_ON); delay(100); }
      sendIR(keys[i].code);
      stripOn = true; curKey = i;
    }
  }
  server.send(204, "text/plain", "");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NKEYS; i++) {
    float h, s, v;
    rgbToHsv(keys[i].r, keys[i].g, keys[i].b, h, s, v);
    keyHue[i] = h;
  }
  for (int i = 0; i < NKEYS; i++) {   // coloured keys sorted by hue (insertion sort)
    if (i == WHITE_IDX) continue;
    int p = ringLen++;
    while (p > 0 && keyHue[ring[p - 1]] > keyHue[i]) { ring[p] = ring[p - 1]; p--; }
    ring[p] = i;
  }
  for (int i = 0; i < ringLen; i++) ringPos[ring[i]] = i;
  ringPos[WHITE_IDX] = -1;

  prefs.begin("ambi", false);
  enabled = prefs.getBool("en", true);
  mode = prefs.getInt("mode", 2);
  whiteBelow = prefs.getInt("wh", 15);
  offThreshold = prefs.getInt("th", 25);
  smoothMs = prefs.getInt("sm", 120);
  glide = prefs.getInt("gl", 4);
  minInterval = prefs.getInt("iv", 80);
  irPin = prefs.getInt("pin", DEFAULT_IR_PIN);
  if (!validPin(irPin)) irPin = DEFAULT_IR_PIN;

  irsend = new IRsend(irPin);
  irsend->begin();
  Serial.print("IR pin: "); Serial.println(irPin);

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

  server.on("/", []() { server.send_P(200, "text/html", PAGE); });
  server.on("/status", handleStatus);
  server.on("/settings", handleSettings);
  server.on("/keys", handleKeys);
  server.on("/save", handleSave);
  server.on("/pin", handlePin);
  server.on("/send", handleSend);
  server.begin();
  udp.begin(4048);   // DDP port used by the PS4-Ambilight plugin / WLED
}

void loop() {
  server.handleClient();
  if (restartAt && millis() > restartAt) ESP.restart();

  // Drain every waiting packet and only process the newest, so a slow IR send
  // never builds up lag.
  static uint8_t buf[1500];
  static uint8_t scratch[64];
  bool got = false; int len = 0, n;
  while ((n = udp.parsePacket()) > 0) {
    if (n >= 13) {
      len = udp.read(buf, sizeof(buf));
      got = len >= 13;
      packets++; lastPacket = millis();
    } else {
      udp.read(scratch, sizeof(scratch));
    }
  }
  if (got) processFrame(buf, len);

  serviceIR();

  static uint32_t rateT = 0, lastPk = 0, lastIr = 0;
  if (millis() - rateT >= 1000) {
    pkRate = packets - lastPk; irRate = irSent - lastIr;
    lastPk = packets; lastIr = irSent; rateT = millis();
  }
}
