#ifndef _PAD_H_
#define _PAD_H_

// Initializes gamepad input driver
void initPad();

// Closes gamepad gamepad input driver
void closePad();

// Blocks until input changes on any of the two gamepads.
// To capture press of any button, pass -1.
int waitForInput(int button);

// Returns inputs on both gamepads
int pollInput();

#endif
