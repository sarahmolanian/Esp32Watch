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
#include "esp_sleep.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "wallpapers.h"
#include <Preferences.h>
#include "mario.h"
#include "pokemonlegends.h"
#include "Space.h"
//#include <NimBLEDevice.h>

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
    UI_SET_TIME,
    UI_WALLPAPER,
    UI_Notifications,
    UI_Missed_Calls,
    UI_Text_Messages,
    UI_Telegram,
    UI_WhatsApp,
    UI_Instagram,
    UI_Gmail,
    UI_Outlook
};

static UIState currentState = UI_HOME;
static int      lastSecond  = -1;
static uint32_t lastSteps   = 0;
static int      lastBattery = -1;
static UIState  lastState   = UI_HOME;
static bool     forceRedraw = true;
static int      wifiIndex   = 0;
static bool     wifiToggleSelected = true;
// ===== Wallpaper =====
static uint8_t currentWallpaper = 0;  // 0 = none
static Preferences wallpaperPrefs;
// =====================
static char   wifiPassword[32] = "";
static String selectedSSID     = "";
static bool   wifiConnected    = false;   
static char   wifiIPStr[20]    = "";
static int btNotifIndex = 0;   // which notification is selected
// ===== OLED SLEEP Mode =====
static bool oledSleeping = false;          
static unsigned long oledOffTime  = 0;     

// ===== Keyboard instance =====
static KeyboardState kb;

// ===== how many items fit on screen at once =====
const int visibleCount = 4;



// ===== Shoe Icon =====

const unsigned char epd_bitmap_shoes[] PROGMEM = {
    0x00, 0x00,
    0xF8, 0x00,
    0xFC, 0x00,
    0xFE, 0x00,
    0xFF, 0xE0,
    0x80, 0x10,
    0xFF, 0xFF
};


// ===== Notifications Icon =====

const unsigned char Notifications_Icon [] PROGMEM = {
	0x7f, 0xc0, 0xbf, 0xa0, 0xdf, 0x60, 0xee, 0xe0, 0xd5, 0x60, 0xbb, 0xa0, 0x7f, 0xc0
};


// ===== MENU =====
const char* menuItems[] = {
    "Settings",
    "Games",
    "Notifications",
    "Sleep Mode",
    "Shutdown"
    
};
#define MENU_SIZE 5

// ===== SETTINGS MENU =====
const char* settingsItems[] = {
    "WiFi",
    "Bluetooth",
    "Wallpaper",
    "Set Time",
    "Reset Steps"
};
#define SETTINGS_SIZE 5

// ===== GAMES MENU =====
const char* gamesItems[] = {
    "Dino Game",
    "Pac-Man",
    "Flappy Bird",
    "Doom 1993",
    "Castle Boy"
};
#define GAMES_SIZE 5


// ===== Notifications =====
const char* notificationItems[] = {
    "Missed Calls:",
    "Text Messages:",
    "Telegram:",
    "WhatsApp:",
    "Instagram:",
    "Gmail:",
    "Outlook:"
    
};
#define NOTIFICATIONS_SIZE 7

static int settingsIndex = 0;
static int settingsTopIndex = 0;
static int menuIndex     = 0;
static int menuTopIndex = 0;
static int gamesIndex    = 0;
static int gamesTopIndex = 0;
static int notificationIndex    = 0;
static int notificationTopIndex = 0;
static int wallpaperIndex = 0;
static int wallpaperTopIndex = 0;
static int categoryNotifIndex = 0;


// ===== TIMING =====
static unsigned long lastUpdate = 0;

// ===== DOOM STATE =====
static bool doomInitialised = false;

// ===== BUTTONS =====
#define BTN_UP     6
#define BTN_DOWN   7
#define BTN_SELECT 10
#define BTN_BACK   3

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

// ===== WALLPAPER STORAGE =====
void wallpaper_save(uint8_t index) {
    wallpaperPrefs.begin("watchprefs", false);
    wallpaperPrefs.putUChar("wallpaper", index);
    wallpaperPrefs.end();
}

uint8_t wallpaper_get() {
    return currentWallpaper;
}

void wallpaper_load() {
    wallpaperPrefs.begin("watchprefs", true);
    currentWallpaper = wallpaperPrefs.getUChar("wallpaper", 0);
    wallpaperPrefs.end();
    if (currentWallpaper >= WALLPAPER_COUNT)
        currentWallpaper = 0;
}

