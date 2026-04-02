#include <Arduino.h>
#include "input.h"
#include "constants.h"

// Buttons are wired active-LOW (to GND) and use internal pull-ups
#define INPUT_MODE  INPUT_PULLUP
#define INPUT_STATE LOW

void input_setup() {
    pinMode(K_LEFT,  INPUT_MODE);
    pinMode(K_RIGHT, INPUT_MODE);
    pinMode(K_UP,    INPUT_MODE);
    pinMode(K_DOWN,  INPUT_MODE);
    pinMode(K_FIRE,  INPUT_MODE);
}

bool input_left()  { return digitalRead(K_LEFT)  == INPUT_STATE; }
bool input_right() { return digitalRead(K_RIGHT) == INPUT_STATE; }
bool input_up()    { return digitalRead(K_UP)    == INPUT_STATE; }
bool input_down()  { return digitalRead(K_DOWN)  == INPUT_STATE; }
bool input_fire()  { return digitalRead(K_FIRE)  == INPUT_STATE; }
