/*
========================================================================

  DOOM RETRO
  The classic, refined DOOM source port. For Windows PC.
  Copyright (C) 2013-2014 Brad Harding.

  This file is part of DOOM RETRO.

  DOOM RETRO is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DOOM RETRO is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with DOOM RETRO. If not, see <http://www.gnu.org/licenses/>.

========================================================================
*/

#include "d_main.h"
#include "doomstat.h"
#include "hu_stuff.h"
#include "i_gamepad.h"
#include "i_system.h"
#include "i_tinttab.h"
#include "i_video.h"
#include "m_config.h"
#include "m_misc.h"
#include "SDL.h"
#include "SDL_syswm.h"
#include "s_sound.h"
#include "v_video.h"
#include "version.h"
#include "w_wad.h"
#include "z_zone.h"

// Window position:
char                    *windowposition = WINDOWPOSITION_DEFAULT;

SDL_Surface             *screen = NULL;
SDL_Surface             *screenbuffer = NULL;

#ifdef SDL20
SDL_Window              *sdl_window = NULL;
static SDL_Renderer     *sdl_renderer = NULL;
static SDL_Texture      *sdl_texture = NULL;
#endif

// palette
SDL_Color               palette[256];
static boolean          palette_to_set;

// Bit mask of mouse button state
static unsigned int     mouse_button_state = 0;

boolean                 novert = NOVERT_DEFAULT;

static int              buttons[MAX_MOUSE_BUTTONS + 1] = { 0, 1, 4, 2, 8, 16, 32, 64, 128 };

// Fullscreen width and height
int                     screenwidth = SCREENWIDTH_DEFAULT;
int                     screenheight = SCREENHEIGHT_DEFAULT;

// Window width and height
int                     windowwidth = WINDOWWIDTH_DEFAULT;
int                     windowheight = WINDOWHEIGHT_DEFAULT;

// Run in full screen mode?
int                     fullscreen = FULLSCREEN_DEFAULT;

boolean                 widescreen = WIDESCREEN_DEFAULT;
boolean                 returntowidescreen = false;
boolean                 hud = HUD_DEFAULT;

// Flag indicating whether the screen is currently visible:
// when the screen isn't visible, don't render the screen
boolean                 screenvisible;

boolean                 window_focused;

// Empty mouse cursor
static SDL_Cursor       *cursors[2];

// Window resize state.
boolean                 need_resize = false;
unsigned int            resize_h;

int                     desktopwidth;
int                     desktopheight;

char                    *videodriver = VIDEODRIVER_DEFAULT;
char                    envstring[255];

static int              width;
static int              height;
static int              stepx;
static int              stepy;
static int              startx;
static int              starty;
static int              pitch;

static int              blitwidth = SCREENWIDTH << FRACBITS;
static int              blitheight = SCREENHEIGHT << FRACBITS;

byte                    *pixels;

byte                    *rows[SCREENHEIGHT];

boolean                 keys[UCHAR_MAX];

byte                    gammatable[GAMMALEVELS][256];

float                   gammalevels[GAMMALEVELS] =
{
    // Darker
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f,

    // No gamma correction
    1.0f,

    // Lighter
    1.05f, 1.10f, 1.15f, 1.20f, 1.25f, 1.30f, 1.35f, 1.40f, 1.45f, 1.50f,
    1.55f, 1.60f, 1.65f, 1.70f, 1.75f, 1.80f, 1.85f, 1.90f, 1.95f, 2.0f
};

// Gamma correction level to use
int                     gammaindex;
float                   gamma = GAMMA_DEFAULT;

float                   saturation = SATURATION_DEFAULT;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.
float                   mouse_acceleration = MOUSEACCELERATION_DEFAULT;
int                     mouse_threshold = MOUSETHRESHOLD_DEFAULT;

int                     capslock;
boolean                 alwaysrun = ALWAYSRUN_DEFAULT;

static void ApplyWindowResize(int height);
static void SetWindowPositionVars(void);

boolean MouseShouldBeGrabbed(void)
{
    // if the window doesn't have focus, never grab it
    if (!window_focused)
        return false;

    // always grab the mouse when full screen (dont want to
    // see the mouse pointer)
    if (fullscreen)
        return true;

    // when menu is active or game is paused, release the mouse
    if (menuactive || paused)
        return false;

    // only grab mouse when playing levels
    return (gamestate == GS_LEVEL);
}

// Update the value of window_focused when we get a focus event
//
// We try to make ourselves be well-behaved: the grab on the mouse
// is removed if we lose focus (such as a popup window appearing),
// and we dont move the mouse around if we aren't focused either.
static void UpdateFocus(void)
{
    static boolean      alreadypaused = false;

#ifdef SDL20
    Uint32              state = SDL_GetWindowFlags(sdl_window);

    // Should the screen be grabbed?
    screenvisible = (state & SDL_WINDOW_SHOWN);

    // We should have input (keyboard) focus and be visible
    // (not minimized)
    window_focused = ((state & SDL_WINDOW_INPUT_FOCUS) && screenvisible);
#else
    Uint8               state = SDL_GetAppState();

    screenvisible = (state & SDL_APPACTIVE);

    window_focused = ((state & SDL_APPINPUTFOCUS) && screenvisible);
#endif

    if (!window_focused && !menuactive && gamestate == GS_LEVEL)
    {
        if (paused)
            alreadypaused = true;
        else
        {
            alreadypaused = false;
            sendpause = true;
            blurred = false;
        }
    }
}

