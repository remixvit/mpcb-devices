#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <OneButton.h>
#include <HX711.h>
#include <MpcbIotCore.h>
#include <WebServer.h>
#include <Update.h>

// ─── Pins ────────────────────────────────────────────────────────────────────
#define BTN_UP   39
#define BTN_SET  37
#define BTN_DN   38
#define HX_DT    32
#define HX_SCK   33

// ─── Layout ──────────────────────────────────────────────────────────────────
#define SCR_W     320
#define SCR_H     240
#define CONT_W    279
#define BTN_X     279
#define BTN_W      41

// ─── Colors ──────────────────────────────────────────────────────────────────
#define C_BG     TFT_BLACK
#define C_ACCENT 0x04FF
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_RED    0xF800
#define C_PANEL  0x2104
#define C_GRAY   0x8410
#define C_WHITE  TFT_WHITE
#define C_DARK   0x1082

// ─── Screens ─────────────────────────────────────────────────────────────────
enum class Screen { MAIN, MENU, SPOOL_PRESET, SPOOL_WEIGHT, ABOUT, CALIB_ZERO, CALIB_WEIGHT, DRYER_WEIGHT };
Screen   currentScreen  = Screen::MAIN;
int8_t   menuIndex      = 0;
IotState currentIotState = IotState::CONNECTING;
int      calibKnownWeight = 500;

// ─── Storage ─────────────────────────────────────────────────────────────────
Preferences prefs;
struct ScaleData {
    float calFactor  = 1.0f;
    long  offset     = 0;
    float dryerW     = 0.0f;
    float spoolEmpty = 200.0f;
    float spoolFull  = 1000.0f;
    bool  calibDone  = false;
    float emaAlpha   = 0.3f;  // EMA smoothing: 0.05=very smooth, 0.5=fast response
} sd;

void loadPrefs() {
    prefs.begin("scale", true);
    sd.calFactor  = prefs.getFloat("cal",      1.0f);
    sd.offset     = prefs.getLong ("offset",   0L);
    sd.dryerW     = prefs.getFloat("dryer",    0.0f);
    sd.spoolEmpty = prefs.getFloat("spool_e",  200.0f);
    sd.spoolFull  = prefs.getFloat("spool_f",  1000.0f);
    sd.calibDone  = prefs.getBool ("cal_done", false);
    sd.emaAlpha   = prefs.getFloat("ema_alpha",0.3f);
    prefs.end();
}
void savePrefs() {
    prefs.begin("scale", false);
    prefs.putFloat("cal",       sd.calFactor);
    prefs.putLong ("offset",    sd.offset);
    prefs.putFloat("dryer",     sd.dryerW);
    prefs.putFloat("spool_e",   sd.spoolEmpty);
    prefs.putFloat("spool_f",   sd.spoolFull);
    prefs.putBool ("cal_done",  sd.calibDone);
    prefs.putFloat("ema_alpha", sd.emaAlpha);
    prefs.end();
}

// ─── HX711 + EMA filter ──────────────────────────────────────────────────────
HX711 hx;
float emaValue   = 0.0f;  // filtered weight in grams
bool  emaReady   = false; // first sample received

// Called from loop() — reads one sample when HX711 is ready (non-blocking)
void hxTick() {
    if(!sd.calibDone || !hx.is_ready()) return;
    float raw = (float)(hx.read() - sd.offset) / sd.calFactor;
    if(!emaReady){ emaValue = raw; emaReady = true; }
    else          emaValue = sd.emaAlpha * raw + (1.0f - sd.emaAlpha) * emaValue;
}

// Blocking read of N samples — used only during calibration
long  hxReadBlocking(int n)   { return hx.read_average(n); }
void  hxTare()                { sd.offset = hxReadBlocking(10); emaReady=false; savePrefs(); }
void  hxCalcFactor(int grams) { sd.calFactor=(float)(hxReadBlocking(10)-sd.offset)/(float)grams; sd.calibDone=true; emaReady=false; savePrefs(); }
float hxGetWeight()           { return emaReady ? emaValue : 0.0f; }

float getFilamentWeight() {
    float w = hxGetWeight() - sd.dryerW - sd.spoolEmpty;
    return w > 0.0f ? w : 0.0f;
}

// ─── Display ─────────────────────────────────────────────────────────────────
TFT_eSPI    tft;
TFT_eSprite spr(&tft);
bool        sprOk = false;

// Универсальный рендер — пишет в spr если есть, иначе в tft
TFT_eSprite* G = nullptr; // graphics context, назначается в setup

