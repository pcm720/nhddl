#include "ui/pad.h"
#include "ui/ui.h"
#include <kernel.h>
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char padBuffer[2][256] ALIGNED(64);
static unsigned int prevInputs[2] = {0, 0};
static struct padButtonStatus currentButtons[2];
static unsigned char currentButtonLengths[2] = {0, 0};

// Initializes gamepad input driver
void initPad() {
  padInit(0);
  padPortOpen(0, 0, padBuffer[0]);
  padPortOpen(1, 0, padBuffer[1]);

  prevInputs[0] = 0;
  prevInputs[1] = 0;
  memset(currentButtons, 0, sizeof(currentButtons));
  currentButtonLengths[0] = 0;
  currentButtonLengths[1] = 0;
}

// Closes gamepad gamepad input driver
void closePad() {
  padPortClose(0, 0);
  padPortClose(1, 0);
  padEnd();
}

// Polls the gamepad and returns only changed inputs
int readPad(int port, int slot) {
  struct padButtonStatus buttons = {0};
  uint32_t curInput, padData;

  curInput = 0;
  currentButtonLengths[port] = padRead(port, slot, &buttons);
  if (currentButtonLengths[port] != 0) {
    currentButtons[port] = buttons;
    padData = 0xffff ^ buttons.btns;

    curInput = padData & ~prevInputs[port];
    prevInputs[port] = padData;
  }

  return curInput;
}

// Polls the gamepad and returns currently pressed buttons
int pollPad(int port, int slot) {
  struct padButtonStatus buttons = {0};
  currentButtonLengths[port] = padRead(port, slot, &buttons);
  if (currentButtonLengths[port] != 0) {
    currentButtons[port] = buttons;
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
    // Sleep until the next frame instead of spinning at full speed,
    // letting background threads (async cover art loader) run
    uiWaitVSync();
  }
}

// Returns inputs on both gamepads
int pollInput() { return (pollPad(0, 0) | pollPad(1, 0)); }

// Returns vertical deflection of the left analog stick on gamepad 1
// (-127..127, 0 = centered/dead zone/not in DualShock mode)
int pollStickY() {
  // Reuse the sample already captured by pollInput(). Calling padInfoMode()
  // every frame makes old ROM PADMAN perform a synchronous configuration
  // RPC continuously, which can interfere with the controller's physical
  // Analog-mode toggle. The mode byte in the ordinary pad sample identifies
  // DualShock mode without another PADMAN transaction.
  if ((currentButtonLengths[0] < 8) ||
      ((currentButtons[0].mode >> 4) != PAD_TYPE_DUALSHOCK))
    return 0;

  int v = (int)currentButtons[0].ljoy_v - 128;
  if ((v > -48) && (v < 48)) // Dead zone
    return 0;
  return v;
}
