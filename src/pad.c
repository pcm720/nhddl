#include <kernel.h>
#include <libpad.h>
#include <stdio.h>
#include <string.h>
#include <tamtypes.h>

#include "pad.h"

static unsigned char padArea[2][256] ALIGNED(64);
static unsigned int old_pad[2] = {0, 0};

// Initializes gamepad input driver
void gpadInit() {
  padInit(0);
  padPortOpen(0, 0, padArea[0]);
  padPortOpen(1, 0, padArea[1]);

  old_pad[0] = 0;
  old_pad[1] = 0;
}

// Closes gamepad gamepad input driver
void gpadClose() {
  padPortClose(0, 0);
  padPortClose(1, 0);
  padEnd();
}

int readPadStatus(int port, int slot) {
  struct padButtonStatus buttons;
  u32 new_pad, paddata;

  new_pad = 0;
  if (padRead(port, slot, &buttons) != 0) {
    paddata = 0xffff ^ buttons.btns;

    new_pad = paddata & ~old_pad[port];
    old_pad[port] = paddata;
  }

  return new_pad;
}

// Combines inputs from both gamepads
int readCombinedPadStatus() { return (readPadStatus(0, 0) | readPadStatus(1, 0)); }

// Blocks until a button is pressed on any of the two gamepads.
// To capture press of any button, pass -1.
int getInput(int button) {
  int new_pad;
  while (1) {
    new_pad = readCombinedPadStatus();
    if (new_pad & button)
      return new_pad;
  }
}
