#include "ui.h"
#include "oled_driver.h"
#include "stepcounter.h"
#include "clock.h"
#include "battery.h"
#include "wifi_module.h"
#include "keyboardesp.h"
#include "Doom.h"
#include "display.h"
#include "sprites.h"
#include "DinoGame.h"
#include "castleboy.h"
#include "flappybird.h"
#include "bluetooth_module.h"
#include "web_server.h"
#include <WiFi.h>

// ===== UI STATES =====
enum UIState {
    UI_HOME,
    UI_MENU,
    UI_SETTINGS,
    UI_GAME,
    UI_DINO_GAME,
    UI_PAC_MAN,
    UI_FLAPPY_BIRD,
    UI_DOOM_1993,
    UI_CASTLE_BOY,
    UI_WIFI,
    UI_WIFI_PASS,
    UI_WIFI_STATUS,
    UI_BLUETOOTH,
    UI_BLUETOOTH_NOTIF,
    UI_SET_TIME
};

static UIState currentState = UI_HOME;
static int      lastSecond  = -1;
static uint32_t lastSteps   = 0;
static int      lastBattery = -1;
static UIState  lastState   = UI_HOME;
static bool     forceRedraw = true;
static int      wifiIndex   = 0;
static bool     wifiToggleSelected = true;

static char   wifiPassword[32] = "";
static String selectedSSID     = "";
static bool   wifiConnected    = false;   
static char   wifiIPStr[20]    = "";
static int btNotifIndex = 0;   // which notification is selected

// ===== Keyboard instance =====
static KeyboardState kb;

// ===== MENU =====
const char* menuItems[] = {
    "Settings",
    "Set Time",
    "Games",
    "Reset Steps"
};
#define MENU_SIZE 4

// ===== SETTINGS MENU =====
const char* settingsItems[] = {
    "WiFi",
    "Bluetooth",
    "Wallpaper"
};
#define SETTINGS_SIZE 3

// ===== GAMES MENU =====
const char* gamesItems[] = {
    "Dino Game",
    "Pac-Man",
    "Flappy Bird",
    "Doom 1993",
    "Castle Boy"
};
#define GAMES_SIZE 5

static int settingsIndex = 0;
static int menuIndex     = 0;
static int gamesIndex    = 0;

// ===== TIMING =====
static unsigned long lastUpdate = 0;

// ===== DOOM STATE =====
static bool doomInitialised = false;

// ===== BUTTONS =====
#define BTN_UP     6
#define BTN_DOWN   7
#define BTN_SELECT 10
#define BTN_BACK   20

#define UI_DEBOUNCE_MS 350

static unsigned long ui_btn_last[4] = {0, 0, 0, 0};

bool btnPressed(int pin) {
    uint8_t idx;
    switch (pin) {
        case BTN_UP:     idx = 0; break;
        case BTN_DOWN:   idx = 1; break;
        case BTN_SELECT: idx = 2; break;
        case BTN_BACK:   idx = 3; break;
        default: return false;
    }
    if (digitalRead(pin) == LOW) {
        unsigned long now = millis();
        if (now - ui_btn_last[idx] > UI_DEBOUNCE_MS) {
            ui_btn_last[idx] = now;
            return true;
        }
    } else {
        ui_btn_last[idx] = 0;
    }
    return false;
}

// ===== SET TIME VARIABLES =====
static int setHour   = 0;
static int setMinute = 0;
static int setSecond = 0;
static int setField  = 0;

// ===== HOLD =====
#define HOLD_TIME 400

bool isHolding(int pin) {
    static unsigned long pressTime[40] = {0};
    if (digitalRead(pin) == LOW) {
        if (pressTime[pin] == 0)
            pressTime[pin] = millis();
        else if (millis() - pressTime[pin] > HOLD_TIME)
            return true;
    } else {
        pressTime[pin] = 0;
    }
    return false;
}

// ===== INIT =====
void ui_init() {
    OLED_Init();
    OLED_ColorTurn(0);
    OLED_DisplayTurn(0);
    battery_init();

    pinMode(BTN_UP,     INPUT_PULLUP);
    pinMode(BTN_DOWN,   INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_BACK,   INPUT_PULLUP);

    keyboard_init(&kb);
    bt_init();

    OLED_BufferClear();
    //OLED_Flush();
    OLED_Refresh();
}