// ===== DRAW WALLPAPER =====
static void drawWallpaper() {
    if (currentWallpaper == 0) return;

    // Wallpaper 1 = animated Mario Kart
    if (currentWallpaper == 1) {
        static uint8_t marioFrame = 0;
        static uint32_t marioLastTime = 0;

        uint32_t now = millis();
        uint16_t delay_ms =
            pgm_read_word(&mario_frame_delays_ms[marioFrame]);

        if (now - marioLastTime >= delay_ms) {
            marioLastTime = now;
            marioFrame++;

            if (marioFrame >= mario_frame_count)
                marioFrame = 0;
        }

        const uint8_t* frame =
            mario_data + (uint32_t)marioFrame * mario_frame_size;

        for (uint8_t page = 0; page < 8; page++) {
            for (uint8_t col = 0; col < 128; col++) {
                OLED_GRAM[col][page] =
                    pgm_read_byte(frame + page * 128 + col);
            }
        }

        return;
    }


    // Wallpaper 2 = animated Pokemon Legends
    if (currentWallpaper == 2) {
        static uint8_t pokemonLegendsFrame = 0;
        static uint32_t pokemonLegendsLastTime = 0;

        uint32_t now = millis();

        uint16_t delay_ms =
            pgm_read_word(&pokemon_legends_frame_delays_ms[pokemonLegendsFrame]);

        if (now - pokemonLegendsLastTime >= delay_ms) {
            pokemonLegendsLastTime = now;
            pokemonLegendsFrame++;

            if (pokemonLegendsFrame >= pokemon_legends_frame_count)
                pokemonLegendsFrame = 0;
        }

        const uint8_t* frame =
            pokemon_legends_data + (uint32_t)pokemonLegendsFrame * pokemon_legends_frame_size;

        for (uint8_t page = 0; page < 8; page++) {
            for (uint8_t col = 0; col < 128; col++) {
                OLED_GRAM[col][page] =
                    pgm_read_byte(frame + page * 128 + col);
            }
        }

        return;
    }


    // Wallpaper 5 = Space GIF 
    if (currentWallpaper == 3) {
        static uint8_t SpaceFrame = 0;
        static uint32_t SpaceLastTime = 0;
 
        uint32_t now = millis();
        uint16_t delay_ms =
            pgm_read_word(&Space_frame_delays_ms[SpaceFrame]);
 
        if (now - SpaceLastTime >= delay_ms) {
            SpaceLastTime = now;
            SpaceFrame++;
 
            if (SpaceFrame >= Space_frame_count)
                SpaceFrame = 0;
        }
 
        const uint8_t* frame =
            //Space_data[SpaceFrame];
            Space_data + (uint32_t)SpaceFrame * Space_frame_size;
 
        for (uint8_t page = 0; page < 8; page++) {
            for (uint8_t col = 0; col < 128; col++) {
                OLED_GRAM[col][page] =
                    pgm_read_byte(frame + page * 128 + col);
            }
        }
 
        return;
    }

    // Wallpapers 2-4: static bitmaps
    const uint8_t* bmp = nullptr;
    switch (currentWallpaper) {
        case 2: bmp = wallpaper_2; break;
        case 3: bmp = wallpaper_3; break;
        case 4: bmp = wallpaper_4; break;
        case 5: bmp = wallpaper_5; break;
        case 6: bmp = wallpaper_6; break;
        case 7: bmp = wallpaper_7; break;
        case 8: bmp = wallpaper_8; break;

        default: return;
    }
    for (uint8_t page = 0; page < 8; page++) {
        for (uint8_t col = 0; col < 128; col++) {
            OLED_GRAM[col][page] = pgm_read_byte(
                bmp + page * 128 + col);
        }
    }
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
    wallpaper_load();
    OLED_BufferClear();
    //OLED_Flush();
    OLED_Refresh();
}

// ===== Shoe ICON =====

void drawStepsShoes(int x, int y) {
    const int width = 12;
    const int height = 7;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {

            int byteIndex = row * 2 + (col / 8);
            int bitIndex = 7 - (col % 8);

            if (pgm_read_byte(&epd_bitmap_shoes[byteIndex]) &
                (1 << bitIndex)) {
                OLED_DrawPoint(x + col, y + row);
            }
        }
    }
}

