#include "pad.h"
#include <kernel.h>
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char padBuffer[2][256] ALIGNED(64);
static unsigned int prevInputs[2] = {0, 0};

// Initializes gamepad input driver
void initPad() {
  padInit(0);
  padPortOpen(0, 0, padBuffer[0]);
  padPortOpen(1, 0, padBuffer[1]);

  prevInputs[0] = 0;
  prevInputs[1] = 0;
}

// Closes gamepad gamepad input driver
void closePad() {
  padPortClose(0, 0);
  padPortClose(1, 0);
  padEnd();
}

// Polls the gamepad and returns only changed inputs
int readPad(int port, int slot) {
  struct padButtonStatus buttons;
  uint32_t curInput, padData;

  curInput = 0;
  if (padRead(port, slot, &buttons) != 0) {
    padData = 0xffff ^ buttons.btns;

    curInput = padData & ~prevInputs[port];
    prevInputs[port] = padData;
  }

  return curInput;
}

// Polls the gamepad and returns currently pressed buttons
int pollPad(int port, int slot) {
  struct padButtonStatus buttons;
  if (padRead(port, slot, &buttons) != 0) {
    prevInputs[port] = 0xffff ^ buttons.btns;
    return prevInputs[port];
  }

  return 0;
}

// Blocks until input changes on any of the two gamepads.
// To capture press of any button, pass -1.
int waitForInput(int button) {
  int curInputs;
  while (1) {
    curInputs = (readPad(0, 0) | readPad(1, 0));
    if (curInputs & button)
      return curInputs;
  }
}


// Returns inputs on both gamepads
int pollInput() { return (pollPad(0, 0) | pollPad(1, 0)); }