// Show or hide the mouse cursor. We have to use different techniques
// depending on the OS.
static void SetShowCursor(boolean show)
{
    // On Windows, using SDL_ShowCursor() adds lag to the mouse input,
    // so work around this by setting an invisible cursor instead. On
    // other systems, it isn't possible to change the cursor, so this
    // hack has to be Windows-only. (Thanks to entryway for this)
#ifdef WIN32
    SDL_SetCursor(cursors[show]);
#else
    SDL_ShowCursor(show);
#endif

    // When the cursor is hidden, grab the input.
#ifdef SDL20
    SDL_SetRelativeMouseMode(!show);
#else
    SDL_WM_GrabInput(!show);
#endif
}

int translatekey[] =
{
#ifdef SDL20
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1',
    '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_ENTER, KEY_ESCAPE,
    KEY_BACKSPACE, KEY_TAB, ' ', KEY_MINUS, KEY_EQUALS, '[', ']', '\\', '\\',
    ';', '\'', '`', ',', '.', '/', KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3,
    KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    0, KEY_SCRLCK, KEY_PAUSE, KEY_INS, KEY_HOME, KEY_PGUP, KEY_DEL, KEY_END,
    KEY_PGDN, KEY_RIGHTARROW, KEY_LEFTARROW, KEY_DOWNARROW, KEY_UPARROW,
    KEY_NUMLOCK, KEYP_DIVIDE, KEYP_MULTIPLY, KEYP_MINUS, KEYP_PLUS, KEYP_ENTER,
    KEYP_1, KEYP_2, KEYP_3, KEYP_4, KEYP_5, KEYP_6, KEYP_7, KEYP_8, KEYP_9,
    KEYP_0, KEYP_PERIOD, 0, 0, 0, KEYP_EQUALS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_RCTRL, KEY_RSHIFT, KEY_RALT, 0, KEY_RCTRL,
    KEY_RSHIFT, KEY_RALT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#else
    0, 0, 0, 0, 0, 0, 0, 0, KEY_BACKSPACE, KEY_TAB, 0, 0, 0, KEY_ENTER, 0, 0,
    0, 0, 0, KEY_PAUSE, 0, 0, 0, 0, 0, 0, 0, KEY_ESCAPE, 0, 0, 0, 0, ' ', '!',
    '\"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',', KEY_MINUS, '.',
    '/', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<',
    KEY_EQUALS, '>', '?', '@', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
    'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', '[', '\\', ']', '^', '_', '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
    'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', 0, 0, 0, 0, KEY_DEL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEYP_0, KEYP_1, KEYP_2,
    KEYP_3, KEYP_4, KEYP_5, KEYP_6, KEYP_7, KEYP_8, KEYP_9, KEYP_PERIOD,
    KEYP_DIVIDE, KEYP_MULTIPLY, KEYP_MINUS, KEYP_PLUS, KEYP_ENTER, KEYP_EQUALS,
    KEY_UPARROW, KEY_DOWNARROW, KEY_RIGHTARROW, KEY_LEFTARROW, KEY_INS,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_F1, KEY_F2, KEY_F3, KEY_F4,
    KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, 0, 0, 0,
    0, 0, 0, KEY_NUMLOCK, KEY_CAPSLOCK, KEY_SCRLCK, KEY_RSHIFT, KEY_RSHIFT,
    KEY_RCTRL, KEY_RCTRL, KEY_RALT, KEY_RALT, KEY_RALT, KEY_RALT, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
#endif
};