// ===== Notifications ICON =====
void drawNotificationsIcon(int x, int y) {
    const int width = 11;
    const int height = 7;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {

            int byteIndex = row * 2 + (col / 8);
            int bitIndex = 7 - (col % 8);

            if (pgm_read_byte(&Notifications_Icon[byteIndex]) &
                (1 << bitIndex)) {
                OLED_DrawPoint(x + col, y + row);
            }
        }
    }
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

    // Draw wallpaper first, then UI on top
    drawWallpaper();

    // Shoes icon - top left
    OLED_FillRect(0, 0, 16, 10);
    drawStepsShoes(2, 1);

    // Notifications icon - top left
    OLED_FillRect(16, 0, 46, 10);
    drawNotificationsIcon(33,1);

    int btNotifs = bt_getNotificationCount();

    if (btNotifs > 0) {
        char notifStr[6];
        sprintf(notifStr, "%d", btNotifs);

        // Put count immediately to the right of the bell
        OLED_ShowString8(46, 1, notifStr);
    }

    ClockTime t = clock_getTime();
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    OLED_ShowString(2, 16, timeStr, 16);

    char stepStr[20];
    sprintf(stepStr, "%lu", sc_getSteps());
    uint8_t stepWidth = strlen(stepStr) * 6 + 4;
    OLED_FillRect(16, 0, stepWidth, 10);
    OLED_ShowString8(17, 1, stepStr);

    int batt = (int)battery_getPercentage();
    char battStr[6];
    sprintf(battStr, "%d%%", batt);
    int textWidth = strlen(battStr) * 6;
    int batteryX  = 128 - 13;
    int textX     = batteryX - textWidth - 2;
    OLED_FillRect(textX - 2, 0, (batteryX + 13) - (textX - 2), 10);
    drawBattery(batteryX, 0, batt);
    OLED_ShowString8(textX, 0, battStr);

    /*int wsNotifs = ws_getNotificationCount();
    if (wsNotifs > 0) {
        char wsBuf[8];
        sprintf(wsBuf, "N:%d", wsNotifs);
        OLED_ShowString8(90, 54, wsBuf);
    }*/

    OLED_Flush();
}

// ===== DRAW MENU =====
void drawMenu() {
    OLED_BufferClear();

    OLED_ShowString(30, 0, "MENU", 12);

    for (int i = 0; i < 4; i++) {
        int idx = menuTopIndex + i;

        if (idx >= MENU_SIZE)
            break;

        int y = 14 + i * 12;

        if (idx == menuIndex)
            OLED_ShowString(0, y, ">", 12);

        OLED_ShowString(10, y, menuItems[idx], 12);
    }

    OLED_Flush();
}




// ============================================================
// Notification category helpers
// ============================================================

static bool notificationMatchesCategory(const BTNotification* n, int category)
{
    if (!n) return false;

    switch (category) {

        case 0: // Missed Calls
            return strcmp(n->app, "Missed Call") == 0;

        case 1: // Text Messages
            return strcmp(n->app, "Messages") == 0;

        case 2: // Telegram
            return strcmp(n->app, "Telegram") == 0;

        case 3: // WhatsApp
            return strcmp(n->app, "WhatsApp") == 0;

        case 4: // Instagram
            return strcmp(n->app, "Instagram") == 0;

        case 5: // Gmail
            return strcmp(n->app, "Gmail") == 0;

        case 6: // Outlook
            return strcmp(n->app, "Outlook") == 0;

        default:
            return false;
    }
}


static int getNotificationCategoryCount(int category)
{
    int count = 0;

    int total = bt_getNotificationCount();

    for (int i = 0; i < total; i++) {
        const BTNotification* n = bt_getNotification(i);

        if (!notificationMatchesCategory(n, category))
            continue;

        if (n->unread)
            count++;
    }

    return count;
}


void drawNotifications()
{
    OLED_BufferClear();

    OLED_ShowString(30, 0, "NOTIFICATIONS", 12);

    for (int i = 0; i < 4; i++) {

        int idx = notificationTopIndex + i;

        if (idx >= NOTIFICATIONS_SIZE)
            break;

        int y = 14 + i * 12;

        if (idx == notificationIndex)
            OLED_ShowString(0, y, ">", 12);

        char buf[22];

        int count = getNotificationCategoryCount(idx);

        snprintf(
            buf,
            sizeof(buf),
            "%s %d",
            notificationItems[idx],
            count
        );

        OLED_ShowString8(10, y, buf);
    }

    OLED_Flush();
}





