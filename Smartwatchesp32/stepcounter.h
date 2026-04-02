#ifndef STEPCOUNTER_H
#define STEPCOUNTER_H

#include <stdint.h>

void sc_interrupt();
void sc_update_from_interrupt();
void sc_interrupt();

// Initialize the step counter (sensor + config)
void sc_init();

// Call this when interrupt fires (does processing + counting)
void sc_update();

// Get current step count
uint32_t sc_getSteps();
//int sc_getSteps();

// Reset step count to zero
void sc_resetSteps();


#endif