int TranslateKey2(int key)
{
    switch (key)
    {

#ifdef SDL20
        case KEY_LEFTARROW:    return SDL_SCANCODE_LEFT;
        case KEY_RIGHTARROW:   return SDL_SCANCODE_RIGHT;
        case KEY_DOWNARROW:    return SDL_SCANCODE_DOWN;
        case KEY_UPARROW:      return SDL_SCANCODE_UP;
        case KEY_ESCAPE:       return SDL_SCANCODE_ESCAPE;
        case KEY_ENTER:        return SDL_SCANCODE_RETURN;
        case KEY_TAB:          return SDL_SCANCODE_TAB;
        case KEY_F1:           return SDL_SCANCODE_F1;
        case KEY_F2:           return SDL_SCANCODE_F2;
        case KEY_F3:           return SDL_SCANCODE_F3;
        case KEY_F4:           return SDL_SCANCODE_F4;
        case KEY_F5:           return SDL_SCANCODE_F5;
        case KEY_F6:           return SDL_SCANCODE_F6;
        case KEY_F7:           return SDL_SCANCODE_F7;
        case KEY_F8:           return SDL_SCANCODE_F8;
        case KEY_F9:           return SDL_SCANCODE_F9;
        case KEY_F10:          return SDL_SCANCODE_F10;
        case KEY_F11:          return SDL_SCANCODE_F11;
        case KEY_F12:          return SDL_SCANCODE_F12;
        case KEY_BACKSPACE:    return SDL_SCANCODE_BACKSPACE;
        case KEY_DEL:          return SDL_SCANCODE_DELETE;
        case KEY_PAUSE:        return SDL_SCANCODE_PAUSE;
        case KEY_EQUALS:       return SDL_SCANCODE_EQUALS;
        case KEY_MINUS:        return SDL_SCANCODE_MINUS;
        case KEY_RSHIFT:       return SDL_SCANCODE_RSHIFT;
        case KEY_RCTRL:        return SDL_SCANCODE_RCTRL;
        case KEY_RALT:         return SDL_SCANCODE_RALT;
        case KEY_CAPSLOCK:     return SDL_SCANCODE_CAPSLOCK;
        case KEY_SCRLCK:       return SDL_SCANCODE_SCROLLLOCK;
        case KEYP_0:           return SDL_SCANCODE_KP_0;
        case KEYP_1:           return SDL_SCANCODE_KP_1;
        case KEYP_3:           return SDL_SCANCODE_KP_3;
        case KEYP_5:           return SDL_SCANCODE_KP_5;
        case KEYP_7:           return SDL_SCANCODE_KP_7;
        case KEYP_9:           return SDL_SCANCODE_KP_9;
        case KEYP_PERIOD:      return SDL_SCANCODE_KP_PERIOD;
        case KEYP_MULTIPLY:    return SDL_SCANCODE_KP_MULTIPLY;
        case KEYP_DIVIDE:      return SDL_SCANCODE_KP_DIVIDE;
        case KEY_INS:          return SDL_SCANCODE_INSERT;
        case KEY_NUMLOCK:      return SDL_SCANCODE_NUMLOCKCLEAR;
#else
        case KEY_LEFTARROW:    return SDLK_LEFT;
        case KEY_RIGHTARROW:   return SDLK_RIGHT;
        case KEY_DOWNARROW:    return SDLK_DOWN;
        case KEY_UPARROW:      return SDLK_UP;
        case KEY_ESCAPE:       return SDLK_ESCAPE;
        case KEY_ENTER:        return SDLK_RETURN;
        case KEY_TAB:          return SDLK_TAB;
        case KEY_F1:           return SDLK_F1;
        case KEY_F2:           return SDLK_F2;
        case KEY_F3:           return SDLK_F3;
        case KEY_F4:           return SDLK_F4;
        case KEY_F5:           return SDLK_F5;
        case KEY_F6:           return SDLK_F6;
        case KEY_F7:           return SDLK_F7;
        case KEY_F8:           return SDLK_F8;
        case KEY_F9:           return SDLK_F9;
        case KEY_F10:          return SDLK_F10;
        case KEY_F11:          return SDLK_F11;
        case KEY_F12:          return SDLK_F12;
        case KEY_BACKSPACE:    return SDLK_BACKSPACE;
        case KEY_DEL:          return SDLK_DELETE;
        case KEY_PAUSE:        return SDLK_PAUSE;
        case KEY_EQUALS:       return SDLK_EQUALS;
        case KEY_MINUS:        return SDLK_MINUS;
        case KEY_RSHIFT:       return SDLK_RSHIFT;
        case KEY_RCTRL:        return SDLK_RCTRL;
        case KEY_RALT:         return SDLK_RALT;
        case KEY_CAPSLOCK:     return SDLK_CAPSLOCK;
        case KEY_SCRLCK:       return SDLK_SCROLLOCK;
        case KEYP_0:           return SDLK_KP0;
        case KEYP_1:           return SDLK_KP1;
        case KEYP_3:           return SDLK_KP3;
        case KEYP_5:           return SDLK_KP5;
        case KEYP_7:           return SDLK_KP7;
        case KEYP_9:           return SDLK_KP9;
        case KEYP_PERIOD:      return SDLK_KP_PERIOD;
        case KEYP_MULTIPLY:    return SDLK_KP_MULTIPLY;
        case KEYP_DIVIDE:      return SDLK_KP_DIVIDE;
        case KEY_INS:          return SDLK_INSERT;
        case KEY_NUMLOCK:      return SDLK_NUMLOCK;
#endif

        default:               return key;
    }
}

boolean keystate(int key)
{
#ifdef SDL20
    const Uint8 *keystate = SDL_GetKeyboardState(NULL);
#else
    Uint8       *keystate = SDL_GetKeyState(NULL);
#endif

    return keystate[TranslateKey2(key)];
}

void I_SaveWindowPosition(void)
{
#ifdef WIN32
    if (!fullscreen)
    {
        static SDL_SysWMinfo    info;
        HWND                    hwnd;
        RECT                    r;

        SDL_VERSION(&info.version);

#ifdef SDL20
        SDL_GetWindowWMInfo(sdl_window, &info);
        hwnd = info.info.win.window;
#else
        SDL_GetWMInfo(&info);
        hwnd = info.window;
#endif

        GetWindowRect(hwnd, &r);
        sprintf(windowposition, "%i,%i", r.left + 8, r.top + 30);
    }
#endif
}

