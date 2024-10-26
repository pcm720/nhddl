#ifndef _PAD_H_
#define _PAD_H_

// Initializes gamepad input driver
void gpadInit();

// Closes gamepad gamepad input driver
void gpadClose();

// Blocks until a button is pressed on any of the two gamepads.
// To capture press of any button, pass -1.
int getInput(int button);

#endif
