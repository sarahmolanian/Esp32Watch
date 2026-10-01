#ifndef STEPCOUNTER_H
#define STEPCOUNTER_H

#include <stdint.h>
#include "esp_attr.h"


// Initialize the step counter (sensor + config)
void sc_init();

// Call this when interrupt fires (does processing + counting)
void sc_update();

// Get current step count
uint32_t sc_getSteps();
//int sc_getSteps();

// Reset step count to zero
void sc_resetSteps();
void sc_saveStepsToRTC();      // ← call before shutdown
void sc_restoreStepsFromRTC(); // ← call after wakeup
void sc_setSteps(uint32_t s);
#endif