void I_ShutdownGraphics(void)
{
    SetShowCursor(true);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void I_ShutdownKeyboard(void)
{
#ifdef WIN32
    if ((GetKeyState(VK_CAPITAL) & 0x0001) && !capslock)
    {
        keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY, (uintptr_t)0);
        keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, (uintptr_t)0);
    }
#endif
}

static int AccelerateMouse(int val)
{
    if (val < 0)
        return -AccelerateMouse(-val);

    if (val > mouse_threshold)
        return (int)((val - mouse_threshold) * mouse_acceleration + mouse_threshold);
    else
        return val;
}

// Warp the mouse back to the middle of the screen
static void CenterMouse(void)
{
    // Warp to the screen center
#ifdef SDL20
    SDL_WarpMouseInWindow(sdl_window, screen->w / 2, screen->h / 2);
#else
    SDL_WarpMouse(screen->w / 2, screen->h / 2);
#endif

    // Clear any relative movement caused by warping
    SDL_PumpEvents();
    SDL_GetRelativeMouseState(NULL, NULL);
}

boolean altdown = false;
boolean waspaused = false;

void I_GetEvent(void)
{
    SDL_Event           sdlevent;
    event_t             ev;

#ifdef SDL20
    SDL_Scancode        key;
#else
    SDLKey              key;
#endif

    while (SDL_PollEvent(&sdlevent))
    {
        switch (sdlevent.type)
        {
            case SDL_KEYDOWN:
                ev.type = ev_keydown;

#ifdef SDL20
                key = sdlevent.key.keysym.scancode;
#else
                key = sdlevent.key.keysym.sym;
#endif

                ev.data1 = translatekey[key];
                ev.data2 = key;

                altdown = (sdlevent.key.keysym.mod & KMOD_ALT);

                if (altdown && ev.data1 == KEY_TAB)
                    ev.data1 = ev.data2 = 0;

                if (!isdigit(ev.data2))
                    idclev = idmus = false;

                if (idbehold && keys[ev.data2])
                {
                    HU_clearMessages();
                    idbehold = false;
                }

                if (ev.data1)
                    D_PostEvent(&ev);
                break;

            case SDL_KEYUP:
                ev.type = ev_keyup;

#ifdef SDL20
                ev.data1 = translatekey[sdlevent.key.keysym.scancode];
#else
                ev.data1 = translatekey[sdlevent.key.keysym.sym];
#endif

                ev.data2 = 0;

                altdown = (sdlevent.key.keysym.mod & KMOD_ALT);
                keydown = 0;

                if (ev.data1)
                    D_PostEvent(&ev);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (mouseSensitivity || menuactive)
                {
                    idclev = false;
                    idmus = false;
                    if (idbehold)
                    {
                        HU_clearMessages();
                        idbehold = false;
                    }
                    ev.type = ev_mouse;
                    mouse_button_state |= buttons[sdlevent.button.button];
                    ev.data1 = mouse_button_state;
                    ev.data2 = 0;
                    ev.data3 = 0;
                    D_PostEvent(&ev);
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (mouseSensitivity || menuactive)
                {
                    keydown = 0;
                    ev.type = ev_mouse;
                    mouse_button_state &= ~buttons[sdlevent.button.button];
                    ev.data1 = mouse_button_state;
                    ev.data2 = 0;
                    ev.data3 = 0;
                    D_PostEvent(&ev);
                }
                break;

            case SDL_JOYBUTTONUP:
                keydown = 0;
                break;

            case SDL_QUIT:
                if (!quitting)
                {
                    keydown = 0;
                    if (paused)
                    {
                        paused = false;
                        waspaused = true;
                    }
                    S_StartSound(NULL, sfx_swtchn);
                    M_QuitDOOM();
                }
                break;

#ifdef SDL12
            case SDL_ACTIVEEVENT:
                // need to update our focus state
                UpdateFocus();
                break;

            case SDL_VIDEOEXPOSE:
                palette_to_set = true;
                break;

            case SDL_VIDEORESIZE:
                if (!fullscreen)
                {
                    need_resize = true;
                    resize_h = sdlevent.resize.h;
                    break;
                }
#endif

#ifdef WIN32
            case SDL_SYSWMEVENT:

#ifdef SDL20
                if (sdlevent.syswm.msg->msg.win.msg == WM_MOVE)
#else
                if (sdlevent.syswm.msg->msg == WM_MOVE)
#endif

                {
                    I_SaveWindowPosition();
                    SetWindowPositionVars();
                    M_SaveDefaults();
                }
                break;
#endif

#ifdef SDL20
            case SDL_WINDOWEVENT:
                switch (sdlevent.window.event)
                {
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        // need to update our focus state
                        UpdateFocus();
                        break;

                    case SDL_WINDOWEVENT_EXPOSED:
                        palette_to_set = true;
                        break;

                    case SDL_WINDOWEVENT_RESIZED:
                        if (!fullscreen)
                        {
                            need_resize = true;
                            resize_h = sdlevent.window.data2;
                            break;
                        }
                        break;
                }
                break;
#endif

            default:
                break;
        }
    }
}

static void I_ReadMouse(void)
{
    int         x;
    int         y;
    event_t     ev;

    SDL_PumpEvents();

    ev.type = ev_mouse;
    ev.data1 = SDL_GetRelativeMouseState(&x, &y);
    ev.data2 = AccelerateMouse(x);
    ev.data3 = (novert ? 0 : -AccelerateMouse(y));

    D_PostEvent(&ev);

    if (MouseShouldBeGrabbed())
        CenterMouse();
}

//
// I_StartTic
//
void I_StartTic(void)
{
    I_GetEvent();
    if (mouseSensitivity || menuactive)
    {
        I_ReadMouse();
        gamepadfunc();
    }
}

boolean currently_grabbed = false;

static void UpdateGrab(void)
{
    boolean     grab = MouseShouldBeGrabbed();

    if (grab && !currently_grabbed)
        SetShowCursor(false);
    else if (!grab && currently_grabbed)
    {
        SetShowCursor(true);

#ifdef SDL20
        SDL_WarpMouseInWindow(sdl_window, screen->w - 16, screen->h - 16);
#else
        SDL_WarpMouse(screen->w - 16, screen->h - 16);
#endif

        SDL_PumpEvents();
        SDL_GetRelativeMouseState(NULL, NULL);
    }

    currently_grabbed = grab;
}

static __forceinline void blit(fixed_t width, fixed_t height)
{
    fixed_t     i = 0;
    fixed_t     y = starty;

    do
    {
        byte    *dest = pixels + i;
        byte    *src = *(rows + (y >> FRACBITS));
        fixed_t x = startx;

        do
            *dest++ = *(src + (x >> FRACBITS));
        while ((x += stepx) < width);

        i += pitch;
    } while ((y += stepy) < height);
}

SDL_Rect dest_rect;

//
// I_FinishUpdate
//
void I_FinishUpdate(void)
{
    if (need_resize)
    {
        ApplyWindowResize(resize_h);
        need_resize = false;
        palette_to_set = true;
    }

    UpdateGrab();

    // Don't update the screen if the window isn't visible.
    // Not doing this breaks under Windows when we alt-tab away
    // while fullscreen.
    if (!screenvisible)
        return;

    if (palette_to_set)
    {

#ifdef SDL20
        SDL_SetPaletteColors(screenbuffer->format->palette, palette, 0, 256);
        if (sdl_texture)
            SDL_DestroyTexture(sdl_texture);
        sdl_texture = SDL_CreateTextureFromSurface(sdl_renderer, screenbuffer);
#else
        SDL_SetColors(screenbuffer, palette, 0, 256);
#endif

        palette_to_set = false;
    }

    // draw to screen
    blit(blitwidth, blitheight);

#ifdef SDL20
    SDL_BlitSurface(screenbuffer, NULL, screen, NULL);
    SDL_UpdateTexture(sdl_texture, NULL, screen->pixels, screen->pitch);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer); 
#else
    SDL_FillRect(screen, NULL, 0);
    SDL_BlitSurface(screenbuffer, NULL, screen, &dest_rect);
    SDL_Flip(screen);
#endif
}

