// RSDKv4 Dreamcast input backend: maple controllers -> engine input slots
//
// Mapping (per controller):
//   D-pad / analog stick -> Up/Down/Left/Right
//   A -> Button A (jump)     B -> Button B
//   X -> Button C            Y -> Button Y
//   L trigger -> Button L    R trigger -> Button R
//   Start -> Start
//   L+R+Start held together -> Select (dev menu access)
//
// Two devices are read. The first controller fills inputDevice[], which is what
// the whole engine and the 2013 menu have always used. The second fills
// inputDeviceP2[], which only ProcessObjectControl() (for an entity in
// CONTROLMODE_P2) and the keyDown[2] / keyPress[2] script variables look at -
// that is how a second player takes Tails over in Sonic & Tails. Nothing else
// sees it, so a single-pad setup behaves exactly as before.

#include "../RetroEngine.hpp"

#if RETRO_USING_KOS

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "DCCommon.hpp"

#define DC_ANALOG_DEADZONE (64)
#define DC_TRIGGER_PRESS   (64)

static inline void DC_SetButton(InputButton *dev, int id, bool held)
{
    if (held)
        dev[id].setHeld();
    else if (dev[id].hold)
        dev[id].setReleased();
}

static void DC_ReleaseAll(InputButton *dev)
{
    for (int i = 0; i < INPUT_BUTTONCOUNT; ++i) {
        if (dev[i].hold)
            dev[i].setReleased();
    }
}

// Fill one device from one controller. `primary` gates the two things that only
// make sense for player 1: the L+R+Start dev-menu chord, and the inputType hint
// the engine uses to decide it is being driven by a pad rather than a keyboard.
static void DC_ReadController(maple_device_t *mdev, InputButton *dev, bool primary)
{
    if (!mdev) {
        DC_ReleaseAll(dev);
        return;
    }

    cont_state_t *state = (cont_state_t *)maple_dev_status(mdev);
    if (!state)
        return;

    const bool up    = (state->buttons & CONT_DPAD_UP) || state->joyy < -DC_ANALOG_DEADZONE;
    const bool down  = (state->buttons & CONT_DPAD_DOWN) || state->joyy > DC_ANALOG_DEADZONE;
    const bool left  = (state->buttons & CONT_DPAD_LEFT) || state->joyx < -DC_ANALOG_DEADZONE;
    const bool right = (state->buttons & CONT_DPAD_RIGHT) || state->joyx > DC_ANALOG_DEADZONE;

    const bool trigL = state->ltrig > DC_TRIGGER_PRESS;
    const bool trigR = state->rtrig > DC_TRIGGER_PRESS;
    const bool start = (state->buttons & CONT_START) != 0;

    DC_SetButton(dev, INPUT_UP, up);
    DC_SetButton(dev, INPUT_DOWN, down);
    DC_SetButton(dev, INPUT_LEFT, left);
    DC_SetButton(dev, INPUT_RIGHT, right);

    DC_SetButton(dev, INPUT_BUTTONA, (state->buttons & CONT_A) != 0);
    DC_SetButton(dev, INPUT_BUTTONB, (state->buttons & CONT_B) != 0);
    DC_SetButton(dev, INPUT_BUTTONC, (state->buttons & CONT_X) != 0);
    DC_SetButton(dev, INPUT_BUTTONY, (state->buttons & CONT_Y) != 0);

    DC_SetButton(dev, INPUT_BUTTONL, trigL);
    DC_SetButton(dev, INPUT_BUTTONR, trigR);

    // Player 2 gets no dev-menu chord and no Start: pausing stays player 1's, so
    // a second pad cannot open the pause menu mid-run.
    DC_SetButton(dev, INPUT_START, primary && start && !(trigL && trigR));
    DC_SetButton(dev, INPUT_SELECT, primary && start && trigL && trigR);

    bool anyHold = false;
    for (int i = 0; i < INPUT_ANY; ++i) {
        if (dev[i].hold) {
            anyHold = true;
            break;
        }
    }
    DC_SetButton(dev, INPUT_ANY, anyHold);

    if (primary && anyHold)
        inputType = 1; // controller
}

void DC_ProcessInput()
{
    // Keep the Sonic 2 emblem on any connected VMU LCD. This lives on the input
    // path purely because it is the one DC-side hook that runs every frame from
    // the very first one, which is the point — the icon needs to be up during
    // the boot logos, long before any stage loads.
    DC_VmuTick();

    DC_ReadController(maple_enum_type(0, MAPLE_FUNC_CONTROLLER), inputDevice, true);

#if !RETRO_USE_ORIGINAL_CODE
    // The second controller, wherever it is plugged in — maple_enum_type indexes
    // by device order, not by port, so it is simply "the next pad after player
    // one's". Absent one, inputP2Connected stays false and IsInputSlotAssigned(2)
    // reports it, which is how the script hands Tails back to the AI.
    maple_device_t *p2 = maple_enum_type(1, MAPLE_FUNC_CONTROLLER);

    if ((p2 != NULL) != inputP2Connected) {
        inputP2Connected = p2 != NULL;
        printf("[DCInput] player 2 controller %s\n", inputP2Connected ? "connected" : "disconnected");
        fflush(stdout);
    }

    DC_ReadController(p2, inputDeviceP2, false);

    // One-shot: say so the first time player 2's pad actually does something.
    // Worth having because emulators (redream at least) report all four maple
    // ports as permanently connected, so the "connected" line above proves the
    // code path runs but not that a real second pad is bound to port B. This
    // only fires on genuine button activity, and only once.
    static bool p2InputSeen = false;
    if (!p2InputSeen) {
        for (int i = 0; i < INPUT_ANY; ++i) {
            if (inputDeviceP2[i].hold) {
                p2InputSeen = true;
                printf("[DCInput] player 2 input seen (button %d) — slot 2 is live\n", i);
                fflush(stdout);
                break;
            }
        }
    }
#endif
}

#endif // RETRO_USING_KOS