// ===== BATTERY ICON =====
void drawBattery(int x, int y, int percent) {
    for (int i = 0; i < 11; i++) {
        OLED_DrawPoint(x + i, y);
        OLED_DrawPoint(x + i, y + 6);
    }
    for (int i = 0; i < 7; i++) {
        OLED_DrawPoint(x,      y + i);
        OLED_DrawPoint(x + 10, y + i);
    }
    OLED_DrawPoint(x + 11, y + 2);
    OLED_DrawPoint(x + 11, y + 3);
    OLED_DrawPoint(x + 11, y + 4);

    int fill = (percent * 8) / 100;
    for (int i = 0; i < fill; i++)
        for (int j = 1; j < 6; j++)
            OLED_DrawPoint(x + 1 + i, y + j);
}

// ===== DRAW HOME =====
void drawHome() {
    OLED_BufferClear();

    ClockTime t = clock_getTime();
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    OLED_ShowString(20, 16, timeStr, 16);

    char stepStr[20];
    sprintf(stepStr, "Steps:%lu", sc_getSteps());
    OLED_ShowString(10, 40, stepStr, 12);

    int batt = (int)battery_getPercentage();
    char battStr[6];
    sprintf(battStr, "%d%%", batt);
    int textWidth = strlen(battStr) * 6;
    int batteryX  = 128 - 13;
    int textX     = batteryX - textWidth - 2;
    drawBattery(batteryX, 0, batt);
    OLED_ShowString8(textX, 0, battStr);
    int wsNotifs = ws_getNotificationCount();
    if (wsNotifs > 0) {
        char wsBuf[8];
        sprintf(wsBuf, "N:%d", wsNotifs);
        OLED_ShowString8(90, 54, wsBuf);
    }

    OLED_Flush();
}

// ===== DRAW MENU =====
void drawMenu() {
    OLED_BufferClear();
    OLED_ShowString(30, 0, "MENU", 12);
    for (int i = 0; i < MENU_SIZE; i++) {
        int y = 14 + i * 12;
        if (i == menuIndex)
            OLED_ShowString(0, y, ">", 12);
        OLED_ShowString(10, y, menuItems[i], 12);
    }
    OLED_Flush();
}

// ===== DRAW GAMES =====
void drawGames() {
    OLED_BufferClear();
    OLED_ShowString(25, 0, "GAMES", 12);

    // show 3 items at a time, scroll with gamesIndex
    for (int i = 0; i < 4; i++) {
        int idx = (gamesIndex + i) % GAMES_SIZE;
        int y   = 14 + i * 12;
        if (i == 0)
            OLED_ShowString(0, y, ">", 12);
        OLED_ShowString(10, y, gamesItems[idx], 12);
    }

    OLED_Flush();
}

// ===== DRAW GAME PLACEHOLDER =====
void drawGameScreen(const char* name) {
    OLED_BufferClear();
    OLED_ShowString(10, 0,  name, 12);
    OLED_ShowString(5,  26, "Coming soon!", 12);
    OLED_ShowString(5,  42, "BACK to exit", 12);
    OLED_Flush();
}

// ===== DRAW SET TIME =====
void drawSetTime() {
    OLED_BufferClear();
    OLED_ShowString(10, 0, "SET TIME", 12);
    char buf[12];
    sprintf(buf, "%02d:%02d:%02d", setHour, setMinute, setSecond);
    OLED_ShowString(20, 20, buf, 16);
    if      (setField == 0) OLED_ShowString(20, 40, "^^", 12);
    else if (setField == 1) OLED_ShowString(52, 40, "^^", 12);
    else                    OLED_ShowString(84, 40, "^^", 12);
    OLED_Flush();
}

// ===== DRAW SETTINGS =====
void drawSettings() {
    OLED_BufferClear();
    OLED_ShowString(20, 0, "SETTINGS", 12);
    for (int i = 0; i < SETTINGS_SIZE; i++) {
        int y = 14 + i * 12;
        if (i == settingsIndex)
            OLED_ShowString(0, y, ">", 12);
        OLED_ShowString(10, y, settingsItems[i], 12);
    }
    OLED_Flush();
}