//
// I_ReadScreen
//
void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
void I_SetPalette(byte *doompalette)
{
    int i;

    for (i = 0; i < 256; ++i)
    {
        byte    r = *doompalette++;
        byte    g = *doompalette++;
        byte    b = *doompalette++;
        double  p = sqrt(r * r * 0.299 + g * g * 0.587 + b * b * 0.114);

        palette[i].r = gammatable[gammaindex][(byte)(p + (r - p) * saturation)];
        palette[i].g = gammatable[gammaindex][(byte)(p + (g - p) * saturation)];
        palette[i].b = gammatable[gammaindex][(byte)(p + (b - p) * saturation)];

#ifdef SDL20
        palette[i].a = 255;
#endif
    }

    palette_to_set = true;
}

static void CreateCursors(void)
{
    static Uint8 empty_cursor_data = 0;

    // Save the default cursor so it can be recalled later
    cursors[1] = SDL_GetCursor();

    // Create an empty cursor
    cursors[0] = SDL_CreateCursor(&empty_cursor_data, &empty_cursor_data, 1, 1, 0, 0);
}

static void SetWindowPositionVars(void)
{
    char        buf[64];
    int         x, y;

    if (sscanf(windowposition, "%i,%i", &x, &y) == 2)
    {
        if (x < 0)
            x = 0;
        else if (x > desktopwidth)
            x = desktopwidth - 16;
        if (y < 0)
            y = 0;
        else if (y > desktopheight)
            y = desktopheight - 16;
        sprintf(buf, "SDL_VIDEO_WINDOW_POS=%i,%i", x, y);
        putenv(buf);
    }
    else
        putenv("SDL_VIDEO_CENTERED=1");
}