void gFill(uint16_t c)                                        { if(sprOk) spr.fillSprite(c); else tft.fillScreen(c); }
void gFillRect(int x,int y,int w,int h,uint16_t c)           { if(sprOk) spr.fillRect(x,y,w,h,c); else tft.fillRect(x,y,w,h,c); }
void gFillRR(int x,int y,int w,int h,int r,uint16_t c)       { if(sprOk) spr.fillRoundRect(x,y,w,h,r,c); else tft.fillRoundRect(x,y,w,h,r,c); }
void gDrawRR(int x,int y,int w,int h,int r,uint16_t c)       { if(sprOk) spr.drawRoundRect(x,y,w,h,r,c); else tft.drawRoundRect(x,y,w,h,r,c); }
void gDrawRect(int x,int y,int w,int h,uint16_t c)           { if(sprOk) spr.drawRect(x,y,w,h,c); else tft.drawRect(x,y,w,h,c); }
void gHLine(int x,int y,int w,uint16_t c)                    { if(sprOk) spr.drawFastHLine(x,y,w,c); else tft.drawFastHLine(x,y,w,c); }
void gVLine(int x,int y,int h,uint16_t c)                    { if(sprOk) spr.drawFastVLine(x,y,h,c); else tft.drawFastVLine(x,y,h,c); }
void gTextColor(uint16_t fg,uint16_t bg)                      { if(sprOk) spr.setTextColor(fg,bg); else tft.setTextColor(fg,bg); }
void gTextSize(int s)                                         { if(sprOk) spr.setTextSize(s); else tft.setTextSize(s); }
void gCursor(int x,int y)                                     { if(sprOk) spr.setCursor(x,y); else tft.setCursor(x,y); }
void gPrint(const char* s)                                    { if(sprOk) spr.print(s); else tft.print(s); }
void gPrint(const String& s)                                  { if(sprOk) spr.print(s); else tft.print(s); }
void gPush()                                                  { if(sprOk) spr.pushSprite(0,0); }

// ─── Widget helpers ───────────────────────────────────────────────────────────
void wBtnPanel(const char* up, const char* mid, const char* dn) {
    gFillRect(BTN_X, 0, BTN_W, SCR_H, C_PANEL);
    gVLine(BTN_X, 0, SCR_H, C_ACCENT);
    gHLine(BTN_X,  80, BTN_W, C_GRAY);
    gHLine(BTN_X, 160, BTN_W, C_GRAY);
    gTextSize(1); gTextColor(C_ACCENT, C_PANEL);
    gCursor(BTN_X+4,  22); gPrint(up);
    gCursor(BTN_X+4, 112); gPrint(mid);
    gCursor(BTN_X+4, 202); gPrint(dn);
}

void wHeader(const char* title, uint16_t color = C_ACCENT) {
    gFillRect(0, 0, CONT_W, 28, color);
    gTextColor(C_BG, color); gTextSize(2);
    gCursor(8, 6); gPrint(title);
}

// ─── MAIN ────────────────────────────────────────────────────────────────────
void _drawMain() {
    gFill(C_BG);

    // ── WiFi статус header ────────────────────────────────────────────────────
    gFillRect(0, 0, CONT_W, 22, C_PANEL);
    gTextSize(1);
    switch(currentIotState) {
        case IotState::AP_PORTAL:
            gTextColor(C_YELLOW, C_PANEL); gCursor(6,7); gPrint("AP MODE  ");
            gTextColor(C_WHITE,  C_PANEL); gPrint("192.168.4.1");
            break;
        case IotState::CONNECTING:
            gTextColor(C_GRAY,   C_PANEL); gCursor(6,7); gPrint("Connecting...");
            break;
        case IotState::CONFIG_SERVER:
            gTextColor(C_ACCENT, C_PANEL); gCursor(6,7); gPrint("CFG  ");
            gTextColor(C_WHITE,  C_PANEL); gPrint(WiFi.localIP().toString());
            break;
        case IotState::RUNNING:
            gTextColor(C_GREEN,  C_PANEL); gCursor(6,7); gPrint("WiFi  ");
            gTextColor(C_WHITE,  C_PANEL); gPrint(WiFi.localIP().toString());
            break;
        default:
            gTextColor(C_GRAY,   C_PANEL); gCursor(6,7); gPrint("...");
    }

    // ── Вес: центрируем в зоне y=22..170 ─────────────────────────────────────
    // "FILAMENT" size=1: 8px tall; gap 8px; число size=6: 48px tall → блок 64px
    // zone height = 170-22 = 148px → top = 22 + (148-64)/2 = 64
    float w = getFilamentWeight();
    char buf[16]; snprintf(buf, sizeof(buf), "%.0f", w);

    int labelY = 64;
    int numY   = labelY + 16;

    gTextSize(1); gTextColor(C_GRAY, C_BG);
    gCursor((CONT_W - 7*6) / 2, labelY); gPrint("FILAMENT");

    int numW   = strlen(buf) * 36;  // textSize=6: 36px per char
    int unitW  = 2 * 12;            // " g" textSize=2
    int startX = (CONT_W - numW - 4 - unitW) / 2;
    gTextSize(6); gTextColor(0xFD20, C_BG);
    gCursor(startX, numY); gPrint(buf);
    gTextSize(2); gTextColor(C_GRAY, C_BG); gPrint(" g");

    // ── Прогресс-бар внизу ───────────────────────────────────────────────────
    float pct = sd.spoolFull>0 ? constrain(w/sd.spoolFull,0.0f,1.0f) : 0;
    int bx=8, by=175, bw=CONT_W-16, bh=30;
    gDrawRR(bx, by, bw, bh, 6, C_GRAY);
    int fill = (int)(pct*(bw-4));
    if(fill>0){
        uint16_t bc = pct>0.3f ? C_GREEN : pct>0.1f ? C_YELLOW : C_RED;
        gFillRR(bx+2, by+2, fill, bh-4, 5, bc);
    }
    snprintf(buf, sizeof(buf), "%.0f%%  %.0f/%.0f g", pct*100, w, sd.spoolFull);
    gTextSize(1); gTextColor(C_BG, 0x0000);
    gCursor(bx + (bw - (int)strlen(buf)*6)/2, by+11); gPrint(buf);

    wBtnPanel("", "MENU", "");
}