// ===== DRAW WIFI =====
void drawWiFi() {
    OLED_BufferClear();
    OLED_ShowString(30, 0, "WIFI", 12);
    if (wifiToggleSelected)
        OLED_ShowString(0, 12, ">", 12);
    OLED_ShowString(10, 12,
        wifi_isEnabled() ? "WiFi: ON " : "WiFi: OFF", 12);
    if (wifi_isEnabled()) {
        int count = wifi_getNetworkCount();
        if (count == 0) {
            OLED_ShowString(10, 28, "Scanning...", 12);
        } else {
            for (int i = 0; i < 3; i++) {
                int idx = wifiIndex + i;
                int y   = 26 + i * 13;
                if (idx >= count) continue;
                if (!wifiToggleSelected && i == 0)
                    OLED_ShowString(0, y, ">", 12);
                char ssidShort[16];
                strncpy(ssidShort, wifi_getSSID(idx), 14);
                ssidShort[14] = '\0';
                OLED_ShowString(10, y, ssidShort, 12);
            }
        }
    }
    OLED_Flush();
}

// ===== DRAW WIFI PASSWORD =====
void drawWiFiPass() {
    if (forceRedraw) {
        keyboard_draw(&kb);
        forceRedraw = false;
    }
}

// ===== DRAW WIFI STATUS =====