static void SetVideoMode(void)
{
#ifdef SDL20
    SDL_Rect            displaybounds;

    SDL_GetDisplayBounds(0, &displaybounds);
    desktopwidth = displaybounds.w;
    desktopheight = displaybounds.h;
#else
    SDL_VideoInfo       *videoinfo = (SDL_VideoInfo *)SDL_GetVideoInfo();

    desktopwidth = videoinfo->current_w;
    desktopheight = videoinfo->current_h;
#endif

    if (fullscreen)
    {
        width = screenwidth;
        height = screenheight;
        if (!width || !height)
        {
            width = desktopwidth;
            height = desktopheight;
            screenwidth = 0;
            screenheight = 0;
            M_SaveDefaults();
        }

#ifdef SDL20
        if (sdl_window)
            SDL_DestroyWindow(sdl_window);
        sdl_window = SDL_CreateWindow(gamedescription, SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_FULLSCREEN);
        screen = SDL_GetWindowSurface(sdl_window);
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC);
#else
        screen = SDL_SetVideoMode(width, height, 0,
            SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
#endif

        if (!screen)
        {
            screenwidth = 0;
            screenheight = 0;
            M_SaveDefaults();

#ifdef SDL20
            if (sdl_window)
                SDL_DestroyWindow(sdl_window);
            sdl_window = SDL_CreateWindow(gamedescription, SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED, desktopwidth, desktopheight, SDL_WINDOW_FULLSCREEN);
            screen = SDL_GetWindowSurface(sdl_window);
            sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC);
#else
            screen = SDL_SetVideoMode(desktopwidth, desktopheight, 0,
                SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
#endif

            if (!screen)
                I_Error("Error setting video mode %ix%i: %s\n", width, height, SDL_GetError());
        }

        height = screen->h;
        width = height * 4 / 3;
        width += (width & 1);

        if (width > screen->w)
        {
            width = screen->w;
            height = width * 3 / 4;
            height += (height & 1);
        }
    }
    else
    {
        if (windowheight > desktopheight)
        {
            windowheight = desktopheight;
            windowwidth = windowheight * 4 / 3;
            windowwidth += (windowwidth & 1);
            M_SaveDefaults();
        }
        height = MAX(ORIGINALWIDTH * 3 / 4, windowheight);
        width = height * 4 / 3;
        width += (width & 1);

        if (width > windowwidth)
        {
            width = windowwidth;
            height = width * 3 / 4;
            height += (height & 1);
        }

        SetWindowPositionVars();

#ifdef SDL20
        if (sdl_window)
            SDL_DestroyWindow(sdl_window);
        sdl_window = SDL_CreateWindow(gamedescription, SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, windowwidth, windowheight, SDL_WINDOW_RESIZABLE);
        screen = SDL_GetWindowSurface(sdl_window);
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC);
#else
        screen = SDL_SetVideoMode(windowwidth, windowheight, 0,
            SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_RESIZABLE);
#endif

        widescreen = false;
    }

#ifdef SDL20
    screenbuffer = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0);
    sdl_texture = SDL_CreateTextureFromSurface(sdl_renderer, screenbuffer);
    SDL_RenderSetLogicalSize(sdl_renderer, screenbuffer->w, screenbuffer->h);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderPresent(sdl_renderer); 
#else
    screenbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);
#endif

    pitch = screenbuffer->pitch;
    pixels = (byte *)screenbuffer->pixels;

    stepx = (SCREENWIDTH << FRACBITS) / width;
    stepy = (SCREENHEIGHT << FRACBITS) / height;

    startx = stepx - 1;
    starty = stepy - 1;

    dest_rect.x = (screen->w - screenbuffer->w) / 2;
    dest_rect.y = (screen->h - screenbuffer->h) / 2;
}

void ToggleWideScreen(boolean toggle)
{
    if ((double)screen->w / screen->h < 1.6)
    {
        widescreen = returntowidescreen = false;
        return;
    }

    if (toggle)
    {
        if (!dest_rect.x && !dest_rect.y)
            return;

        widescreen = true;

        if (returntowidescreen && screensize == 8)
        {
            screensize = 7;
            R_SetViewSize(screensize);
        }

        width = screen->w;
        height = screen->h + (int)((double)screen->h * 32 / (ORIGINALHEIGHT - 32) + 1.5);

        blitheight = (SCREENHEIGHT - SBARHEIGHT) << FRACBITS;
    }
    else
    {
        widescreen = false;

        height = screen->h;
        width = height * 4 / 3;
        width += (width & 1);

        blitheight = SCREENHEIGHT << FRACBITS;
    }

    returntowidescreen = false;
#ifdef SDL20
    SDL_RenderSetLogicalSize(sdl_renderer, width, height);
#else
    screenbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);
#endif

    pitch = screenbuffer->pitch;
    pixels = (byte *)screenbuffer->pixels;

    stepx = (SCREENWIDTH << FRACBITS) / width;
    stepy = (SCREENHEIGHT << FRACBITS) / height;

    startx = stepx - 1;
    starty = stepy - 1;

    if (widescreen)
        dest_rect.x = dest_rect.y = 0;
    else
    {
        dest_rect.x = (screen->w - screenbuffer->w) / 2;
        dest_rect.y = (screen->h - screenbuffer->h) / 2;
    }

    palette_to_set = true;
}

