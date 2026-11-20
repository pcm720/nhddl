#ifndef _DEVICES_PAD_H_
#define _DEVICES_PAD_H_

#include <libpad.h>

// Cross and Circle both used as the select action
#define PAD_SELECT_ACTION (PAD_CROSS | PAD_CIRCLE)

// Initializes gamepad input driver
void initPad();

// Closes gamepad gamepad input driver
void closePad();

// Blocks until input changes on any of the two gamepads.
// To capture press of any button, pass -1.
int waitForInput(int button);

// Returns inputs on both gamepads
int pollInput();

// Returns input changes
int readInput();

#endif