void drawWiFiStatus() {

    // Phase 1: attempt connection (runs only while !wifiConnected)
    if (!wifiConnected) {
        OLED_BufferClear();
        char ssidShort[16];
        strncpy(ssidShort, selectedSSID.c_str(), 14);
        ssidShort[14] = '\0';
        OLED_ShowString(5,  0,  ssidShort, 12);
        OLED_ShowString(5,  18, "Connecting...", 12);
        OLED_Flush();

        bool ok = wifi_connect(selectedSSID.c_str(), wifiPassword);

        if (ok) {
            IPAddress ip = WiFi.localIP();
            sprintf(wifiIPStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            wifiConnected = true;
            webserver_begin();
        } else {
            // Failed — draw error and wait for BACK
            OLED_BufferClear();
            OLED_ShowString(22, 16, "FAILED!", 12);
            OLED_ShowString(5,  36, "Press BACK to go back", 8);
            OLED_Flush();
        }
        forceRedraw = true;
        return;
    }

    // Phase 2: already connected — draw info screen every tick
    OLED_BufferClear();
    OLED_ShowString(5,  0,  "Connected!", 12);

    char ssidShort[16];
    strncpy(ssidShort, selectedSSID.c_str(), 14);
    ssidShort[14] = '\0';
    OLED_ShowString(5, 16, ssidShort, 12);

    OLED_ShowString(5, 32, wifiIPStr, 12);

    OLED_ShowString(5, 50, "BACK to menu", 8);
    OLED_Flush();
}

// ===== DRAW BLE =====
void drawBluetooth() {
    OLED_BufferClear();
    OLED_ShowString(15, 0, "BLUETOOTH", 12);

    // Toggle row
    OLED_ShowString(0, 14, bt_isEnabled() ? ">BT: ON " : ">BT: OFF", 12);

    // Connection status
    if (bt_isEnabled()) {
        OLED_ShowString(0, 28,
            bt_isConnected() ? "Phone: connected"
                             : "Phone: waiting..", 12);

        // Notification count
        int n = bt_getNotificationCount();
        char buf[20];
        sprintf(buf, "Notifs: %d", n);
        OLED_ShowString(0, 42, buf, 12);

        if (n > 0)
            OLED_ShowString(0, 54, "SELECT=view", 8);
    }
    OLED_Flush();
}

// ===== DRAW BLE Notification list =====

void drawBluetoothNotifs() {
    OLED_BufferClear();
    int total = bt_getNotificationCount();
    if (total == 0) {
        OLED_ShowString(10, 24, "No notifications", 12);
        OLED_Flush();
        return;
    }
    // Show up to 3 notifications, scrolled by btNotifIndex
    for (int i = 0; i < 3; i++) {
        int idx = btNotifIndex + i;
        if (idx >= total) break;
        const BTNotification* n = bt_getNotification(idx);
        if (!n) break;
        int y = i * 20;
        if (idx == btNotifIndex) OLED_ShowString(0, y, ">", 12);
        // Show app + truncated title on one line
        char line[22];
        snprintf(line, sizeof(line), "%-8s %.10s",
                 n->app, n->title);
        OLED_ShowString8(8, y,     line);
        // Show truncated body on next line
        char body[22];
        snprintf(body, sizeof(body), "  %.18s", n->body);
        OLED_ShowString8(0, y + 9, body);
    }
    OLED_Flush();
}

// ===== DOOM RUNNER =====
void runDoom() {
    if (!doomInitialised) {
        doom_setup();
        doom_scene      = INTRO;
        doomInitialised = true;
    }
    doom_loop();
    if (doom_scene == INTRO) {
        doomInitialised = false;
        currentState    = UI_GAME;
        forceRedraw     = true;
        OLED_BufferClear();
        OLED_Flush();
    }
}

// ===== DINO GAME RUNNER =====
void runDino() {
    dinoGame_run();          // blocks until BTN_BACK pressed
    currentState = UI_GAME;
    forceRedraw  = true;
}

// ===== Flappy Bird RUNNER =====
static bool flappyInitialised = false;

void runFlappyBird() {
    if (!flappyInitialised) {
        FlappyBird_SetBackPin(BTN_BACK);  // pin 20
        FlappyBird_Init();
        flappyInitialised = true;
    }
    if (FlappyBird_Loop()) {
        // Back was pressed — return to games menu
        flappyInitialised = false;
        currentState = UI_GAME;
        forceRedraw  = true;
        OLED_BufferClear();
        OLED_Flush();
    }
}

// ===== CASTLE BOY RUNNER =====
static bool castleboyInitialised = false;

void runCastleBoy() {
    if (!castleboyInitialised) {
        castleboy_setup();
        castleboyInitialised = true;
    }

    while (castleboy_loop()) { yield(); }

    // Restore last real frame from PREV
    for (uint8_t col = 0; col < 128; col++)
        for (uint8_t page = 0; page < 8; page++)
            OLED_GRAM[col][page] = OLED_GRAM_PREV[col][page];

    // Exactly like doom_loop() — i goes 0..GRADIENT_COUNT-1
    for (uint8_t i = 0; i < GRADIENT_COUNT; i++) {
        fadeScreen(i, false);
        // Force dirty so displayFlush() actually sends
        memset(OLED_GRAM_PREV, 0x00, sizeof(OLED_GRAM_PREV));
        displayFlush();
        delay(40);
    }

    castleboyInitialised = false;
    currentState = UI_GAME;
    forceRedraw  = true;
    OLED_BufferClear();
    OLED_Flush();
}

// ===== INPUT HANDLING =====
void handleInput() {

    // ===== HOME =====
    if (currentState == UI_HOME) {
        if (btnPressed(BTN_SELECT)) {
            currentState = UI_MENU;
            forceRedraw  = true;
        }
    }

    // ===== MENU =====
    else if (currentState == UI_MENU) {
        if (btnPressed(BTN_UP)) {
            menuIndex--;
            if (menuIndex < 0) menuIndex = MENU_SIZE - 1;
            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            menuIndex++;
            if (menuIndex >= MENU_SIZE) menuIndex = 0;
            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            if (menuIndex == 0) {
                currentState = UI_SETTINGS;
            }
            else if (menuIndex == 1) {
                ClockTime t = clock_getTime();
                setHour   = t.hour;
                setMinute = t.minute;
                setSecond = t.second;
                setField  = 0;
                currentState = UI_SET_TIME;
            }
            else if (menuIndex == 2) {
                gamesIndex   = 0;
                currentState = UI_GAME;
            }
            else if (menuIndex == 3) {
                sc_resetSteps();
            }
            forceRedraw = true;
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_HOME;
            forceRedraw  = true;
        }
    }

    // ===== GAMES LIST =====
    else if (currentState == UI_GAME) {
        if (btnPressed(BTN_UP)) {
            gamesIndex--;
            if (gamesIndex < 0) gamesIndex = GAMES_SIZE - 1;
            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            gamesIndex++;
            if (gamesIndex >= GAMES_SIZE) gamesIndex = 0;
            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            forceRedraw = true;
        if (gamesIndex == 0) {
                currentState = UI_DINO_GAME;  // runDino() called at top of ui_update()
            }
            else if (gamesIndex == 1) currentState = UI_PAC_MAN;
            else if (gamesIndex == 2) currentState = UI_FLAPPY_BIRD;
            else if (gamesIndex == 3) currentState = UI_DOOM_1993;
            else if (gamesIndex == 4) currentState = UI_CASTLE_BOY;
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_MENU;
            forceRedraw  = true;
        }
    }

    // ===== NON-DOOM, NON-DINO GAME PLACEHOLDERS =====
    // UI_DINO_GAME input is owned by dinoGame_tick() — not handled here.
    else if (currentState == UI_PAC_MAN     ||
             currentState == UI_FLAPPY_BIRD) {
        if (btnPressed(BTN_BACK)) {
            currentState = UI_GAME;
            forceRedraw  = true;
        }
    }

    // ===== SETTINGS =====
    else if (currentState == UI_SETTINGS) {
        if (btnPressed(BTN_UP)) {
            settingsIndex--;
            if (settingsIndex < 0) settingsIndex = SETTINGS_SIZE - 1;
            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            settingsIndex++;
            if (settingsIndex >= SETTINGS_SIZE) settingsIndex = 0;
            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            if (settingsIndex == 0) {
                currentState = UI_WIFI;
                wifiIndex    = 0;
                forceRedraw  = true;
            }
            else if (settingsIndex == 1){
                currentState = UI_BLUETOOTH;
                forceRedraw  = true;
            }
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_MENU;
            forceRedraw  = true;
        }
    }

    // ===== WIFI =====
    else if (currentState == UI_WIFI) {
        if (btnPressed(BTN_SELECT)) {
            if (wifiToggleSelected) {
                if (wifi_isEnabled()) {
                    // User turning WiFi OFF — stop server and clean up
                    webserver_stop();
                    wifiConnected = false;
                    wifiIPStr[0]  = '\0';
                    memset(wifiPassword, 0, sizeof(wifiPassword));
                    selectedSSID       = "";
                    wifiIndex          = 0;
                    wifiToggleSelected = true;
                }
                wifi_toggle();
                if (!wifi_isEnabled()) wifiToggleSelected = true;
            } else {
                if (wifi_getNetworkCount() > 0) {
                    selectedSSID = wifi_getSSID(wifiIndex);
                    keyboard_reset(&kb);
                    keyboard_draw(&kb);
                    currentState = UI_WIFI_PASS;
                }
            }
            forceRedraw = true;
        }
        if (btnPressed(BTN_UP)) {
            if (!wifiToggleSelected) wifiToggleSelected = true;
            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            if (wifiToggleSelected) {
                wifiToggleSelected = false;
            } else {
                wifiIndex++;
                if (wifiIndex >= wifi_getNetworkCount()) wifiIndex = 0;
            }
            forceRedraw = true;
        }
        if (btnPressed(BTN_BACK)) {
            currentState       = UI_SETTINGS;
            wifiToggleSelected = true;
            forceRedraw        = true;
        }
    }

    // ===== WIFI PASSWORD =====
    else if (currentState == UI_WIFI_PASS) {
        if (btnPressed(BTN_BACK)) {
            currentState = UI_WIFI;
            forceRedraw  = true;
            return;
        }
        if (keyboard_update(&kb)) {
            strncpy(wifiPassword, kb.password, 31);
            wifiPassword[31] = '\0';
            currentState = UI_WIFI_STATUS;
            forceRedraw  = true;
        }
    }
    // ===== WIFI STATUS =====
    else if (currentState == UI_WIFI_STATUS) {
        if (btnPressed(BTN_BACK)) {
            currentState = UI_WIFI;
            forceRedraw  = true;
        }
    }


    // ===== BLUETOOTH =====
    else if (currentState == UI_BLUETOOTH) {
        if (btnPressed(BTN_SELECT)) {
            if (bt_isEnabled()) {
                // If there are notifications, open the list
                if (bt_getNotificationCount() > 0) {
                    btNotifIndex = 0;
                    currentState = UI_BLUETOOTH_NOTIF;
                } else {
                    bt_disable();
                }
            } else {
                bt_enable();
            }
            forceRedraw = true;
        }
        if (btnPressed(BTN_UP)) {
            // Toggle enable / disable directly with UP too
            if (bt_isEnabled()) bt_disable(); else bt_enable();
            forceRedraw = true;
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_SETTINGS;
            forceRedraw  = true;
        }
    }


    // ===== BLUETOOTH NOTIFICATIONS =====
    else if (currentState == UI_BLUETOOTH_NOTIF) {
        if (btnPressed(BTN_UP)) {
            btNotifIndex--;
            if (btNotifIndex < 0) btNotifIndex = 0;
            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            btNotifIndex++;
            if (btNotifIndex >= bt_getNotificationCount())
                btNotifIndex = bt_getNotificationCount() - 1;
            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            bt_markRead(btNotifIndex);
            forceRedraw = true;
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_BLUETOOTH;
            forceRedraw  = true;
        }
    }


    // ===== SET TIME =====
    else if (currentState == UI_SET_TIME) {
        if (btnPressed(BTN_UP)) {
            if (setField == 0) setHour   = (setHour   + 1)  % 24;
            if (setField == 1) setMinute = (setMinute + 1)  % 60;
            if (setField == 2) setSecond = (setSecond + 1)  % 60;
            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            if (setField == 0) setHour   = (setHour   + 23) % 24;
            if (setField == 1) setMinute = (setMinute + 59) % 60;
            if (setField == 2) setSecond = (setSecond + 59) % 60;
            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            setField++;
            if (setField > 2) setField = 0;
            forceRedraw = true;
        }
        if (btnPressed(BTN_BACK)) {
            clock_setTime(setHour, setMinute, setSecond);
            currentState = UI_MENU;
            forceRedraw  = true;
        }
    }
}

// ===== MAIN UPDATE =====
void ui_update() {
    // Doom owns the CPU while active
    if (currentState == UI_DOOM_1993) {
        runDoom();
        return;
    }

    // Dino Game owns the CPU while active                
    if (currentState == UI_DINO_GAME) {
        runDino();
        return;
    }

    if (currentState == UI_CASTLE_BOY){
        runCastleBoy();
        return;
    }

    if (currentState == UI_FLAPPY_BIRD) {
    runFlappyBird();
    return;
    }

    if (millis() - lastUpdate < 50) return;
    lastUpdate = millis();

    if (currentState == UI_WIFI_PASS) {
        handleInput();
        return;
    }

    handleInput();
    clock_update();
    battery_update();
    wifi_update();
    sc_update();
    bt_update();
    webserver_update();

    if (currentState != lastState) {
        OLED_BufferClear();
        OLED_Flush();
        lastSecond  = -1;
        lastSteps   = 0;
        lastBattery = -1;
        lastState   = currentState;
    }

    // Flash notification badge on home screen when webapp sends one
    if (ws_hasNewNotification()) {
        // forceRedraw makes the home screen redraw with the notif count
        forceRedraw = true;
    }

    switch (currentState) {
        case UI_HOME:            drawHome();                        break;
        case UI_MENU:            drawMenu();                        break;
        case UI_GAME:            drawGames();                       break;
        case UI_SETTINGS:        drawSettings();                    break;
        case UI_WIFI:            drawWiFi();                        break;
        case UI_SET_TIME:        drawSetTime();                     break;
        case UI_WIFI_PASS:       drawWiFiPass();                    break;
        case UI_WIFI_STATUS:     drawWiFiStatus();                  break;
        case UI_PAC_MAN:         drawGameScreen("Pac-Man");         break;
        case UI_FLAPPY_BIRD:     drawGameScreen("Flappy Bird");     break;
        case UI_BLUETOOTH:       drawBluetooth();                   break;
        case UI_BLUETOOTH_NOTIF: drawBluetoothNotifs();             break;
        // UI_DOOM_1993 and UI_DINO_GAME handled at top via runDoom()/runDino()
        default: break;
    }
}