void drawMain() { _drawMain(); gPush(); }

// ─── MENU ────────────────────────────────────────────────────────────────────
const char* menuItems[] = {"New spool","Calibrate","Dryer weight","About"};
const int   MENU_COUNT  = 4;

void drawMenu() {
    _drawMain(); // фон без push

    gFillRect(0,0,192,SCR_H,C_DARK);
    gDrawRect(0,0,192,SCR_H,C_ACCENT);
    gTextSize(1); gTextColor(C_ACCENT,C_DARK);
    gCursor(8,7); gPrint("MENU");
    gHLine(0,20,192,C_ACCENT);

    for(int i=0;i<MENU_COUNT;i++){
        int y=28+i*30; bool sel=(i==menuIndex);
        gFillRect(1,y,190,28,sel?C_ACCENT:C_DARK);
        gTextColor(sel?C_BG:C_WHITE, sel?C_ACCENT:C_DARK);
        gTextSize(2); gCursor(10,y+6); gPrint(menuItems[i]);
    }

    wBtnPanel("UP","OK","DN");
    gPush();
}

// ─── SPOOL PRESET ─────────────────────────────────────────────────────────────
const int presets[]    = {500,750,1000};
const int PRESET_COUNT = 3;
int8_t presetIndex = 1;

void drawSpoolPreset() {
    gFill(C_BG); wHeader("New spool");
    gTextSize(1); gTextColor(C_GRAY,C_BG);
    gCursor(8,36); gPrint("Select filament weight:");

    for(int i=0;i<PRESET_COUNT;i++){
        int y=58+i*48; bool sel=(i==presetIndex);
        gFillRR(8,y,CONT_W-16,38,5,sel?C_ACCENT:C_PANEL);
        gTextColor(sel?C_BG:C_WHITE, sel?C_ACCENT:C_PANEL);
        gTextSize(2); char buf[16];
        snprintf(buf,sizeof(buf),"%d g",presets[i]);
        gCursor(20,y+10); gPrint(buf);
    }
    gTextSize(1); gTextColor(C_GRAY,C_BG);
    gCursor(8,220); gPrint("hold UP/DN for custom");
    wBtnPanel("UP","OK","DN");
    gPush();
}

// ─── SPOOL WEIGHT ─────────────────────────────────────────────────────────────
int customSpoolWeight = 1000;

void drawSpoolWeight() {
    gFill(C_BG); wHeader("Custom weight");
    gTextSize(1); gTextColor(C_GRAY,C_BG);
    gCursor(8,36); gPrint("Filament in spool:");

    char buf[16]; snprintf(buf,sizeof(buf),"%d",customSpoolWeight);
    gTextSize(4); gTextColor(C_WHITE,C_BG); gCursor(8,70); gPrint(buf);
    gTextSize(2); gTextColor(C_GRAY,C_BG);  gPrint(" g");

    gTextSize(1); gTextColor(C_GRAY,C_BG);
    gCursor(8,175); gPrint("short: +-50g");
    gCursor(8,190); gPrint("long:  +-500g");
    wBtnPanel("+50","OK","-50");
    gPush();
}

// ─── CALIB ZERO ──────────────────────────────────────────────────────────────
void drawCalibZero() {
    gFill(C_BG); wHeader("Calibrate 1/2");
    gTextSize(2); gTextColor(C_WHITE, C_BG);
    gCursor(8, 40); gPrint("Remove all weight");
    gCursor(8, 62); gPrint("from the platform");
    gTextSize(1); gTextColor(C_GRAY, C_BG);
    gCursor(8, 100); gPrint("Platform must be completely empty.");
    gCursor(8, 116); gPrint("Include dryer, spools, everything.");
    gTextColor(C_YELLOW, C_BG);
    gCursor(8, 160); gPrint("Press SET to zero the scale.");
    wBtnPanel("", "ZERO", "");
    gPush();
}

