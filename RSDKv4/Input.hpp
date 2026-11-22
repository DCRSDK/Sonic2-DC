#ifndef INPUT_H
#define INPUT_H

enum InputButtons {
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_BUTTONA,
    INPUT_BUTTONB,
    INPUT_BUTTONC,
    INPUT_BUTTONX,
    INPUT_BUTTONY,
    INPUT_BUTTONZ,
    INPUT_BUTTONL,
    INPUT_BUTTONR,
    INPUT_START,
    INPUT_SELECT,
    INPUT_ANY,
    INPUT_BUTTONCOUNT,
};

struct InputData {
    bool up;
    bool down;
    bool left;
    bool right;
    bool A;
    bool B;
    bool C;
    bool X;
    bool Y;
    bool Z;
    bool L;
    bool R;
    bool start;
    bool select;
};

struct InputButton {
    bool press, hold;
    int keyMappings, contMappings;

    inline void setHeld()
    {
        press = !hold;
        hold  = true;
    }
    inline void setReleased()
    {
        press = false;
        hold  = false;
    }

    inline bool down() { return (press || hold); }
};

enum DefaultHapticIDs {
    HAPTIC_NONE = -2,
    HAPTIC_STOP = -1,
};

extern InputData keyPress;
extern InputData keyDown;

extern int touchDown[8];
extern int touchX[8];
extern int touchY[8];
extern int touchID[8];
extern float touchXF[8];
extern float touchYF[8];
extern int touches;

extern int hapticEffectNum;

#if !RETRO_USE_ORIGINAL_CODE
extern InputButton inputDevice[INPUT_BUTTONCOUNT];
extern int inputType;

// ---- second local player ------------------------------------------------
// A parallel device, deliberately kept SEPARATE from inputDevice[] so every
// existing reader - the 2013 menu especially - still sees exactly one
// controller and behaves as it always has. Only two things consume this:
// ProcessObjectControl() for an entity whose controlMode is CONTROLMODE_P2,
// and the keyDown[2] / keyPress[2] script variables that Sonic 2's
// Player2Object polls to notice a human taking Tails over.
//
// keyDownP2 / keyPressP2 are refreshed once per frame in ProcessInput().
// P1's equivalents are refreshed by the scripts themselves (CheckKeyDown),
// and nothing does that for P2, so the engine has to.
extern InputButton inputDeviceP2[INPUT_BUTTONCOUNT];
extern InputData keyDownP2;
extern InputData keyPressP2;
extern bool inputP2Connected; // is a second controller physically present

extern float LSTICK_DEADZONE;
extern float RSTICK_DEADZONE;
extern float LTRIGGER_DEADZONE;
extern float RTRIGGER_DEADZONE;

extern int mouseHideTimer;
extern int lastMouseX;
extern int lastMouseY;
#endif

#if !RETRO_USE_ORIGINAL_CODE
#if RETRO_USING_SDL2
// Easier this way
enum ExtraSDLButtons {
    SDL_CONTROLLER_BUTTON_ZL = SDL_CONTROLLER_BUTTON_MAX + 1,
    SDL_CONTROLLER_BUTTON_ZR,
    SDL_CONTROLLER_BUTTON_LSTICK_UP,
    SDL_CONTROLLER_BUTTON_LSTICK_DOWN,
    SDL_CONTROLLER_BUTTON_LSTICK_LEFT,
    SDL_CONTROLLER_BUTTON_LSTICK_RIGHT,
    SDL_CONTROLLER_BUTTON_RSTICK_UP,
    SDL_CONTROLLER_BUTTON_RSTICK_DOWN,
    SDL_CONTROLLER_BUTTON_RSTICK_LEFT,
    SDL_CONTROLLER_BUTTON_RSTICK_RIGHT,
    SDL_CONTROLLER_BUTTON_MAX_EXTRA,
};

void controllerInit(int controllerID);
void controllerClose(int controllerID);
#endif

#if RETRO_USING_SDL1
extern byte keyState[SDLK_LAST];

extern SDL_Joystick *controller;
#endif

void InitInputDevices();
void ReleaseInputDevices();

void ProcessInput();
#endif

void CheckKeyPress(InputData *input);
void CheckKeyDown(InputData *input);

int CheckTouchRect(float x1, float y1, float x2, float y2);
int CheckTouchRectMatrix(void *m, float x1, float y1, float x2, float y2);

#if RETRO_USE_HAPTICS
inline int GetHapticEffectNum()
{
    int num         = hapticEffectNum;
    hapticEffectNum = HAPTIC_NONE;
    return num;
}
void HapticEffect(int *id, int *a2, int *a3, int *a4);
#endif

#endif // !INPUT_H