#ifdef WIN32
void init_win32(LPCTSTR lpIconName);
#endif

void ToggleFullScreen(void)
{
    fullscreen = !fullscreen;
    if (fullscreen)
    {
        width = screenwidth;
        height = screenheight;
        if (!width || !height)
        {
            width = desktopwidth;
            height = desktopheight;
            screenwidth = 0;
            screenheight = 0;
            M_SaveDefaults();
        }

#ifdef SDL20
        if (sdl_window)
            SDL_DestroyWindow(sdl_window);
        sdl_window = SDL_CreateWindow(gamedescription, SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_FULLSCREEN);
        screen = SDL_GetWindowSurface(sdl_window);
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC);
#else
        screen = SDL_SetVideoMode(width, height, 0,
            SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
#endif

        if (!screen)
        {
            width = desktopwidth;
            height = desktopheight;
            screenwidth = 0;
            screenheight = 0;
            M_SaveDefaults();

#ifdef SDL20
            if (sdl_window)
                SDL_DestroyWindow(sdl_window);
            sdl_window = SDL_CreateWindow(gamedescription, SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED, desktopwidth, desktopheight, SDL_WINDOW_FULLSCREEN);
            screen = SDL_GetWindowSurface(sdl_window);
            sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC);
#else
            screen = SDL_SetVideoMode(desktopwidth, desktopheight, 0,
                SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
#endif

            if (!screen)
                I_Error("Error setting video mode %ix%i: %s\n", width, height, SDL_GetError());
        }

        if (screensize == 8)
        {
            if (gamestate != GS_LEVEL)
                returntowidescreen = true;
            else
            {
                dest_rect.x = 1;
                ToggleWideScreen(true);
                if (widescreen)
                    screensize = 7;
                R_SetViewSize(screensize);
                M_SaveDefaults();
                return;
            }
        }

        height = screen->h;
        width = height * 4 / 3;
        width += (width & 1);

        if (width > screen->w)
        {
            width = screen->w;
            height = width * 3 / 4;
            height += (height & 1);
        }
    }
    else
    {
        event_t ev;

        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        putenv(envstring);
        SDL_InitSubSystem(SDL_INIT_VIDEO);

#ifdef WIN32
        init_win32("IDI_ICON1");
#endif

        if (windowheight > desktopheight)
        {
            windowheight = desktopheight;
            windowwidth = windowheight * 4 / 3;
            windowwidth += (windowwidth & 1);
            M_SaveDefaults();
        }
        height = MAX(ORIGINALWIDTH * 3 / 4, windowheight);
        width = height * 4 / 3;
        width += (width & 1);

        if (width > windowwidth)
        {
            width = windowwidth;
            height = width * 3 / 4;
            height += (height & 1);
        }

        if (widescreen)
        {
            widescreen = false;
            screensize = 8;
            R_SetViewSize(screensize);
        }

        SetWindowPositionVars();

#ifdef SDL20
        if (sdl_window)
            SDL_DestroyWindow(sdl_window);
        sdl_window = SDL_CreateWindow(gamedescription, SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_RESIZABLE);
        screen = SDL_GetWindowSurface(sdl_window);
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC);
#else
        screen = SDL_SetVideoMode(width, height, 0,
            SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_RESIZABLE);
#endif

        CreateCursors();
        SDL_SetCursor(cursors[0]);

#ifdef SDL20
        SDL_SetWindowTitle(sdl_window, gamestate == GS_LEVEL ? mapnumandtitle : gamedescription);
#else
        SDL_WM_SetCaption(gamestate == GS_LEVEL ? mapnumandtitle : gamedescription, NULL);
#endif

        currently_grabbed = true;
        UpdateFocus();
        UpdateGrab();

#ifdef SDL12
        SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#endif

        ev.type = ev_keyup;
        ev.data1 = KEY_RALT;
        ev.data2 = 0;
        D_PostEvent(&ev);
    }

#ifdef SDL20
    screenbuffer = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0);
    sdl_texture = SDL_CreateTextureFromSurface(sdl_renderer, screenbuffer);
#else
    screenbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);
#endif

    pitch = screenbuffer->pitch;
    pixels = (byte *)screenbuffer->pixels;

    stepx = (SCREENWIDTH << FRACBITS) / width;
    stepy = (SCREENHEIGHT << FRACBITS) / height;

    startx = stepx - 1;
    starty = stepy - 1;

    dest_rect.x = (screen->w - screenbuffer->w) / 2;
    dest_rect.y = (screen->h - screenbuffer->h) / 2;

    M_SaveDefaults();
}

void ApplyWindowResize(int height)
{
    windowheight = MAX(SCREENWIDTH * 3 / 4, height);
    windowwidth = windowheight * 4 / 3;
    windowwidth += (windowwidth & 1);

#ifdef SDL20
    SDL_SetWindowSize(sdl_window, windowwidth, windowheight);
    screen = SDL_GetWindowSurface(sdl_window);

    screenbuffer = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0);
    sdl_texture = SDL_CreateTextureFromSurface(sdl_renderer, screenbuffer);