// ─── CALIB WEIGHT ─────────────────────────────────────────────────────────────
void drawCalibWeight() {
    gFill(C_BG); wHeader("Calibrate 2/2");
    gTextSize(1); gTextColor(C_GRAY, C_BG);
    gCursor(8, 36); gPrint("Place known weight on platform:");
    char buf[16]; snprintf(buf, sizeof(buf), "%d", calibKnownWeight);
    int numW = strlen(buf)*30 + 2*12; // size5=30px/char, 'g' size2
    int sx = (CONT_W - numW) / 2;
    gTextSize(5); gTextColor(C_WHITE, C_BG);
    gCursor(sx, 60); gPrint(buf);
    gTextSize(2); gTextColor(C_GRAY, C_BG); gPrint(" g");
    gTextSize(1); gTextColor(C_GRAY, C_BG);
    gCursor(8, 155); gPrint("short: +-50g   long: +-500g");
    wBtnPanel("+50", "OK", "-50");
    gPush();
}

// ─── DRYER WEIGHT ─────────────────────────────────────────────────────────────
void drawDryerWeight() {
    gFill(C_BG); wHeader("Dryer weight", C_PANEL);
    gTextSize(1); gTextColor(C_GRAY, C_BG);
    gCursor(8, 36); gPrint("Dryer weight (empty):");
    char buf[16]; snprintf(buf, sizeof(buf), "%.0f", sd.dryerW);
    int numW = strlen(buf)*24 + 2*12; // size4=24px/char
    int sx = (CONT_W - numW) / 2;
    gTextSize(4); gTextColor(C_WHITE, C_BG);
    gCursor(sx, 65); gPrint(buf);
    gTextSize(2); gTextColor(C_GRAY, C_BG); gPrint(" g");
    gTextSize(1); gTextColor(C_GRAY, C_BG);
    gCursor(8, 150); gPrint("short: +-50g   long: +-500g");
    gTextColor(C_YELLOW, C_BG);
    gCursor(8, 168); gPrint("hold SET to measure now");
    wBtnPanel("+50", "OK", "-50");
    gPush();
}

// ─── ABOUT ───────────────────────────────────────────────────────────────────
void drawAbout() {
    gFill(C_BG); wHeader("About");
    auto row=[&](int line,const char* label,const String& val){
        int y=36+line*20;
        gTextSize(1); gTextColor(C_GRAY,C_BG); gCursor(8,y); gPrint(label);
        gTextColor(C_WHITE,C_BG); gPrint(val);
    };
    row(0,"IP:     ",WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"no wifi");
    row(1,"SSID:   ",WiFi.status()==WL_CONNECTED?WiFi.SSID():"---");
    row(2,"Dryer:  ",String(sd.dryerW,0)+" g");
    row(3,"Spool0: ",String(sd.spoolEmpty,0)+" g");
    row(4,"Calib:  ",sd.calibDone?"done":"not done");
    char buf[24]; uint32_t up=millis()/1000;
    snprintf(buf,sizeof(buf),"%02lu:%02lu:%02lu",up/3600,(up%3600)/60,up%60);
    row(5,"Uptime: ",String(buf));
    wBtnPanel("","BACK","");
    gPush();
}

// ─── Navigation ──────────────────────────────────────────────────────────────
void showScreen(Screen s) {
    currentScreen = s;
    switch(s){
        case Screen::MAIN:         drawMain();        break;
        case Screen::MENU:         drawMenu();        break;
        case Screen::SPOOL_PRESET: drawSpoolPreset(); break;
        case Screen::SPOOL_WEIGHT:  drawSpoolWeight();  break;
        case Screen::ABOUT:         drawAbout();        break;
        case Screen::CALIB_ZERO:    drawCalibZero();    break;
        case Screen::CALIB_WEIGHT:  drawCalibWeight();  break;
        case Screen::DRYER_WEIGHT:  drawDryerWeight();  break;
    }
}

