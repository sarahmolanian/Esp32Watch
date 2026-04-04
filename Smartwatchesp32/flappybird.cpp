#include "flappybird.h"

// ── Frame rate ───────────────────────────────────────────────
#define TARGET_FPS          30
#define FRAME_MS            (1000 / TARGET_FPS)   // ~33 ms per frame

// ── Tuning ───────────────────────────────────────────────────
#define PIPE_SPEED_INIT     2
#define PIPE_SPEED_MAX      5
#define PIPE_SPEED_STEP     1
#define PIPE_SPEED_EVERY    20      // increase speed every N points

// Fixed-point x16 bird physics (per frame at 30 fps)
#define GRAVITY             2       // px/16 added each frame downward
#define FLAP_VEL           -20      // px/16 upward kick on press
#define VEL_MAX             20      // terminal velocity cap (downward)

// Pipe cap geometry
#define CAP_HEIGHT          4
#define CAP_OVERHANG        2

// Button debounce
#define DEBOUNCE_MS         30

// ── State ────────────────────────────────────────────────────
static Preferences preferences;

static int  tubeX[4];
static int  bottomTubeHeight[4];
static bool hasScored[4];

static unsigned int score      = 0;
static unsigned int highScore  = 0;
static unsigned int gameState  = 0;

static int birdYfp   = 28 << 4;
static int birdVel   = 0;
static int pipeSpeed = PIPE_SPEED_INIT;

static const int BIRD_X = 20;

static unsigned long lastFrameTime = 0;
static int  backPin     = -1;   // set by FlappyBird_SetBackPin()
static bool backPinLast = false;
static unsigned long backPinTime = 0;

// ── Button edge detection ─────────────────────────────────────
static bool buttonPressed(int pin, bool &lastState, unsigned long &lastTime) {
    bool raw = (digitalRead(pin) == LOW);
    unsigned long now = millis();
    if (raw && !lastState && (now - lastTime) > DEBOUNCE_MS) {
        lastState = true;
        lastTime  = now;
        return true;
    }
    if (!raw) lastState = false;
    return false;
}

static bool     btnMainLast = false;  unsigned long btnMainTime = 0;

// ── Drawing helpers ──────────────────────────────────────────

static void drawFilledRect(int x, int y, int w, int h) {
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > SCREEN_WIDTH  ? SCREEN_WIDTH  : x + w;
    int y2 = y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + h;
    for (int col = x1; col < x2; col++)
        for (int row = y1; row < y2; row++)
            OLED_DrawPoint((uint8_t)col, (uint8_t)row);
}