static void drawNotificationCategory(const char* title, int category)
{
    OLED_BufferClear();

    OLED_ShowString(25, 0, title, 12);

    int total = bt_getNotificationCount();

    int current = 0;
    int shown = 0;

    for (int i = 0; i < total; i++) {

        const BTNotification* n = bt_getNotification(i);

        if (!notificationMatchesCategory(n, category))
            continue;

        if (current < categoryNotifIndex) {
            current++;
            continue;
        }

        if (shown >= 3)
            break;

        int y = 14 + shown * 17;

        if (current == categoryNotifIndex)
            OLED_ShowString8(0, y, ">");

        char line[22];

        snprintf(
            line,
            sizeof(line),
            "%.20s",
            n->title
        );

        OLED_ShowString8(8, y, line);

        char body[22];

        snprintf(
            body,
            sizeof(body),
            "%.20s",
            n->body
        );

        OLED_ShowString8(8, y + 7, body);

        shown++;
        current++;
    }

    if (shown == 0)
        OLED_ShowString8(15, 28, "No notifications");

    OLED_Flush();
}



static int getCurrentNotificationCategory()
{
    switch (currentState) {

        case UI_Missed_Calls: return 0;
        case UI_Text_Messages: return 1;
        case UI_Telegram:      return 2;
        case UI_WhatsApp:      return 3;
        case UI_Instagram:     return 4;
        case UI_Gmail:         return 5;
        case UI_Outlook:       return 6;

        default:
            return -1;
    }
}