// ─── Button handlers ─────────────────────────────────────────────────────────
void onUp() {
    switch(currentScreen){
        case Screen::MENU:         menuIndex=(menuIndex-1+MENU_COUNT)%MENU_COUNT; drawMenu(); break;
        case Screen::SPOOL_PRESET: presetIndex=(presetIndex-1+PRESET_COUNT)%PRESET_COUNT; drawSpoolPreset(); break;
        case Screen::SPOOL_WEIGHT:  customSpoolWeight=constrain(customSpoolWeight+50,10,10000); drawSpoolWeight(); break;
        case Screen::CALIB_WEIGHT:  calibKnownWeight=constrain(calibKnownWeight+50,10,5000); drawCalibWeight(); break;
        case Screen::DRYER_WEIGHT:  sd.dryerW=constrain(sd.dryerW+50,0.0f,5000.0f); drawDryerWeight(); break;
        default: break;
    }
}
void onDn() {
    switch(currentScreen){
        case Screen::MENU:          menuIndex=(menuIndex+1)%MENU_COUNT; drawMenu(); break;
        case Screen::SPOOL_PRESET:  presetIndex=(presetIndex+1)%PRESET_COUNT; drawSpoolPreset(); break;
        case Screen::SPOOL_WEIGHT:  customSpoolWeight=constrain(customSpoolWeight-50,10,10000); drawSpoolWeight(); break;
        case Screen::CALIB_WEIGHT:  calibKnownWeight=constrain(calibKnownWeight-50,10,5000); drawCalibWeight(); break;
        case Screen::DRYER_WEIGHT:  sd.dryerW=constrain(sd.dryerW-50,0.0f,5000.0f); drawDryerWeight(); break;
        default: break;
    }
}
void onUpLong() {
    switch(currentScreen){
        case Screen::SPOOL_WEIGHT: customSpoolWeight=constrain(customSpoolWeight+500,10,10000); drawSpoolWeight(); break;
        case Screen::CALIB_WEIGHT: calibKnownWeight=constrain(calibKnownWeight+500,10,5000); drawCalibWeight(); break;
        case Screen::DRYER_WEIGHT: sd.dryerW=constrain(sd.dryerW+500,0.0f,5000.0f); drawDryerWeight(); break;
        default: break;
    }
}
void onDnLong() {
    switch(currentScreen){
        case Screen::SPOOL_WEIGHT: customSpoolWeight=constrain(customSpoolWeight-500,10,10000); drawSpoolWeight(); break;
        case Screen::CALIB_WEIGHT: calibKnownWeight=constrain(calibKnownWeight-500,10,5000); drawCalibWeight(); break;
        case Screen::DRYER_WEIGHT: sd.dryerW=constrain(sd.dryerW-500,0.0f,5000.0f); drawDryerWeight(); break;
        default: break;
    }
}

void onSetClick() {
    switch(currentScreen){
        case Screen::MENU:
            switch(menuIndex){
                case 0: presetIndex=1; showScreen(Screen::SPOOL_PRESET); break;
                case 1: showScreen(Screen::CALIB_ZERO); break;
                case 2: showScreen(Screen::DRYER_WEIGHT); break;
                case 3: showScreen(Screen::ABOUT); break;
            } break;
        case Screen::SPOOL_PRESET:  customSpoolWeight=presets[presetIndex]; showScreen(Screen::SPOOL_WEIGHT); break;
        case Screen::SPOOL_WEIGHT:  sd.spoolFull=customSpoolWeight; savePrefs(); showScreen(Screen::MAIN); break;
        case Screen::ABOUT:         showScreen(Screen::MENU); break;
        case Screen::CALIB_ZERO:    hxTare(); showScreen(Screen::CALIB_WEIGHT); break;
        case Screen::CALIB_WEIGHT:  hxCalcFactor(calibKnownWeight); showScreen(Screen::MAIN); break;
        case Screen::DRYER_WEIGHT:  savePrefs(); showScreen(Screen::MAIN); break;
        default: break;
    }
}
void onSetLong() {
    switch(currentScreen){
        case Screen::MAIN:         menuIndex=0; showScreen(Screen::MENU); break;
        case Screen::DRYER_WEIGHT: sd.dryerW=hxGetWeight(); savePrefs(); drawDryerWeight(); break;
        default:                   showScreen(Screen::MAIN); break;
    }
}

// ─── Buttons ─────────────────────────────────────────────────────────────────
OneButton btnUp (BTN_UP,  true, false);
OneButton btnSet(BTN_SET, true, true);
OneButton btnDn (BTN_DN,  true, false);

