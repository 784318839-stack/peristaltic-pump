#ifndef PUMP_MACHINE_H
#define PUMP_MACHINE_H
#include "pump_shared.h"
void pump_machine_tick();
void pump_machine_transition(State newState);
#endif