static void drawHLine(int x, int y, int w) {
    for (int col = x; col < x + w; col++)
        if (col >= 0 && col < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
            OLED_DrawPoint((uint8_t)col, (uint8_t)y);
}

static void drawVLine(int x, int y, int h) {
    for (int row = y; row < y + h; row++)
        if (x >= 0 && x < SCREEN_WIDTH && row >= 0 && row < SCREEN_HEIGHT)
            OLED_DrawPoint((uint8_t)x, (uint8_t)row);
}

static void drawRect(int x, int y, int w, int h) {
    drawHLine(x, y,         w);
    drawHLine(x, y + h - 1, w);
    drawVLine(x,         y, h);
    drawVLine(x + w - 1, y, h);
}

static void drawXbm(int x, int y, int bmpW, int bmpH, const unsigned char* bmp) {
    int bytesPerRow = (bmpW + 7) / 8;
    for (int row = 0; row < bmpH; row++) {
        for (int col = 0; col < bmpW; col++) {
            uint8_t b = pgm_read_byte(&bmp[row * bytesPerRow + col / 8]);
            if (b & (1 << (col % 8)))
                OLED_DrawPoint((uint8_t)(x + col), (uint8_t)(y + row));
        }
    }
}

static void drawNumber(uint8_t x, uint8_t y, unsigned int n) {
    char buf[12];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (n == 0) buf[--i] = '0';
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    OLED_ShowString8(x, y, &buf[i]);
}

// ── Pipe drawing ─────────────────────────────────────────────

static void drawPipe(int x, int gapTop, int gapBottom) {
    int capX = x - CAP_OVERHANG;
    int capW = TUBE_WIDTH + 2 * CAP_OVERHANG;

    int topBodyH = gapTop - CAP_HEIGHT;
    if (topBodyH > 0) {
        drawFilledRect(x, 0, TUBE_WIDTH, topBodyH);
        for (int row = 1; row < topBodyH - 1; row++)
            if (x + 1 < SCREEN_WIDTH)
                OLED_ClearPoint((uint8_t)(x + 1), (uint8_t)row);
    }
    if (gapTop - CAP_HEIGHT >= 0)
        drawFilledRect(capX, gapTop - CAP_HEIGHT, capW, CAP_HEIGHT);

    drawFilledRect(capX, gapBottom, capW, CAP_HEIGHT);
    int botBodyY = gapBottom + CAP_HEIGHT;
    int botBodyH = SCREEN_HEIGHT - botBodyY;
    if (botBodyH > 0) {
        drawFilledRect(x, botBodyY, TUBE_WIDTH, botBodyH);
        for (int row = botBodyY + 1; row < SCREEN_HEIGHT - 1; row++)
            if (x + 1 < SCREEN_WIDTH)
                OLED_ClearPoint((uint8_t)(x + 1), (uint8_t)row);
    }
}

// ── Misc ─────────────────────────────────────────────────────

static void saveHighScore() {
    preferences.begin("Flappy", false);
    preferences.putUInt("highScore", highScore);
    preferences.end();
}

static void resetTubes() {
    for (int i = 0; i < 4; i++) {
        tubeX[i]            = 128 + (i + 1) * TUBE_DISTANCE;
        bottomTubeHeight[i] = random(10, 30);
        hasScored[i]        = false;
    }
    pipeSpeed = PIPE_SPEED_INIT;
}

static void triggerDeath() {
    if (score > highScore) { highScore = score; saveHighScore(); }
    btnMainLast = true;
    gameState   = 2;
}

// ── Screens ──────────────────────────────────────────────────

static void handleStart() {
    birdYfp = 28 << 4;
    birdVel = 0;
    score   = 0;
    resetTubes();

    OLED_ShowString8(0, 4,  "Flappy Bird");
    drawXbm(64, 0, Building_width, Building_height, Building);
    drawXbm(BIRD_X, 28, Flappy_width, Flappy_height, Flappy);
    drawFilledRect(0, SCREEN_HEIGHT - 5, SCREEN_WIDTH, 5);
    OLED_ShowString8(0, 44, "Press to start");

    if (buttonPressed(BUTTON_PIN, btnMainLast, btnMainTime))
        gameState = 1;
}

static void handlePlay() {
    if (buttonPressed(BUTTON_PIN, btnMainLast, btnMainTime))
        birdVel = FLAP_VEL;

    birdVel += GRAVITY;
    if (birdVel > VEL_MAX) birdVel = VEL_MAX;
    birdYfp += birdVel;
    int birdY = birdYfp >> 4;

    for (int i = 0; i < 4; i++) {
        tubeX[i] -= pipeSpeed;

        if (tubeX[i] + TUBE_WIDTH < BIRD_X && !hasScored[i]) {
            score++;
            hasScored[i] = true;
            if (score % PIPE_SPEED_EVERY == 0 && pipeSpeed < PIPE_SPEED_MAX)
                pipeSpeed += PIPE_SPEED_STEP;
        }

        if (tubeX[i] + TUBE_WIDTH + CAP_OVERHANG < 0) {
            bottomTubeHeight[i] = random(10, 30);
            tubeX[i]             = 128;
            hasScored[i]         = false;
        }
    }

    for (int i = 0; i < 4; i++) {
        int gapTop    = bottomTubeHeight[i];
        int gapBottom = gapTop + PATH_WIDTH;
        drawPipe(tubeX[i], gapTop, gapBottom);
    }

    drawXbm(BIRD_X, birdY, Flappy_width, Flappy_height, Flappy);
    drawNumber(3, 0, score);
    drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (birdY + Flappy_height >= SCREEN_HEIGHT || birdY < 0) {
        triggerDeath(); return;
    }

    int birdRight  = BIRD_X + Flappy_width  - 1;
    int birdBottom = birdY  + Flappy_height - 1;

    for (int i = 0; i < 4; i++) {
        int gapTop    = bottomTubeHeight[i];
        int gapBottom = gapTop + PATH_WIDTH;
        int pipeLeft  = tubeX[i];
        int pipeRight = tubeX[i] + TUBE_WIDTH - 1;
        int capLeft   = tubeX[i] - CAP_OVERHANG;
        int capRight  = tubeX[i] + TUBE_WIDTH - 1 + CAP_OVERHANG;

        if (birdRight >= pipeLeft && BIRD_X <= pipeRight) {
            if (birdY < gapTop || birdBottom > gapBottom) {
                triggerDeath(); return;
            }
        }
        if (birdRight >= capLeft && BIRD_X <= capRight) {
            if (birdBottom >= gapTop - CAP_HEIGHT && birdY < gapTop) {
                triggerDeath(); return;
            }
            if (birdY <= gapBottom + CAP_HEIGHT - 1 && birdBottom > gapBottom) {
                triggerDeath(); return;
            }
        }
    }
}

static void handleScore() {
    OLED_ShowString8(0,  0, "Score: ");   drawNumber(42,  0, score);
    OLED_ShowString8(0, 14, "Best:  ");   drawNumber(42, 14, highScore);
    OLED_ShowString8(2, 32, "Select to restart");
    OLED_ShowString8(2, 44, "BACK to exit");

    if (buttonPressed(BUTTON_PIN, btnMainLast, btnMainTime))
        gameState = 0;
}

// ── Public API ───────────────────────────────────────────────

void FlappyBird_SetBackPin(int pin) {
    backPin = pin;
}

void FlappyBird_Init(void) {
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Only load high score once on first init; preserve it across re-entries
    static bool prefsLoaded = false;
    if (!prefsLoaded) {
        preferences.begin("Flappy", false);
        highScore = preferences.getUInt("highScore", 0);
        preferences.end();
        prefsLoaded = true;
    }

    // Reset game state for a fresh run
    gameState = 0;
    score     = 0;
    birdYfp   = 28 << 4;
    birdVel   = 0;

    for (int i = 0; i < 4; i++) {
        tubeX[i]            = 128 + i * TUBE_DISTANCE;
        bottomTubeHeight[i] = random(10, 30);
        hasScored[i]        = false;
    }

    btnMainLast   = (digitalRead(BUTTON_PIN) == LOW);
    btnMainTime   = millis();
    lastFrameTime = millis();

    if (backPin != -1) {
        pinMode(backPin, INPUT_PULLUP);
        backPinLast = (digitalRead(backPin) == LOW);
        backPinTime = millis();
    }
}

bool FlappyBird_Loop(void) {
    // Check back button — one clean press exits immediately
    if (backPin != -1 && buttonPressed(backPin, backPinLast, backPinTime)) {
        OLED_BufferClear();
        OLED_Flush();
        return true;   // caller should exit back to menu
    }

    // Block until next frame is due — this is the key fix
    unsigned long now = millis();
    if (now - lastFrameTime < FRAME_MS) {
        delay(FRAME_MS - (now - lastFrameTime));
    }
    lastFrameTime = millis();

    OLED_BufferClear();

    switch (gameState) {
        case 0: handleStart(); break;
        case 1: handlePlay();  break;
        default: handleScore(); break;
    }

    OLED_Flush();
    return false;  // still running
}