// ─── IoT ─────────────────────────────────────────────────────────────────────
MpcbIotCore iot;
String      deviceId;

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);
    loadPrefs();
    hx.begin(HX_DT, HX_SCK);
    delay(500);
    if(hx.is_ready()){
        long raw = hx.read();
        Serial.printf("[HX711] OK  raw=%ld\n", raw);
        Log.log("HX711", "OK raw=" + String(raw));
    } else {
        Serial.println("[HX711] NOT READY — check wiring DT=IO32 SCK=IO33");
        Log.log("HX711", "NOT READY — check wiring");
    }

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(C_BG);

    // Пробуем 16-bit спрайт (150KB), фолбэк на 8-bit (76KB)
    spr.setColorDepth(16);
    spr.createSprite(SCR_W, SCR_H);
    if(!spr.created()){
        Serial.println("16-bit sprite failed, trying 8-bit");
        spr.setColorDepth(8);
        spr.createSprite(SCR_W, SCR_H);
    }
    sprOk = spr.created();
    Serial.printf("Sprite: %s  Free heap: %u\n", sprOk?"OK":"FAILED", ESP.getFreeHeap());

    btnUp.attachClick(onUp);
    btnUp.attachLongPressStart(onUpLong);
    btnSet.attachClick(onSetClick);
    btnSet.attachLongPressStart(onSetLong);
    btnDn.attachClick(onDn);
    btnDn.attachLongPressStart(onDnLong);

    iot.storage().begin();
    { MqttConfig empty; iot.storage().saveMqtt(empty); } // clear stale MQTT from flash
    WiFi.mode(WIFI_STA);
    {
        DeviceConfig dev = iot.storage().loadDevice();
        if(dev.deviceId.isEmpty()){
            uint8_t mac[6]; WiFi.macAddress(mac);
            char s[5]; snprintf(s,sizeof(s),"%02X%02X",mac[4],mac[5]);
            dev.deviceId="esp32-"+String(s); dev.deviceName=DEVICE_NAME;
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
    }

    iot.onStateChange([](IotState s){
        currentIotState = s;
        if(currentScreen==Screen::MAIN) drawMain();
    });
    iot.disableConfigServer();
    iot.begin("T4 Scale");

    showScreen(Screen::MAIN);
}

// ─── Web server ───────────────────────────────────────────────────────────────
WebServer web(80);

static const char NAV[] =
    "<nav>"
    "<span>&#9878; T4 Scale</span>"
    "<a href='/' id='n1'>Monitor</a>"
    "<a href='/calibrate' id='n2'>Calibrate</a>"
    "<a href='/wifi' id='n3'>WiFi</a>"
    "<a href='/ota' id='n4'>OTA</a>"
    "<a href='/logs' id='n5'>Logs</a>"
    "</nav>";