// ===== DRAW GAMES =====
void drawGames() {
    OLED_BufferClear();
    OLED_ShowString(25, 0, "GAMES", 12);

    for (int i = 0; i < 4; i++) {
        int idx = gamesTopIndex + i;

        if (idx >= GAMES_SIZE)
            break;

        int y = 14 + i * 12;

        if (idx == gamesIndex)
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
    for (int i = 0; i < 4; i++) {
        int idx = settingsTopIndex + i;
        if (idx >= SETTINGS_SIZE)
            break;

        int y = 14 + i * 12;

        if (idx == settingsIndex)
            OLED_ShowString(0, y, ">", 12);
        OLED_ShowString(10, y, settingsItems[idx], 12);
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



// ===== DRAW WALLPAPER SELECTION =====
void drawWallpaperSelect() {
    OLED_BufferClear();

    OLED_ShowString(10, 0, "WALLPAPER", 12);

    for (int i = 0; i < 4; i++) {
        int idx = wallpaperTopIndex + i;

        if (idx >= WALLPAPER_COUNT)
            break;

        int y = 14 + i * 12;

        // Draw > next to selected wallpaper
        if (idx == wallpaperIndex)
            OLED_ShowString(0, y, ">", 12);

        // Show * next to currently applied wallpaper
        char buf[20];

        if (idx == currentWallpaper)
            sprintf(buf, "%s *", wallpaperNames[idx]);
        else
            sprintf(buf, "%s", wallpaperNames[idx]);

        OLED_ShowString(10, y, buf, 12);
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
    // ===== Oled SLeep =====
    if (oledSleeping) {
        if (btnPressed(BTN_SELECT)) {
            // Wake OLED
            oledSleeping = false;
            OLED_WR_Byte(0xAF, OLED_CMD);  // display on command
            forceRedraw = true;
        }
        return;  // swallow all other input while OLED is off
    }

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

            // Move selection up
            menuIndex--;

            // Wrap from first to last
            if (menuIndex < 0) {
                menuIndex = MENU_SIZE - 1;

                // Show the last 4 items
                menuTopIndex = MENU_SIZE - visibleCount;

                if (menuTopIndex < 0)
                    menuTopIndex = 0;
            }
            else {
                // If selection moved above visible window,
                // scroll window up
                if (menuIndex < menuTopIndex) {
                    menuTopIndex = menuIndex;
                }
            }

            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {

            // Move selection down
            menuIndex++;

            // Wrap from last to first
            if (menuIndex >= MENU_SIZE) {
                menuIndex = 0;
                menuTopIndex = 0;
            }
            else {
                // If selection moved below visible window,
                // scroll window down
                if (menuIndex >= menuTopIndex + visibleCount) {
                    menuTopIndex = menuIndex - visibleCount + 1;
                }
            }

            forceRedraw = true;
        }

        if (btnPressed(BTN_SELECT)) {
            if (menuIndex == 0) {
                currentState = UI_SETTINGS;
            }
            else if (menuIndex == 1) {
                gamesIndex   = 0;
                currentState = UI_GAME;
            }

            else if (menuIndex == 2){
                notificationIndex   = 0;
                currentState = UI_Notifications;

            }
            else if (menuIndex == 3) {
                // Turn OLED off, ESP keeps running, steps keep counting
                oledSleeping = true;
                OLED_WR_Byte(0xAE, OLED_CMD);  // display off
                OLED_BufferClear();
            }
            
            else if (menuIndex == 4) {
                shutdownRequested = true;  // signal loop() to handle it
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

            // Move selection up
            gamesIndex--;

            // Wrap from first to last
            if (gamesIndex < 0) {
                gamesIndex = GAMES_SIZE - 1;

                // Show the last 4 items
                gamesTopIndex = GAMES_SIZE - visibleCount;

                if (gamesTopIndex < 0)
                    gamesTopIndex = 0;
            }
            else {
                // If selection moved above visible window,
                // scroll window up
                if (gamesIndex < gamesTopIndex) {
                    gamesTopIndex = gamesIndex;
                }
            }

            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {

            // Move selection down
            gamesIndex++;

            // Wrap from last to first
            if (gamesIndex >= GAMES_SIZE) {
                gamesIndex = 0;
                gamesTopIndex = 0;
            }
            else {
                // If selection moved below visible window,
                // scroll window down
                if (gamesIndex >= gamesTopIndex + visibleCount) {
                    gamesTopIndex = gamesIndex - visibleCount + 1;
                }
            }

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

            if (settingsIndex < 0) {
                settingsIndex = SETTINGS_SIZE - 1;
                settingsTopIndex = SETTINGS_SIZE - visibleCount;

                if (settingsTopIndex < 0)
                    settingsTopIndex = 0;
            }
            else if (settingsIndex < settingsTopIndex) {
                settingsTopIndex = settingsIndex;
            }

            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {
            settingsIndex++;
            if (settingsIndex >= SETTINGS_SIZE) {
                settingsIndex = 0;
                settingsTopIndex = 0;
            }
            else if (settingsIndex >= settingsTopIndex + visibleCount) {
                settingsTopIndex = settingsIndex - visibleCount + 1;
            }
            
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
            else if (settingsIndex == 2) {
                wallpaperIndex = currentWallpaper; // start cursor at current
                currentState   = UI_WALLPAPER;
                forceRedraw    = true;
            }
            else if (settingsIndex == 3) {
                ClockTime t = clock_getTime();
                setHour   = t.hour;
                setMinute = t.minute;
                setSecond = t.second;
                setField  = 0;
                currentState = UI_SET_TIME;
                forceRedraw = true;
            }

            else if (settingsIndex == 4) {
                sc_resetSteps();
                forceRedraw = true;
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


    // ===== WALLPAPER =====
    else if (currentState == UI_WALLPAPER) {
        if (btnPressed(BTN_UP)) {

            wallpaperIndex--;

            // Wrap first -> last
            if (wallpaperIndex < 0) {
                wallpaperIndex = WALLPAPER_COUNT - 1;

                wallpaperTopIndex = WALLPAPER_COUNT - visibleCount;

                if (wallpaperTopIndex < 0)
                    wallpaperTopIndex = 0;
            }
            else {
                // Move the window up if > leaves the top
                if (wallpaperIndex < wallpaperTopIndex) {
                    wallpaperTopIndex = wallpaperIndex;
                }
            }

            forceRedraw = true;
        }
        if (btnPressed(BTN_DOWN)) {

            wallpaperIndex++;

            // Wrap last -> first
            if (wallpaperIndex >= WALLPAPER_COUNT) {
                wallpaperIndex = 0;
                wallpaperTopIndex = 0;
            }
            else {
                // Move window down if > leaves the bottom
                if (wallpaperIndex >= wallpaperTopIndex + visibleCount) {
                    wallpaperTopIndex =
                        wallpaperIndex - visibleCount + 1;
                }
            }

            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            // Apply and save immediately
            currentWallpaper = wallpaperIndex;
            wallpaper_save(currentWallpaper);
            forceRedraw = true;
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_SETTINGS;
            forceRedraw  = true;
        }
    } 



    else if (currentState == UI_Notifications) {
        if (btnPressed(BTN_UP)) {
            notificationIndex--;
            if (notificationIndex < 0) {
                notificationIndex = NOTIFICATIONS_SIZE - 1;
                // Show the last 4 items
                notificationTopIndex = NOTIFICATIONS_SIZE - visibleCount;

                if (notificationTopIndex < 0)
                    notificationTopIndex = 0;
            }
            else {
                // If selection moved above visible window,
                // scroll window up
                if (notificationIndex < notificationTopIndex) {
                    notificationTopIndex = notificationIndex;
                }
            }
            forceRedraw = true;
        }

        if (btnPressed(BTN_DOWN)) {
            notificationIndex++;

            if (notificationIndex >= NOTIFICATIONS_SIZE) {
                notificationIndex = 0;
                notificationTopIndex = 0;
            }
            else {
                // If selection moved below visible window,
                // scroll window down
                if (notificationIndex >= notificationTopIndex + visibleCount) {
                    notificationTopIndex = notificationIndex - visibleCount + 1;
                }
            }
            forceRedraw = true;
        }
        if (btnPressed(BTN_SELECT)) {
            categoryNotifIndex = 0;

            if (notificationIndex == 0) {
                currentState = UI_Missed_Calls;
                forceRedraw  = true;
            }
            else if (notificationIndex == 1){
                currentState = UI_Text_Messages;
                forceRedraw  = true;
            }
            else if (notificationIndex == 2) {
                currentState   = UI_Telegram;
                forceRedraw    = true;
            }
            else if (notificationIndex == 3){
                currentState   = UI_WhatsApp;
                forceRedraw    = true;
            }
            else if (notificationIndex == 4) {
                currentState   = UI_Instagram;
                forceRedraw    = true;
            }
            else if (notificationIndex == 5){
                currentState   = UI_Gmail;
                forceRedraw    = true;
            }
            else if (notificationIndex == 6){
                currentState   = UI_Outlook;
                forceRedraw    = true;
            }
        }
        if (btnPressed(BTN_BACK)) {
            currentState = UI_MENU;
            forceRedraw  = true;
        }
    }
        





    // ============================================================
    // INDIVIDUAL NOTIFICATION CATEGORY
    // ============================================================

    else if (currentState == UI_Missed_Calls ||
             currentState == UI_Text_Messages ||
             currentState == UI_Telegram ||
             currentState == UI_WhatsApp ||
             currentState == UI_Instagram ||
             currentState == UI_Gmail ||
             currentState == UI_Outlook) {

        int category = getCurrentNotificationCategory();
        int count = getNotificationCategoryCount(category);

        if (btnPressed(BTN_UP)) {

            categoryNotifIndex--;

            if (categoryNotifIndex < 0)
                categoryNotifIndex = 0;

            forceRedraw = true;
        }

        if (btnPressed(BTN_DOWN)) {

            categoryNotifIndex++;

            if (categoryNotifIndex >= count)
                categoryNotifIndex = count - 1;

            if (categoryNotifIndex < 0)
                categoryNotifIndex = 0;

            forceRedraw = true;
        }

        if (btnPressed(BTN_SELECT)) {

            // Mark the selected notification as read.
            int current = 0;

            for (int i = 0;
                 i < bt_getNotificationCount();
                 i++) {

                const BTNotification* n = bt_getNotification(i);

                if (!notificationMatchesCategory(n, category))
                    continue;

                if (current == categoryNotifIndex) {
                    bt_markRead(i);
                    break;
                }

                current++;
            }

            forceRedraw = true;
        }

        if (btnPressed(BTN_BACK)) {

            currentState = UI_Notifications;
            forceRedraw = true;
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

    // Don't draw anything while OLED is sleeping
    if (oledSleeping) {
        handleInput();   // still check SELECT to wake
        sc_update();     // still count steps
        clock_update();  // still tick clock
        return;          // skip all drawing
    }

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
        case UI_WALLPAPER:       drawWallpaperSelect();             break;
        case UI_Notifications:   drawNotifications();               break;
        case UI_Missed_Calls:    drawNotificationCategory("MISSED CALLS", 0);       break;
        case UI_Text_Messages:   drawNotificationCategory("MESSAGES", 1);           break;
        case UI_Telegram:        drawNotificationCategory("TELEGRAM", 2);           break;
        case UI_WhatsApp:        drawNotificationCategory("WHATSAPP", 3);            break; 
        case UI_Instagram:       drawNotificationCategory("INSTAGRAM", 4);          break;
        case UI_Gmail:           drawNotificationCategory("GMAIL", 5);              break;
        case UI_Outlook:         drawNotificationCategory("OUTLOOK", 6);            break;
        // UI_DOOM_1993 and UI_DINO_GAME handled at top via runDoom()/runDino()
        default: break;
    }
}