#else
    screen = SDL_SetVideoMode(windowwidth, windowheight, 0,
        SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | SDL_RESIZABLE);

    screenbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);
#endif

    pitch = screenbuffer->pitch;
    pixels = (byte *)screenbuffer->pixels;

    stepx = (SCREENWIDTH << FRACBITS) / windowwidth;
    stepy = (SCREENHEIGHT << FRACBITS) / windowheight;

    startx = stepx - 1;
    starty = stepy - 1;

    dest_rect.x = (screen->w - screenbuffer->w) / 2;
    dest_rect.y = (screen->h - screenbuffer->h) / 2;

    M_SaveDefaults();
}

void I_InitGammaTables(void)
{
    int i;
    int j;

    for (i = 0; i < GAMMALEVELS; i++)
        if (gammalevels[i] == 1.0)
            for (j = 0; j < 256; j++)
                gammatable[i][j] = j;
        else
            for (j = 0; j < 256; j++)
                gammatable[i][j] = (byte)(pow((j + 1) / 256.0, 1.0 / gammalevels[i]) * 255.0);
}

boolean I_ValidScreenMode(int width, int height)
{
#ifdef SDL20
    SDL_DisplayMode     mode;
    const int           modecount = SDL_GetNumDisplayModes(0);
    int                 i;

    for (i = 0; i < modecount; i++)
    {
        SDL_GetDisplayMode(0, i, &mode);
        if (!mode.w || !mode.h || (width >= mode.w && height >= mode.h))
            return true;
    }
    return false;
#else
    return SDL_VideoModeOK(width, height, 32, SDL_FULLSCREEN);
#endif
}

void I_InitKeyboard(void)
{
#ifdef WIN32
    capslock = (GetKeyState(VK_CAPITAL) & 0x0001);

    if ((alwaysrun && !capslock) || (!alwaysrun && capslock))
    {
        keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY, (uintptr_t)0);
        keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, (uintptr_t)0);
    }
#endif

#ifdef SDL20
    SDL_SetWindowTitle(sdl_window, gamedescription);
#else
    SDL_WM_SetCaption(gamedescription, NULL);
#endif
}

void I_InitGraphics(void)
{
    int         i = 0;
    SDL_Event   dummy;
    byte        *doompal = W_CacheLumpName("PLAYPAL", PU_CACHE);

    putenv("SDL_DISABLE_LOCK_KEYS=1");

    while (i < UCHAR_MAX)
        keys[i++] = true;
    keys['v'] = keys['V'] = false;
    keys['s'] = keys['S'] = false;
    keys['i'] = keys['I'] = false;
    keys['r'] = keys['R'] = false;
    keys['a'] = keys['A'] = false;
    keys['l'] = keys['L'] = false;

    I_InitTintTables(doompal);

    I_InitGammaTables();

#ifdef SDL12
    if (videodriver != NULL && strlen(videodriver) > 0)
    {
        M_snprintf(envstring, sizeof(envstring), "SDL_VIDEODRIVER=%s", videodriver);
        putenv(envstring);
    }
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
    {
#ifdef SDL12
        if (!strcasecmp(videodriver, "directx"))
            M_StringCopy(videodriver, "windib", 7);
        else
            M_StringCopy(videodriver, "directx", 8);
        M_snprintf(envstring, sizeof(envstring), "SDL_VIDEODRIVER=%s", videodriver);
        putenv(envstring);
        M_SaveDefaults();

        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
#endif
            I_Error("Failed to initialize video: %s", SDL_GetError());
    }

    CreateCursors();
    SDL_SetCursor(cursors[0]);

    if (fullscreen && (screenwidth || screenheight))
        if (!I_ValidScreenMode(screenwidth, screenheight))
        {
            screenwidth = 0;
            screenheight = 0;
        }

    SetVideoMode();

#ifdef WIN32
    init_win32("IDI_ICON1");
#endif

    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

#ifdef SDL20
    SDL_SetWindowTitle(sdl_window, PACKAGE_NAME);
#else
    SDL_WM_SetCaption(PACKAGE_NAME, NULL);
#endif

    SDL_FillRect(screenbuffer, NULL, 0);

    I_SetPalette(doompal);

#ifdef SDL20
    SDL_SetPaletteColors(screenbuffer->format->palette, palette, 0, 256);
    if (sdl_texture)
        SDL_DestroyTexture(sdl_texture);
    sdl_texture = SDL_CreateTextureFromSurface(sdl_renderer, screenbuffer);
#else
    SDL_SetColors(screenbuffer, palette, 0, 256);
#endif

    UpdateFocus();
    UpdateGrab();

    screens[0] = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);

    for (i = 0; i < SCREENHEIGHT; i++)
        rows[i] = *screens + i * SCREENWIDTH;

#ifdef SDL12
    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#endif

    while (SDL_PollEvent(&dummy));

    if (fullscreen)
        CenterMouse();
}