static const char CSS[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>T4 Scale</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#111;color:#eee;font-family:sans-serif;min-height:100vh}"
    "nav{background:#1a1a2e;padding:12px 16px;display:flex;align-items:center;"
    "gap:10px;flex-wrap:wrap;border-bottom:2px solid #7b2feb}"
    "nav span{color:#7b2feb;font-weight:bold;font-size:16px;margin-right:8px}"
    "nav a{color:#bbb;text-decoration:none;padding:6px 14px;border-radius:20px;"
    "font-size:14px;border:1px solid #333}"
    "nav a.act{background:#7b2feb;color:#fff;border-color:#7b2feb}"
    ".pg{padding:28px 20px;max-width:540px;margin:auto}"
    "h3{color:#aaa;font-size:13px;text-transform:uppercase;letter-spacing:1px;margin-bottom:16px}"
    "input,select{width:100%;padding:10px 14px;background:#222;border:1px solid #444;"
    "border-radius:8px;color:#eee;font-size:15px;margin-bottom:14px}"
    "button,input[type=submit]{width:100%;padding:11px;background:#7b2feb;border:none;"
    "border-radius:8px;color:#fff;font-size:15px;cursor:pointer}"
    "button:hover{background:#9a4fff}"
    ".info{color:#888;font-size:13px;margin-bottom:14px}"
    ".badge{display:inline-block;padding:4px 14px;border-radius:20px;font-size:13px;margin:8px 0}"
    ".ok{background:#1a3a1a;color:#4e4} .warn{background:#3a2200;color:#fa0}"
    ".weight{font-size:88px;font-weight:bold;color:#fd8000;line-height:1;text-align:center}"
    ".unit{font-size:26px;color:#888;text-align:center;margin-top:6px}"
    ".bar-wrap{background:#2a2a2a;border-radius:14px;height:28px;margin:20px auto;overflow:hidden}"
    ".bar{height:100%;border-radius:14px;transition:width .6s}"
    ".row{display:flex;justify-content:center;gap:24px;margin-top:10px;color:#888;font-size:14px}"
    "</style></head><body>";

void webSendPage(int act, const String& body) {
    String nav = String(NAV);
    nav.replace("id='n" + String(act) + "'", "id='n" + String(act) + "' class='act'");
    web.send(200, "text/html", String(CSS) + nav + body + "</body></html>");
}

void webSetup() {
    // ── Monitor ──────────────────────────────────────────────────────────────
    web.on("/", [](){ webSendPage(1,
        "<div class='pg' style='text-align:center'>"
        "<div class='weight' id='w'>---</div>"
        "<div class='unit'>grams</div>"
        "<div class='bar-wrap'><div class='bar' id='bar' style='width:0%'></div></div>"
        "<div class='row'><span id='pct'>--</span><span id='cfg'>--</span></div>"
        "<div id='cal' style='margin-top:12px'></div>"
        "</div>"
        "<script>"
        "function u(){fetch('/api/weight').then(r=>r.json()).then(d=>{"
        "document.getElementById('w').textContent=Math.round(d.weight);"
        "var p=Math.min(d.pct,100),b=document.getElementById('bar');"
        "b.style.width=p+'%';b.style.background=p>30?'#07e0':p>10?'#ffe0':'#f800';"
        "document.getElementById('pct').textContent=p.toFixed(1)+'%  '+Math.round(d.weight)+'g / '+d.full+'g';"
        "document.getElementById('cfg').textContent='Dryer: '+d.dryer+'g';"
        "document.getElementById('cal').innerHTML=d.calib"
        "?\"<span class='badge ok'>&#10003; Calibrated</span>\""
        ":\"<span class='badge warn'>&#9888; Not calibrated</span>\";"
        "}).catch(()=>{})}"
        "u();setInterval(u,2000);"
        "</script>"
    ); });

    // ── API weight ────────────────────────────────────────────────────────────
    web.on("/api/weight", [](){
        char j[128]; float w=getFilamentWeight();
        float pct=sd.spoolFull>0?w/sd.spoolFull*100.0f:0.0f;
        snprintf(j,sizeof(j),"{\"weight\":%.0f,\"full\":%.0f,\"dryer\":%.0f,\"pct\":%.1f,\"calib\":%s}",
            w,sd.spoolFull,sd.dryerW,pct,sd.calibDone?"true":"false");
        web.send(200,"application/json",j);
    });

    // ── WiFi ─────────────────────────────────────────────────────────────────
    // ── Calibrate ─────────────────────────────────────────────────────────────
    web.on("/calibrate", [](){
        String status = sd.calibDone
            ? "<span class='badge ok'>&#10003; Calibrated</span>"
            : "<span class='badge warn'>&#9888; Not calibrated</span>";
        char info[80];
        snprintf(info, sizeof(info), "cal_factor=%.4f  offset=%ld  dryer=%.0fg",
            sd.calFactor, sd.offset, sd.dryerW);
        webSendPage(2,
            "<div class='pg'>"
            "<h3>Calibration Status</h3>"
            + status +
            "<div class='info' style='margin-top:10px;font-family:monospace'>" + String(info) + "</div>"

            "<h3 style='margin-top:24px'>Step 1 — Zero Scale</h3>"
            "<div class='info'>Remove everything from the platform, then press Zero.</div>"
            "<form method='POST' action='/api/calib/zero'>"
            "<input type='submit' value='Zero Scale (Tare)'>"
            "</form>"

            "<h3 style='margin-top:24px'>Step 2 — Known Weight</h3>"
            "<div class='info'>Place a known weight on the platform.</div>"
            "<form method='POST' action='/api/calib/weight'>"
            "<input name='grams' type='number' placeholder='Weight in grams (e.g. 500)' min='10' max='5000'>"
            "<input type='submit' value='Calibrate'>"
            "</form>"

            "<h3 style='margin-top:24px'>Filter (EMA Alpha)</h3>"
            "<div class='info'>"
            "Controls smoothing of weight readings.<br>"
            "<b>0.05</b> — very smooth, slow to react (good for stable loads)<br>"
            "<b>0.1</b> — balanced (default)<br>"
            "<b>0.3</b> — faster response, more noise<br>"
            "<b>0.5</b> — minimal filtering, raw-like"
            "</div>"
            "<form method='POST' action='/api/calib/alpha'>"
            "<input name='alpha' type='number' step='0.01' min='0.01' max='1.0' value='"
            + String(sd.emaAlpha, 2) + "'>"
            "<input type='submit' value='Save Filter'>"
            "</form>"

            "<h3 style='margin-top:24px'>Dryer Weight</h3>"
            "<div class='info'>Place <b>empty dryer</b> on platform (no spool), then measure or enter manually.</div>"
            "<form method='POST' action='/api/calib/dryer/measure'>"
            "<input type='submit' value='&#128207; Measure Now (" + String((int)hxGetWeight()) + "g on platform)'>"
            "</form>"
            "<form method='POST' action='/api/calib/dryer' style='margin-top:10px'>"
            "<input name='grams' type='number' placeholder='Or enter manually (grams)' min='0' max='5000' value='"
            + String((int)sd.dryerW) + "'>"
            "<input type='submit' value='Save Dryer Weight'>"
            "</form>"

            "<h3 style='margin-top:24px'>Empty Spool Weight</h3>"
            "<div class='info'>Place <b>dryer + empty spool</b> on platform, then measure or enter manually.</div>"
            "<form method='POST' action='/api/calib/spool/measure'>"
            "<input type='submit' value='&#128207; Measure Now (" + String(max(0.0f, hxGetWeight()-sd.dryerW)) + "g above dryer)'>"
            "</form>"
            "<form method='POST' action='/api/calib/spool' style='margin-top:10px'>"
            "<input name='grams' type='number' placeholder='Or enter manually (grams)' min='0' max='2000' value='"
            + String((int)sd.spoolEmpty) + "'>"
            "<input type='submit' value='Save Spool Weight'>"
            "</form>"
            "</div>"
        );
    });
    web.on("/api/calib/zero", HTTP_POST, [](){
        hxTare();
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });
    web.on("/api/calib/weight", HTTP_POST, [](){
        int g = web.arg("grams").toInt();
        if(g > 0) hxCalcFactor(g);
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });
    web.on("/api/calib/dryer", HTTP_POST, [](){
        sd.dryerW = web.arg("grams").toFloat();
        savePrefs();
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });
    web.on("/api/calib/alpha", HTTP_POST, [](){
        float a = web.arg("alpha").toFloat();
        if(a >= 0.01f && a <= 1.0f){ sd.emaAlpha = a; emaReady = false; savePrefs(); }
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });
    web.on("/api/calib/dryer/measure", HTTP_POST, [](){
        sd.dryerW = hxGetWeight();
        savePrefs();
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });
    web.on("/api/calib/spool", HTTP_POST, [](){
        sd.spoolEmpty = web.arg("grams").toFloat();
        savePrefs();
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });
    web.on("/api/calib/spool/measure", HTTP_POST, [](){
        sd.spoolEmpty = max(0.0f, hxGetWeight() - sd.dryerW);
        savePrefs();
        web.sendHeader("Location", "/calibrate"); web.send(303);
    });

    // ── WiFi ─────────────────────────────────────────────────────────────────
    web.on("/wifi", [](){
        WifiConfig wf = iot.storage().loadWifi();
        String body = "<div class='pg'><h3>WiFi Settings</h3>"
            "<div class='info'>Current: " + (WiFi.isConnected() ? WiFi.SSID()+" — "+WiFi.localIP().toString() : "not connected") + "</div>"
            "<form method='POST' action='/api/wifi'>"
            "<input name='ssid' placeholder='SSID' value='" + wf.ssid + "'>"
            "<input name='pass' placeholder='Password' type='password'>"
            "<input type='submit' value='Save &amp; Reboot'>"
            "</form></div>";
        webSendPage(3, body);
    });
    web.on("/api/wifi", HTTP_POST, [](){
        iot.storage().saveWifi(web.arg("ssid"), web.arg("pass"));
        web.send(200,"text/html", String(CSS)+"<div style='text-align:center;padding:60px'>"
            "<p style='color:#4e4;font-size:20px'>Saved! Rebooting...</p></div></body></html>");
        delay(1000); ESP.restart();
    });

    // ── OTA ──────────────────────────────────────────────────────────────────
    web.on("/ota", [](){  webSendPage(4,
        "<div class='pg'><h3>OTA Firmware Update</h3>"
        "<form method='POST' action='/api/ota' enctype='multipart/form-data'>"
        "<input type='file' name='fw' accept='.bin' style='margin-bottom:14px'>"
        "<input type='submit' value='Upload &amp; Flash'>"
        "</form><div class='info' style='margin-top:12px'>Select firmware.bin from .pio/build/t4/</div>"
        "</div>"
    ); });
    web.on("/api/ota", HTTP_POST,
        [](){
            web.send(200,"text/html", String(CSS)+(Update.hasError()
                ? "<div style='text-align:center;padding:60px'><p style='color:#f44;font-size:20px'>Update FAILED</p></div>"
                : "<div style='text-align:center;padding:60px'><p style='color:#4e4;font-size:20px'>Done! Rebooting...</p></div>")
                +"</body></html>");
            delay(500); ESP.restart();
        },
        [](){
            HTTPUpload& up = web.upload();
            if(up.status==UPLOAD_FILE_START) Update.begin();
            else if(up.status==UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
            else if(up.status==UPLOAD_FILE_END) Update.end(true);
        }
    );

    // ── Logs ─────────────────────────────────────────────────────────────────
    web.on("/logs", [](){
        String logs = Log.toText();
        logs.replace("&","&amp;"); logs.replace("<","&lt;");
        webSendPage(5,
            "<div class='pg'><h3>Device Log</h3>"
            "<pre style='background:#1a1a1a;padding:14px;border-radius:8px;"
            "font-size:12px;color:#aaa;overflow-x:auto;white-space:pre-wrap'>"
            + logs + "</pre></div>"
        );
    });

    web.begin();
}

// ─── Loop ────────────────────────────────────────────────────────────────────
uint32_t lastRefresh = 0;
bool     webStarted  = false;
void loop() {
    btnUp.tick(); btnSet.tick(); btnDn.tick();
    iot.loop();

    if(!webStarted && (currentIotState==IotState::CONFIG_SERVER || currentIotState==IotState::RUNNING)){
        webSetup();
        webStarted = true;
    }
    if(webStarted) web.handleClient();
    hxTick();

    if(currentScreen==Screen::MAIN && millis()-lastRefresh>500){
        lastRefresh=millis(); drawMain();
    }
}
