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

#define _USE_MATH_DEFINES

#include <math.h>

#ifdef WIN32
#include <Windows.h>
#include <Xinput.h>
#endif

#include "am_map.h"
#include "d_deh.h"
#include "doomstat.h"
#include "dstrings.h"
#include "hu_stuff.h"
#include "i_gamepad.h"
#include "i_video.h"
#include "m_config.h"
#include "m_misc.h"
#include "p_local.h"
#include "SDL.h"
#include "st_stuff.h"
#include "v_video.h"
#include "z_zone.h"

#define BLACK                   0
#define WHITE                   4
#define DARKGRAY                5
#define BROWN                   64
#define GRAY                    96
#define GREEN                   112
#define YELLOW                  160
#define RED                     176
#define PINK                    251

// Automap colors
#define CROSSHAIRCOLOR          WHITE
#define MARKCOLOR               (GRAY + 4)
#define PLAYERCOLOR             WHITE
#define THINGCOLOR              GREEN
#define WALLCOLOR               RED
#define ALLMAPWALLCOLOR         (GRAY + 12)
#define MASKCOLOR               PINK
#define TELEPORTERCOLOR         (RED + 8)
#define FDWALLCOLOR             BROWN
#define ALLMAPFDWALLCOLOR       (GRAY + 14)
#define CDWALLCOLOR             YELLOW
#define ALLMAPCDWALLCOLOR       (GRAY + 10)
#define TSWALLCOLOR             (GRAY + 8)
#define GRIDCOLOR               DARKGRAY
#define BACKGROUNDCOLOR         BLACK

// Automap color priorities
#define PLAYERPRIORITY          12
#define THINGPRIORITY           11
#define WALLPRIORITY            10
#define ALLMAPWALLPRIORITY      9
#define MASKPRIORITY            8
#define CDWALLPRIORITY          7
#define ALLMAPCDWALLPRIORITY    6
#define FDWALLPRIORITY          5
#define ALLMAPFDWALLPRIORITY    4
#define TELEPORTERPRIORITY      3
#define TSWALLPRIORITY          2
#define GRIDPRIORITY            1

byte    *priorities;
byte    *mask;

byte    *playercolor;
byte    *thingcolor;
byte    *wallcolor;
byte    *allmapwallcolor;
byte    *maskcolor;
byte    *teleportercolor;
byte    *fdwallcolor;
byte    *allmapfdwallcolor;
byte    *cdwallcolor;
byte    *allmapcdwallcolor;
byte    *tswallcolor;
byte    *gridcolor;

#define AM_PANDOWNKEY           key_down
#define AM_PANDOWNKEY2          key_down2
#define AM_PANUPKEY             key_up
#define AM_PANUPKEY2            key_up2
#define AM_PANRIGHTKEY          key_right
#define AM_PANRIGHTKEY2         key_straferight
#define AM_PANLEFTKEY           key_left
#define AM_PANLEFTKEY2          key_strafeleft
#define AM_ZOOMINKEY            KEY_EQUALS
#define AM_ZOOMOUTKEY           KEY_MINUS
#define AM_STARTKEY             KEY_TAB
#define AM_ENDKEY               KEY_TAB
#define AM_GOBIGKEY             '0'
#define AM_FOLLOWKEY            'f'
#define AM_GRIDKEY              'g'
#define AM_MARKKEY              'm'
#define AM_CLEARMARKKEY         'c'
#define AM_ROTATEKEY            'r'

#define MAPWIDTH                SCREENWIDTH
#define MAPHEIGHT               (SCREENHEIGHT - SBARHEIGHT)
#define MAPAREA                 (MAPWIDTH * MAPHEIGHT)

// scale on entry
#define INITSCALEMTOF           (0.2 * FRACUNIT)
// how much the automap moves window per tic in map coordinates
// moves 140 pixels in 1 second
#define F_PANINC                (8 << speedtoggle)
// how much zoom-in per tic
// goes to 2x in 1 second
#define M_ZOOMIN                ((int)((float)FRACUNIT * (1.00f + F_PANINC / 200.0f)))
// how much zoom-out per tic
// pulls out to 0.5x in 1 second
#define M_ZOOMOUT               ((int)((float)FRACUNIT / (1.00f + F_PANINC / 200.0f)))

// translates between frame-buffer and map distances
#define FTOM(x)                 (fixed_t)(((int64_t)((x) << 16) * scale_ftom) >> FRACBITS)
#define MTOF(x)                 (fixed_t)((((int64_t)(x) * scale_mtof) >> FRACBITS) >> FRACBITS)
// translates between frame-buffer and map coordinates
#define CXMTOF(x)               MTOF(x - m_x)
#define CYMTOF(y)               (MAPHEIGHT - MTOF(y - m_y))

#define FLOAT2FIXED(f)          (((int)(f)) * 65536)
#define FIXED2FLOAT(f)          (((float)(f)) / (float)65536)

typedef struct
{
    fixed_t     x, y;
} mpoint_t;

typedef struct
{
    mpoint_t    a, b;
} mline_t;

//
// The vector graphics for the automap.
//  A line drawing of the player pointing right,
//   starting from the middle.
//
mline_t playerarrow[] =
{
    { { -973678,       0 }, {  -674085,       0 } }, //  -
    { { -674085,       0 }, {  1273270,       0 } }, //  -------
    { { 1273270,       0 }, {   674084,  299593 } }, //  ------>
    { { 1273270,       0 }, {   674084, -299593 } },
    { { -973678,       0 }, { -1273271,  299593 } }, // >------>
    { { -973678,       0 }, { -1273271, -299593 } },
    { { -674085,       0 }, {  -973678,  299593 } }, // >>----->
    { { -674085,       0 }, {  -973678, -299593 } }
};

#define PLAYERARROWLINES 8

mline_t cheatplayerarrow[] =
{
    { { -973678,       0 }, {  -674085,       0 } }, //  -
    { { -674085,       0 }, {  -524288,       0 } }, //  --
    { { -524288,       0 }, {  -124831,       0 } }, //  ---
    { { -124831,       0 }, {  1273270,       0 } }, //  -------
    { { 1273270,       0 }, {   674084,  199729 } }, //  ------>
    { { 1273270,       0 }, {   674084, -199729 } },
    { { -973678,       0 }, { -1273271,  199729 } }, // >------>
    { { -973678,       0 }, { -1273271, -199729 } },
    { { -674085,       0 }, {  -973678,  199729 } }, // >>----->
    { { -674085,       0 }, {  -973678, -199729 } },
    { { -524288,       0 }, {  -524288, -199729 } }, // >>-d--->
    { { -524288, -199729 }, {  -324559, -199729 } },
    { { -324559, -199729 }, {  -324559,  299593 } },
    { { -124831,       0 }, {  -124831, -199729 } }, // >>-dd-->
    { { -124831, -199729 }, {    74898, -199729 } },
    { {   74898, -199729 }, {    74898,  299593 } },
    { {  274627,  299593 }, {   274627, -171196 } }, // >>-ddt->
    { {  274627, -171196 }, {   312076, -208645 } },
    { {  312076, -208645 }, {   394464, -171196 } }
};

#define CHEATPLAYERARROWLINES 19

mline_t thingtriangle[] =
{
    { {  -32768,  -45875 }, {    65536,       0 } },
    { {   65536,       0 }, {   -32768,   45875 } },
    { {  -32768,   45875 }, {   -32768,  -45875 } }
};

#define THINGTRIANGLELINES 3

boolean         grid = GRID_DEFAULT;

boolean         automapactive = false;

static mpoint_t m_paninc;                       // how far the window pans each tic (map coords)
static fixed_t  mtof_zoommul;                   // how far the window zooms in each tic (map coords)
static fixed_t  ftom_zoommul;                   // how far the window zooms in each tic (fb coords)

static fixed_t  decpanx = 0;
static fixed_t  decpany = 0;

fixed_t         m_x = INT_MAX, m_y = INT_MAX;   // LL x,y where the window is on the map (map coords)
static fixed_t  m_x2, m_y2;                     // UR x,y where the window is on the map (map coords)

//
// width/height of window on map (map coords)
//
fixed_t         m_w;
fixed_t         m_h;

// based on level size
static fixed_t  min_x;
static fixed_t  min_y;
static fixed_t  max_x;
static fixed_t  max_y;

static fixed_t  min_scale_mtof;                 // used to tell when to stop zooming out
static fixed_t  max_scale_mtof;                 // used to tell when to stop zooming in

// old stuff for recovery later
static fixed_t  old_m_w, old_m_h;
static fixed_t  old_m_x, old_m_y;

// old location used by the Follower routine
static mpoint_t f_oldloc;

// used by MTOF to scale from map-to-frame-buffer coords
static fixed_t  scale_mtof = (fixed_t)INITSCALEMTOF;
// used by FTOM to scale from frame-buffer-to-map coords (=1/scale_mtof)
static fixed_t  scale_ftom;

static player_t *plr;                           // the player represented by an arrow

mpoint_t        *markpoints = NULL;             // where the points are
int             markpointnum = 0;               // next point to be assigned
int             markpointnum_max = 0;

boolean         followplayer = true;            // specifies whether to follow the player around
boolean         rotate = ROTATE_DEFAULT;

static boolean  stopped = true;

boolean         bigstate = false;
byte            *area;
static boolean  movement = false;
int             keydown;
int             direction;

int             GATE1;
int             GATE2;
int             GATE3;
int             GATE4;

__inline static fixed_t sign(fixed_t a)
{
    return (a > 0) - (a < 0);
}

static void AM_rotate(fixed_t* xp, fixed_t* yp, angle_t a);

static void AM_activateNewScale(void)
{
    m_x += (m_w >> 1);
    m_y += (m_h >> 1);
    m_w = FTOM(MAPWIDTH);
    m_h = FTOM(MAPHEIGHT);
    m_x -= (m_w >> 1);
    m_y -= (m_h >> 1);
    m_x2 = m_x + m_w;
    m_y2 = m_y + m_h;
}

static void AM_saveScaleAndLoc(void)
{
    old_m_x = m_x;
    old_m_y = m_y;
    old_m_w = m_w;
    old_m_h = m_h;
}

static void AM_restoreScaleAndLoc(void)
{
    m_w = old_m_w;
    m_h = old_m_h;
    if (followplayer)
    {
        m_x = FTOM(MTOF(plr->mo->x)) - (m_w >> 1);
        m_y = FTOM(MTOF(plr->mo->y)) - (m_h >> 1);
    }
    else
    {
        m_x = old_m_x;
        m_y = old_m_y;
    }
    m_x2 = m_x + m_w;
    m_y2 = m_y + m_h;

    // Change the scaling multipliers
    scale_mtof = FixedDiv(MAPWIDTH << FRACBITS, m_w);
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
}

//
// adds a marker at the current location
//
static void AM_addMark(void)
{
    int         i;
    int         x = m_x + (m_w >> 1);
    int         y = m_y + (m_h >> 1);
    static char message[32];

    for (i = 0; i < markpointnum; ++i)
        if (markpoints[i].x == x && markpoints[i].y == y)
            return;

    if (markpointnum >= markpointnum_max)
    {
        markpointnum_max = (markpointnum_max ? markpointnum_max << 1 : 16);
        markpoints = (mpoint_t *)realloc(markpoints, markpointnum_max * sizeof(*markpoints));
    }

    markpoints[markpointnum].x = x;
    markpoints[markpointnum].y = y;
    M_snprintf(message, sizeof(message), s_AMSTR_MARKEDSPOT, ++markpointnum);
    plr->message = message;
    message_dontfuckwithme = true;
}

//
// Determines bounding box of all vertices,
// sets global variables controlling zoom range.
//
static void AM_findMinMaxBoundaries(void)
{
    int         i;
    fixed_t     a;
    fixed_t     b;

    min_x = min_y = INT_MAX;
    max_x = max_y = INT_MIN;

    for (i = 0; i < numvertexes; ++i)
    {
        fixed_t x = vertexes[i].x;
        fixed_t y = vertexes[i].y;

        if (x < min_x)
            min_x = x;
        else if (x > max_x)
            max_x = x;
        if (y < min_y)
            min_y = y;
        else if (y > max_y)
            max_y = y;
    }

    min_x -= FRACUNIT * 24;
    max_x += FRACUNIT * 24;
    min_y -= FRACUNIT * 24;
    max_y += FRACUNIT * 24;

    a = FixedDiv(MAPWIDTH << FRACBITS, max_x - min_x);
    b = FixedDiv(MAPHEIGHT << FRACBITS, max_y - min_y);

    min_scale_mtof = MIN(a, b);
    max_scale_mtof = FixedDiv(MAPHEIGHT << FRACBITS, PLAYERRADIUS << 1);
}

static void AM_changeWindowLoc(void)
{
    fixed_t     incx = m_paninc.x;
    fixed_t     incy = m_paninc.y;

    if (rotate)
        AM_rotate(&incx, &incy, plr->mo->angle - ANG90);

    m_x += incx;
    m_y += incy;

    if (!rotate)
    {
        fixed_t w = (m_w >> 1);
        fixed_t h = (m_h >> 1);

        if (m_x + w > max_x)
        {
            m_x = max_x - w;
            decpanx = 0;
        }
        else if (m_x + w < min_x)
        {
            m_x = min_x - w;
            decpanx = 0;
        }
        if (m_y + h > max_y)
        {
            m_y = max_y - h;
            decpany = 0;
        }
        else if (m_y + h < min_y)
        {
            m_y = min_y - h;
            decpany = 0;
        }
    }

    m_x2 = m_x + m_w;
    m_y2 = m_y + m_h;
}

void AM_Init(void)
{
    byte        *priority;
    int         x;

    priority = (byte *)Z_Malloc(256, PU_STATIC, NULL);
    mask = (byte *)Z_Malloc(256, PU_STATIC, NULL);
    for (x = 0; x < 256; ++x)
    {
        *(priority + x) = 0;
        *(mask + x) = x;
    }

    *(priority + PLAYERCOLOR) = PLAYERPRIORITY;
    *(priority + THINGCOLOR) = THINGPRIORITY;
    *(priority + WALLCOLOR) = WALLPRIORITY;
    *(priority + ALLMAPWALLCOLOR) = ALLMAPWALLPRIORITY;
    *(priority + MASKCOLOR) = MASKPRIORITY;
    *(priority + CDWALLCOLOR) = CDWALLPRIORITY;
    *(priority + ALLMAPCDWALLCOLOR) = ALLMAPCDWALLPRIORITY;
    *(priority + FDWALLCOLOR) = FDWALLPRIORITY;
    *(priority + ALLMAPFDWALLCOLOR) = ALLMAPFDWALLPRIORITY;
    *(priority + TELEPORTERCOLOR) = TELEPORTERPRIORITY;
    *(priority + TSWALLCOLOR) = TSWALLPRIORITY;
    *(priority + GRIDCOLOR) = GRIDPRIORITY;

    *(mask + MASKCOLOR) = BACKGROUNDCOLOR;

    priorities = (byte *)Z_Malloc(65536, PU_STATIC, NULL);
    for (x = 0; x < 256; ++x)
    {
        int y;

        for (y = 0; y < 256; ++y)
            *(priorities + (x << 8) + y) = (*(priority + x) > *(priority + y) ? x : y);
    }

    playercolor = priorities + (PLAYERCOLOR << 8);
    thingcolor = priorities + (THINGCOLOR << 8);
    wallcolor = priorities + (WALLCOLOR << 8);
    allmapwallcolor = priorities + (ALLMAPWALLCOLOR << 8);
    maskcolor = priorities + (MASKCOLOR << 8);
    cdwallcolor = priorities + (CDWALLCOLOR << 8);
    allmapcdwallcolor = priorities + (ALLMAPCDWALLCOLOR << 8);
    fdwallcolor = priorities + (FDWALLCOLOR << 8);
    allmapfdwallcolor = priorities + (ALLMAPFDWALLCOLOR << 8);
    teleportercolor = priorities + (TELEPORTERCOLOR << 8);
    tswallcolor = priorities + (TSWALLCOLOR << 8);
    gridcolor = priorities + (GRIDCOLOR << 8);

    GATE1 = R_CheckFlatNumForName("GATE1");
    GATE2 = R_CheckFlatNumForName("GATE2");
    GATE3 = R_CheckFlatNumForName("GATE3");
    GATE4 = R_CheckFlatNumForName("GATE4");
}

static void AM_initVariables(void)
{
    automapactive = true;

    area = *screens + MAPAREA;

    f_oldloc.x = INT_MAX;

    m_paninc.x = m_paninc.y = 0;
    ftom_zoommul = FRACUNIT;
    mtof_zoommul = FRACUNIT;

    m_w = FTOM(MAPWIDTH);
    m_h = FTOM(MAPHEIGHT);

    // find player to center on initially
    if (playeringame[consoleplayer])
        plr = &players[consoleplayer];
    else
    {
        int i;

        plr = &players[0];
        for (i = 0; i < MAXPLAYERS; ++i)
            if (playeringame[i])
            {
                plr = &players[i];
                break;
            }
    }

    if (m_x == INT_MAX || followplayer)
    {
        m_x = FTOM(MTOF(plr->mo->x)) - (m_w >> 1);
        m_y = FTOM(MTOF(plr->mo->y)) - (m_h >> 1);
        m_x2 = m_x + m_w;
        m_y2 = m_y + m_h;
    }

    // inform the status bar of the change
    ST_AutomapEvent(AM_MSGENTERED);
}

//
// should be called at the start of every level
// right now, i figure it out myself
//
static void AM_LevelInit(void)
{
    followplayer = true;
    bigstate = false;

    AM_findMinMaxBoundaries();
    scale_mtof = 0x2ba0;
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);

    // for saving & restoring
    old_m_x = m_x;
    old_m_y = m_y;
    old_m_w = m_w;
    old_m_h = m_h;
}

void AM_Stop(void)
{
    automapactive = false;
    if (!idbehold && !(players[consoleplayer].cheats & CF_MYPOS) && !devparm)
        HU_clearMessages();
    ST_AutomapEvent(AM_MSGEXITED);
    stopped = true;
}

int lastlevel = -1, lastepisode = -1;

void AM_Start(void)
{
    if (!stopped)
        AM_Stop();
    stopped = false;

    if (!idbehold && !(players[consoleplayer].cheats & CF_MYPOS) && !devparm)
        HU_clearMessages();

    if (lastlevel != gamemap || lastepisode != gameepisode)
    {
        AM_LevelInit();
        lastlevel = gamemap;
        lastepisode = gameepisode;
    }
    AM_initVariables();
}

//
// set the window scale to the maximum size
//
static void AM_minOutWindowScale(void)
{
    scale_mtof = min_scale_mtof;
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
    AM_activateNewScale();
}

//
// set the window scale to the minimum size
//
static void AM_maxOutWindowScale(void)
{
    scale_mtof = max_scale_mtof;
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
    AM_activateNewScale();
}

#ifdef SDL20
SDL_Keymod      modstate;
#else
SDLMod          modstate;
#endif

static boolean AM_getSpeedToggle(void)
{
    boolean     lt = (gamepadbuttons & GAMEPAD_LEFT_TRIGGER);
    boolean     shift = (modstate & KMOD_SHIFT);

    return ((lt ? 1 : 0) + (shift ? 1 : 0) == 1);
}

//
// Handle events (user inputs) in automap mode
//
boolean AM_Responder(event_t *ev)
{
    int                 key;
    int                 rc = false;
    static boolean      backbuttondown = false;
    boolean             speedtoggle;
    static int          markpress = 0;

    direction = 0;
    modstate = SDL_GetModState();

    if (!menuactive && !paused)
    {
        if (!(gamepadbuttons & gamepadautomap))
            backbuttondown = false;

        if (!automapactive)
        {
            if ((ev->type == ev_keydown
                 && ev->data1 == AM_STARTKEY
                 && keydown != AM_STARTKEY
                 && !(modstate & KMOD_ALT))
                 || (ev->type == ev_gamepad
                     && (gamepadbuttons & gamepadautomap)
                     && !backbuttondown))
            {
                keydown = AM_STARTKEY;
                backbuttondown = true;
                AM_Start();
                viewactive = false;
                rc = true;
            }
        }
        else
        {
            if (ev->type == ev_keydown)
            {
                rc = true;
                key = ev->data1;

                // pan right
                if (key == AM_PANRIGHTKEY || key == AM_PANRIGHTKEY2)
                {
                    keydown = key;
                    if (followplayer)
                    {
                        m_paninc.x = 0;
                        rc = false;
                    }
                    else
                    {
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.x = FTOM(F_PANINC);
                        decpanx = 0;
                    }
                }

                // pan left
                else if (key == AM_PANLEFTKEY || key == AM_PANLEFTKEY2)
                {
                    keydown = key;
                    if (followplayer)
                    {
                        m_paninc.x = 0;
                        rc = false;
                    }
                    else
                    {
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.x = -FTOM(F_PANINC);
                        decpanx = 0;
                    }
                }

                // pan up
                else if (key == AM_PANUPKEY || key == AM_PANUPKEY2)
                {
                    keydown = key;
                    if (followplayer)
                    {
                        m_paninc.y = 0;
                        rc = false;
                    }
                    else
                    {
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.y = FTOM(F_PANINC);
                        decpany = 0;
                    }
                }

                // pan down
                else if (key == AM_PANDOWNKEY || key == AM_PANDOWNKEY2)
                {
                    keydown = key;
                    if (followplayer)
                    {
                        m_paninc.y = 0;
                        rc = false;
                    }
                    else
                    {
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.y = -FTOM(F_PANINC);
                        decpany = 0;
                    }
                }

                // zoom out
                else if (key == AM_ZOOMOUTKEY && !movement)
                {
                    keydown = key;
                    speedtoggle = AM_getSpeedToggle();
                    mtof_zoommul = M_ZOOMOUT;
                    ftom_zoommul = M_ZOOMIN;
                }

                // zoom in
                else if (key == AM_ZOOMINKEY && !movement)
                {
                    keydown = key;
                    speedtoggle = AM_getSpeedToggle();
                    mtof_zoommul = M_ZOOMIN;
                    ftom_zoommul = M_ZOOMOUT;
                    bigstate = false;
                }

                // leave automap
                else if (key == AM_ENDKEY && !(modstate & KMOD_ALT) && keydown != AM_ENDKEY)
                {
                    keydown = key;
                    viewactive = true;
                    AM_Stop();
                }

                // toggle maximum zoom
                else if (key == AM_GOBIGKEY && !idclev && !idmus)
                {
                    if (keydown != AM_GOBIGKEY)
                    {
                        keydown = key;
                        if (bigstate)
                        {
                            bigstate = false;
                            AM_restoreScaleAndLoc();
                        }
                        else if (scale_mtof != min_scale_mtof)
                        {
                            bigstate = true;
                            AM_saveScaleAndLoc();
                            AM_minOutWindowScale();
                        }
                    }
                }

                // toggle follow mode
                else if (key == AM_FOLLOWKEY)
                {
                    if (keydown != AM_FOLLOWKEY)
                    {
                        keydown = key;
                        followplayer = !followplayer;
                        if (followplayer)
                            m_paninc.x = m_paninc.y = 0;
                        f_oldloc.x = INT_MAX;
                        plr->message = (followplayer ? s_AMSTR_FOLLOWON : s_AMSTR_FOLLOWOFF);
                        message_dontfuckwithme = true;
                    }
                }

                // toggle grid
                else if (key == AM_GRIDKEY)
                {
                    if (keydown != AM_GRIDKEY)
                    {
                        keydown = key;
                        grid = !grid;
                        plr->message = (grid ? s_AMSTR_GRIDON : s_AMSTR_GRIDOFF);
                        message_dontfuckwithme = true;
                    }
                }

                // mark spot
                else if (key == AM_MARKKEY
                         && plr->health)
                {
                    if (keydown != AM_MARKKEY)
                    {
                        keydown = key;
                        AM_addMark();
                    }
                }

                // clear mark(s)
                else if (key == AM_CLEARMARKKEY)
                {
                    if (markpointnum)
                    {
                        if (++markpress == 5)
                        {
                            // clear all marks
                            plr->message = (markpointnum == 1 ? s_AMSTR_MARKCLEARED : s_AMSTR_MARKSCLEARED);
                            message_dontfuckwithme = true;
                            markpointnum = 0;
                        }
                        else if (markpress == 1)
                        {
                            // clear one mark
                            plr->message = s_AMSTR_MARKCLEARED;
                            message_dontfuckwithme = true;
                            markpointnum--;
                        }
                    }
                }

                // toggle rotate mode
                else if (key == AM_ROTATEKEY)
                {
                    if (keydown != AM_ROTATEKEY)
                    {
                        keydown = key;
                        rotate = !rotate;
                        plr->message = (rotate ? s_AMSTR_ROTATEON : s_AMSTR_ROTATEOFF);
                        message_dontfuckwithme = true;
                    }
                }
                else
                    rc = false;
            }
            else if (ev->type == ev_keyup)
            {
                rc = false;
                key = ev->data1;
                if (key == AM_CLEARMARKKEY)
                    markpress = 0;
                keydown = 0;
                if ((key == AM_ZOOMOUTKEY
                     || key == AM_ZOOMINKEY)
                    && !movement)
                {
                    mtof_zoommul = FRACUNIT;
                    ftom_zoommul = FRACUNIT;
                }
                else if (key == AM_FOLLOWKEY)
                {
                    int keydown = 0;

                    if (keystate(AM_PANLEFTKEY))
                        keydown = AM_PANLEFTKEY;
                    else if (keystate(AM_PANLEFTKEY2))
                        keydown = AM_PANLEFTKEY2;
                    else if (keystate(AM_PANRIGHTKEY))
                        keydown = AM_PANRIGHTKEY;
                    else if (keystate(AM_PANRIGHTKEY2))
                        keydown = AM_PANRIGHTKEY2;
                    else if (keystate(AM_PANUPKEY))
                        keydown = AM_PANUPKEY;
                    else if (keystate(AM_PANUPKEY2))
                        keydown = AM_PANUPKEY2;
                    else if (keystate(AM_PANDOWNKEY))
                        keydown = AM_PANDOWNKEY;
                    else if (keystate(AM_PANDOWNKEY2))
                        keydown = AM_PANDOWNKEY2;

                    if (keydown)
                    {
                        event_t event;

                        event.type = ev_keydown;
                        event.data1 = keydown;
                        event.data2 = 0;
                        D_PostEvent(&event);
                    }
                }
                else if (!followplayer)
                {
                    if (key == AM_PANLEFTKEY || key == AM_PANLEFTKEY2)
                    {
                        speedtoggle = AM_getSpeedToggle();
                        if (keystate(AM_PANRIGHTKEY)
                            || keystate(AM_PANRIGHTKEY2))
                        {
                            m_paninc.x = FTOM(F_PANINC);
                            decpanx = 0;
                        }
                        else if (m_paninc.x)
                            decpanx = F_PANINC - 1;
                    }
                    else if (key == AM_PANRIGHTKEY || key == AM_PANRIGHTKEY2)
                    {
                        speedtoggle = AM_getSpeedToggle();
                        if (keystate(AM_PANLEFTKEY)
                            || keystate(AM_PANLEFTKEY2))
                        {
                            m_paninc.x = -FTOM(F_PANINC);
                            decpanx = 0;
                        }
                        else if (m_paninc.x)
                            decpanx = F_PANINC - 1;
                    }
                    else if (key == AM_PANUPKEY || key == AM_PANUPKEY2)
                    {
                        speedtoggle = AM_getSpeedToggle();
                        if (keystate(AM_PANDOWNKEY)
                            || keystate(AM_PANDOWNKEY2))
                        {
                            m_paninc.y = FTOM(F_PANINC);
                            decpany = 0;
                        }
                        else if (m_paninc.y)
                            decpany = F_PANINC - 1;
                    }
                    else if (key == AM_PANDOWNKEY || key == AM_PANDOWNKEY2)
                    {
                        speedtoggle = AM_getSpeedToggle();
                        if (keystate(AM_PANUPKEY) || keystate(AM_PANUPKEY2))
                        {
                            m_paninc.y = -FTOM(F_PANINC);
                            decpany = 0;
                        }
                        else if (m_paninc.y)
                            decpany = F_PANINC - 1;
                    }
                }
            }
            else if (ev->type == ev_mouse)
            {
                // zoom in
                if (ev->data1 & MOUSE_WHEELUP)
                {
                    movement = true;
                    speedtoggle = AM_getSpeedToggle();
                    mtof_zoommul = M_ZOOMIN + 2000;
                    ftom_zoommul = M_ZOOMOUT - 2000;
                    bigstate = false;
                }
                // zoom out
                else if (ev->data1 & MOUSE_WHEELDOWN)
                {
                    movement = true;
                    speedtoggle = AM_getSpeedToggle();
                    mtof_zoommul = M_ZOOMOUT - 2000;
                    ftom_zoommul = M_ZOOMIN + 2000;
                }
            }
            else if (ev->type == ev_gamepad)
            {
                if ((gamepadbuttons & gamepadautomap) && !backbuttondown)
                {
                    viewactive = true;
                    backbuttondown = true;
                    AM_Stop();
                }

                if ((gamepadbuttons & GAMEPAD_RIGHT_SHOULDER)
                    && !(gamepadbuttons & GAMEPAD_LEFT_SHOULDER))
                {
                    movement = true;
                    speedtoggle = AM_getSpeedToggle();
                    mtof_zoommul = M_ZOOMIN;
                    ftom_zoommul = M_ZOOMOUT;
                    bigstate = false;
                }
                else if ((gamepadbuttons & GAMEPAD_LEFT_SHOULDER)
                         && !(gamepadbuttons & GAMEPAD_RIGHT_SHOULDER))
                {
                    movement = true;
                    speedtoggle = AM_getSpeedToggle();
                    mtof_zoommul = M_ZOOMOUT;
                    ftom_zoommul = M_ZOOMIN;
                }

#ifdef WIN32
                if (!followplayer)
                {
                    // pan right
                    if (gamepadthumbLX > 0)
                    {
                        movement = true;
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.x = FTOM(MTOF((fixed_t)(FTOM(F_PANINC) * gamepadthumbLXright * 1.2f)));
                    }

                    // pan left
                    else if (gamepadthumbLX < 0)
                    {
                        movement = true;
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.x = -FTOM(MTOF((fixed_t)(FTOM(F_PANINC) * gamepadthumbLXleft * 1.2f)));
                    }

                    // pan up
                    if (gamepadthumbLY < 0)
                    {
                        movement = true;
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.y = FTOM(MTOF((fixed_t)(FTOM(F_PANINC) * gamepadthumbLYup * 1.2f)));
                    }

                    // pan down
                    else if (gamepadthumbLY > 0)
                    {
                        movement = true;
                        speedtoggle = AM_getSpeedToggle();
                        m_paninc.y = -FTOM(MTOF((fixed_t)(FTOM(F_PANINC) * gamepadthumbLYdown * 1.2f)));
                    }
                }
#endif
            }

            if ((plr->cheats & CF_MYPOS) && !followplayer && (m_paninc.x || m_paninc.y))
            {
                double x = m_paninc.x;
                double y = m_paninc.y;

                if ((m_x == min_x - (m_w >> 1) && x < 0)
                    || (m_x == max_x - (m_w >> 1) && x > 0))
                    x = 0;
                if ((m_y == min_y - (m_h >> 1) && y < 0)
                    || (m_y == max_y - (m_h >> 1) && y > 0))
                    y = 0;
                direction = (int)(atan2(y, x) * 180.0 / M_PI);
                if (direction < 0)
                    direction += 360;
            }
        }
    }
    return rc;
}

//
// Rotation in 2D.
// Used to rotate player arrow line character.
//
static void AM_rotate(fixed_t *xp, fixed_t *yp, angle_t a)
{
    static angle_t      angle_saved = 0;
    static double       sinrot = 0;
    static double       cosrot = 1;

    double              x, y;
    double              tmpx;

    if (angle_saved != a)
    {
        double  rot = (double)a / (double)(1u << 31) * M_PI;

        angle_saved = a;
        sinrot = sin(rot);
        cosrot = cos(rot);
    }
    x = FIXED2FLOAT(*xp);
    y = FIXED2FLOAT(*yp);
    tmpx = x * cosrot - y * sinrot;
    y = x * sinrot + y * cosrot;
    x = tmpx;
    *xp = FLOAT2FIXED(x);
    *yp = FLOAT2FIXED(y);
}

static void AM_rotatePoint(fixed_t *x, fixed_t *y)
{
    fixed_t pivotx = m_x + (m_w >> 1);
    fixed_t pivoty = m_y + (m_h >> 1);

    *x -= pivotx;
    *y -= pivoty;
    AM_rotate(x, y, ANG90 - plr->mo->angle);
    *x += pivotx;
    *y += pivoty;
}

//
// Zooming
//
static void AM_changeWindowScale(void)
{
    // Change the scaling multipliers
    scale_mtof = FixedMul(scale_mtof, mtof_zoommul);
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);

    if (scale_mtof < min_scale_mtof)
        AM_minOutWindowScale();
    else if (scale_mtof > max_scale_mtof)
        AM_maxOutWindowScale();
    else
        AM_activateNewScale();
}

static void AM_doFollowPlayer(void)
{
    fixed_t     x = plr->mo->x;
    fixed_t     y = plr->mo->y;

    if (f_oldloc.x != x || f_oldloc.y != y)
    {
        m_x = FTOM(MTOF(x)) - (m_w >> 1);
        m_y = FTOM(MTOF(y)) - (m_h >> 1);
        m_x2 = m_x + m_w;
        m_y2 = m_y + m_h;
        f_oldloc.x = x;
        f_oldloc.y = y;
    }
}

static void AM_decelerate(void)
{
    if (decpanx)
        m_paninc.x = FixedMul(sign(m_paninc.x), FTOM(--decpanx));
    if (decpany)
        m_paninc.y = FixedMul(sign(m_paninc.y), FTOM(--decpany));
}

//
// Updates on Game Tic
//
void AM_Ticker(void)
{
    if (!automapactive)
        return;

    if (followplayer)
        AM_doFollowPlayer();

    // Change the zoom if necessary
    if (ftom_zoommul != FRACUNIT)
        AM_changeWindowScale();

    // Change x,y location
    if ((m_paninc.x || m_paninc.y) && !menuactive && !paused)
    {
        AM_decelerate();
        AM_changeWindowLoc();
    }

    if (movement)
    {
        movement = false;
        m_paninc.x = 0;
        m_paninc.y = 0;
        mtof_zoommul = FRACUNIT;
        ftom_zoommul = FRACUNIT;
    }
}

//
// Clear automap frame buffer.
//
static void AM_clearFB(void)
{
    memset(*screens, BACKGROUNDCOLOR, MAPAREA);
}

//
// Automap clipping of lines.
//
// Based on Cohen-Sutherland clipping algorithm but with a slightly
// faster reject and precalculated slopes. If the speed is needed,
// use a hash algorithm to handle the common cases.
static boolean AM_clipMline(int *x0, int *y0, int *x1, int *y1)
{
    enum
    {
        LEFT   = 1,
        RIGHT  = 2,
        TOP    = 4,
        BOTTOM = 8
    };

    unsigned int        outcode1 = 0;
    unsigned int        outcode2 = 0;

    *x0 = CXMTOF(*x0);
    if (*x0 < -1)
        outcode1 = LEFT;
    else if (*x0 >= MAPWIDTH)
        outcode1 = RIGHT;
    *x1 = CXMTOF(*x1);
    if (*x1 < -1)
        outcode2 = LEFT;
    else if (*x1 >= MAPWIDTH)
        outcode2 = RIGHT;
    if (outcode1 & outcode2)
        return false;
    *y0 = CYMTOF(*y0);
    if (*y0 < -1)
        outcode1 |= TOP;
    else if (*y0 >= MAPHEIGHT)
        outcode1 |= BOTTOM;
    *y1 = CYMTOF(*y1);
    if (*y1 < -1)
        outcode2 |= TOP;
    else if (*y1 >= MAPHEIGHT)
        outcode2 |= BOTTOM;
    return !(outcode1 & outcode2);
}

static __inline void _PUTDOT(byte *dot, byte *color)
{
    *dot = *(*dot + color);
}

static __inline void PUTDOT(unsigned int x, unsigned int y, byte *color)
{
    if (x < MAPWIDTH && y < MAPAREA)
        _PUTDOT(*screens + y + x, color);
}

static __inline void PUTBIGDOT(unsigned int x, unsigned int y, byte *color)
{
    if (x < MAPWIDTH)
    {
        byte    *dot = *screens + y + x;
        boolean top = (y < MAPAREA);
        boolean bottom = (y + MAPWIDTH < MAPAREA);

        if (top)
            _PUTDOT(dot, color);
        if (bottom)
            _PUTDOT(dot + MAPWIDTH, color);
        if (x + 1 < MAPWIDTH)
        {
            if (top)
                _PUTDOT(dot + 1, color);
            if (bottom)
                _PUTDOT(dot + MAPWIDTH + 1, color);
        }
    }
    else if (++x < MAPWIDTH)
    {
        byte    *dot = *screens + y + x;

        if (y < MAPAREA)
            _PUTDOT(dot, color);
        if (y + MAPWIDTH < MAPAREA)
            _PUTDOT(dot + MAPWIDTH, color);
    }
}

static __inline void PUTTRANSDOT(unsigned int x, unsigned int y, byte *color)
{
    if (x < MAPWIDTH && y < MAPAREA)
    {
        byte    *dot = *screens + y + x;

        if (*dot != *(tinttab60 + PLAYERCOLOR))
            *dot = *(tinttab60 + (*dot << 8) + PLAYERCOLOR);
    }
}

//
// Classic Bresenham w/ whatever optimizations needed for speed
//
static void AM_drawFline(int x0, int y0, int x1, int y1, byte *color,
                         void (*putdot)(unsigned int, unsigned int, byte *))
{
    int dx = x1 - x0;
    int dy = y1 - y0;

    if (!dy)
    {
        // horizontal line
        int     sx = SIGN(dx);

        x0 = BETWEEN(-1, x0, MAPWIDTH - 1);
        x1 = BETWEEN(-1, x1, MAPWIDTH - 1);

        y0 *= MAPWIDTH;

        putdot(x0, y0, color);
        while (x0 != x1)
            putdot(x0 += sx, y0, color);

        return;
    }
    else if (!dx)
    {
        // vertical line
        int     sy = SIGN(dy) * MAPWIDTH;

        y0 = BETWEEN(-MAPWIDTH, y0 * MAPWIDTH, MAPAREA - MAPWIDTH);
        y1 = BETWEEN(-MAPWIDTH, y1 * MAPWIDTH, MAPAREA - MAPWIDTH);

        putdot(x0, y0, color);
        while (y0 != y1)
            putdot(x0, y0 += sy, color);

        return;
    }
    else
    {
        int     sx = SIGN(dx);
        int     sy = SIGN(dy) * MAPWIDTH;

        dx = ABS(dx);
        dy = ABS(dy);
        y0 *= MAPWIDTH;
        if (dx == dy)
        {
            // diagonal line
            putdot(x0, y0, color);
            while (x0 != x1)
                putdot(x0 += sx, y0 += sy, color);

            return;
        }
        else
        {
            putdot(x0, y0, color);
            if (dx > dy)
            {
                // x-major line
                int     error = (dy <<= 1) - dx;

                dx <<= 1;
                while (x0 != x1)
                {
                    int mask = ~(error >> 31);

                    putdot(x0 += sx, y0 += (sy & mask), color);
                    error += dy - (dx & mask);
                }
                return;
            }
            else
            {
                // y-major line
                int     error = (dx <<= 1) - dy;

                dy <<= 1;
                y1 *= MAPWIDTH;
                while (y0 != y1)
                {
                    int mask = ~(error >> 31);

                    putdot(x0 += (sx & mask), y0 += sy, color);
                    error += dx - (dy & mask);
                }
                return;
            }
        }
    }
}

//
// Clip lines, draw visible parts of lines.
//
static void AM_drawMline(int x0, int y0, int x1, int y1, byte *color)
{
    if (AM_clipMline(&x0, &y0, &x1, &y1))
        AM_drawFline(x0, y0, x1, y1, color, PUTDOT);
}

static void AM_drawBigMline(int x0, int y0, int x1, int y1, byte *color)
{
    if (AM_clipMline(&x0, &y0, &x1, &y1))
        AM_drawFline(x0, y0, x1, y1, color, PUTBIGDOT);
}

static void AM_drawTransMline(int x0, int y0, int x1, int y1, byte *color)
{
    if (AM_clipMline(&x0, &y0, &x1, &y1))
        AM_drawFline(x0, y0, x1, y1, color, PUTTRANSDOT);
}

//
// Draws flat (floor/ceiling tile) aligned grid lines.
//
static void AM_drawGrid(void)
{
    fixed_t     x, y;
    fixed_t     start, end;
    mline_t     ml;

    fixed_t     minlen = (fixed_t)(sqrt((double)m_w * (double)m_w + (double)m_h * (double)m_h));
    fixed_t     extx = (minlen - m_w) >> 1;
    fixed_t     exty = (minlen - m_h) >> 1;

    // Figure out start of vertical gridlines
    start = m_x - extx;
    if ((start - bmaporgx) % MAPBLOCKSIZE)
        start += MAPBLOCKSIZE - ((start - bmaporgx) % MAPBLOCKSIZE);
    end = m_x + minlen - extx;

    // draw vertical gridlines
    for (x = start; x < end; x += MAPBLOCKSIZE)
    {
        ml.a.x = x;
        ml.b.x = x;
        ml.a.y = m_y - exty;
        ml.b.y = ml.a.y + minlen;
        if (rotate)
        {
            AM_rotatePoint(&ml.a.x, &ml.a.y);
            AM_rotatePoint(&ml.b.x, &ml.b.y);
        }
        AM_drawMline(ml.a.x, ml.a.y, ml.b.x, ml.b.y, gridcolor);
    }

    // Figure out start of horizontal gridlines
    start = m_y - exty;
    if ((start - bmaporgy) % MAPBLOCKSIZE)
        start += MAPBLOCKSIZE - ((start - bmaporgy) % MAPBLOCKSIZE);
    end = m_y + minlen - exty;

    // draw horizontal gridlines
    for (y = start; y < end; y += MAPBLOCKSIZE)
    {
        ml.a.x = m_x - extx;
        ml.b.x = ml.a.x + minlen;
        ml.a.y = y;
        ml.b.y = y;
        if (rotate)
        {
            AM_rotatePoint(&ml.a.x, &ml.a.y);
            AM_rotatePoint(&ml.b.x, &ml.b.y);
        }
        AM_drawMline(ml.a.x, ml.a.y, ml.b.x, ml.b.y, gridcolor);
    }
}

boolean isteleport(int floorpic)
{
    return (floorpic == GATE1 || floorpic == GATE2 || floorpic == GATE3 || floorpic == GATE4);
}

//
// Determines visible lines, draws them.
// This is LineDef based, not LineSeg based.
//
static void AM_drawWalls(void)
{
    boolean     allmap = plr->powers[pw_allmap];
    boolean     cheating = (plr->cheats & (CF_ALLMAP | CF_ALLMAP_THINGS));
    int         i = 0;

    while (i < numlines)
    {
        line_t  line = lines[i++];
        short   flags = line.flags;

        if ((flags & ML_DONTDRAW) && !cheating)
            continue;
        else
        {
            sector_t            *backsector = line.backsector;
            sector_t            *frontsector = line.frontsector;
            short               mapped = (flags & ML_MAPPED);
            short               secret = (flags & ML_SECRET);
            short               special = line.special;
            static mline_t      l;

            l.a.x = line.v1->x;
            l.a.y = line.v1->y;
            l.b.x = line.v2->x;
            l.b.y = line.v2->y;

            if (rotate)
            {
                AM_rotatePoint(&l.a.x, &l.a.y);
                AM_rotatePoint(&l.b.x, &l.b.y);
            }

            if ((special == W1_TeleportToTaggedSectorContainingTeleportLanding
                || special == W1_ExitLevel
                || special == WR_TeleportToTaggedSectorContainingTeleportLanding
                || (special >= W1_ExitLevelAndGoToSecretLevel
                    && special <= MR_TeleportToTaggedSectorContainingTeleportLanding))
                && ((flags & ML_TELEPORTTRIGGERED) || cheating || isteleport(backsector->floorpic)))
            {
                if (cheating || (mapped && !secret
                    && backsector->ceilingheight != backsector->floorheight))
                {
                    AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, teleportercolor);
                    continue;
                }
                else if (allmap)
                {
                    AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, allmapfdwallcolor);
                    continue;
                }
            }
            if (!backsector || (secret && !cheating))
                AM_drawBigMline(l.a.x, l.a.y, l.b.x, l.b.y, (mapped || cheating ? wallcolor : 
                                (allmap ? allmapwallcolor : maskcolor)));
            else if (backsector->floorheight != frontsector->floorheight)
            {
                if (mapped || cheating)
                    AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, fdwallcolor);
                else if (allmap)
                    AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, allmapfdwallcolor);
            }
            else if (backsector->ceilingheight != frontsector->ceilingheight)
            {
                if (mapped || cheating)
                    AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, cdwallcolor);
                else if (allmap)
                    AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, allmapcdwallcolor);
            }
            else if (cheating)
                AM_drawMline(l.a.x, l.a.y, l.b.x, l.b.y, tswallcolor);
        }
    }

    if (!cheating && !allmap)
    {
        byte    *dot = *screens;

        while (dot < area)
        {
            *dot = *(*dot + mask);
            ++dot;
        }
    }
}

static void AM_drawLineCharacter(mline_t *lineguy, int lineguylines, fixed_t scale,
                                 angle_t angle, byte *color, fixed_t x, fixed_t y)
{
    int i;

    if (rotate)
        angle -= plr->mo->angle - ANG90;

    for (i = 0; i < lineguylines; ++i)
    {
        int     x1, y1;
        int     x2, y2;

        if (scale)
        {
            x1 = FixedMul(lineguy[i].a.x, scale);
            y1 = FixedMul(lineguy[i].a.y, scale);
            x2 = FixedMul(lineguy[i].b.x, scale);
            y2 = FixedMul(lineguy[i].b.y, scale);
        }
        else
        {
            x1 = lineguy[i].a.x;
            y1 = lineguy[i].a.y;
            x2 = lineguy[i].b.x;
            y2 = lineguy[i].b.y;
        }
        if (angle)
        {
            AM_rotate(&x1, &y1, angle);
            AM_rotate(&x2, &y2, angle);
        }
        AM_drawMline(x + x1, y + y1, x + x2, y + y2, color);
    }
}

static void AM_drawTransLineCharacter(mline_t *lineguy, int lineguylines, fixed_t scale,
                                      angle_t angle, byte *color, fixed_t x, fixed_t y)
{
    int i;

    if (rotate)
        angle -= plr->mo->angle - ANG90;

    for (i = 0; i < lineguylines; ++i)
    {
        int     x1, y1;
        int     x2, y2;

        if (scale)
        {
            x1 = FixedMul(lineguy[i].a.x, scale);
            y1 = FixedMul(lineguy[i].a.y, scale);
            x2 = FixedMul(lineguy[i].b.x, scale);
            y2 = FixedMul(lineguy[i].b.y, scale);
        }
        else
        {
            x1 = lineguy[i].a.x;
            y1 = lineguy[i].a.y;
            x2 = lineguy[i].b.x;
            y2 = lineguy[i].b.y;
        }
        if (angle)
        {
            AM_rotate(&x1, &y1, angle);
            AM_rotate(&x2, &y2, angle);
        }
        AM_drawTransMline(x + x1, y + y1, x + x2, y + y2, color);
    }
}

static void AM_drawPlayers(void)
{
    int             invisibility = plr->powers[pw_invisibility];
    mpoint_t        pt;

    pt.x = plr->mo->x;
    pt.y = plr->mo->y;

    if (rotate)
        AM_rotatePoint(&pt.x, &pt.y);

    if (plr->cheats & (CF_ALLMAP | CF_ALLMAP_THINGS))
    {
        if (invisibility > 128 || (invisibility & 8))
            AM_drawTransLineCharacter(cheatplayerarrow, CHEATPLAYERARROWLINES, 0,
                                        plr->mo->angle, NULL, pt.x, pt.y);
        else
            AM_drawLineCharacter(cheatplayerarrow, CHEATPLAYERARROWLINES, 0,
                                    plr->mo->angle, playercolor, pt.x, pt.y);
    }
    else
    {
        if (invisibility > 128 || (invisibility & 8))
            AM_drawTransLineCharacter(playerarrow, PLAYERARROWLINES, 0,
                                        plr->mo->angle, NULL, pt.x, pt.y);
        else
            AM_drawLineCharacter(playerarrow, PLAYERARROWLINES, 0,
                                    plr->mo->angle, playercolor, pt.x, pt.y);
    }
}

static void AM_drawThings(void)
{
    int i;

    for (i = 0; i < numsectors; ++i)
    {
        // e6y
        // Two-pass method for better usability of automap:
        // The first one will draw all things except enemies
        // The second one is for enemies only
        // Stop after first pass if the current sector has no enemies
        int     pass;
        int     enemies = 0;

        for (pass = 0; pass < 2; pass += (enemies ? 1 : 2))
        {
            mobj_t      *thing = sectors[i].thinglist;

            while (thing)
            {
                //e6y: stop if all enemies from current sector already have been drawn
                if (pass && !enemies)
                    break;
                if (pass == ((thing->flags & (MF_COUNTKILL | MF_CORPSE)) == MF_COUNTKILL ?
                    (!pass ? enemies++ : enemies--), 0 : 1))
                {
                    thing = thing->snext;
                    continue;
                }

                if (thing->type != MT_PLAYER && thing->type != MT_BLOODSPLAT)
                {
                    int x = thing->x;
                    int y = thing->y;
                    int fx;
                    int fy;
                    int lump = sprites[thing->sprite].spriteframes[0].lump[0];
                    int w = BETWEEN(24 << FRACBITS, MIN(spritewidth[lump], spriteheight[lump]),
                        96 << FRACBITS) >> 1;

                    if (rotate)
                        AM_rotatePoint(&x, &y);

                    fx = CXMTOF(x);
                    fy = CYMTOF(y);

                    if (fx >= -w && fx <= MAPWIDTH + w && fy >= -w && fy <= MAPHEIGHT + w)
                        AM_drawLineCharacter(thingtriangle, THINGTRIANGLELINES, w, thing->angle,
                                             thingcolor, x, y);
                }
                thing = thing->snext;
            }
        }
    }
}

const char *marknums[10] =
{
    "011111101122221112222221122112211221122112211221"
    "122112211221122112211221122222211122221101111110",
    "001111000112210011222100122221001112210000122100"
    "001221000012210000122100001221000012210000111100",
    "111111101222221112222221111112210111122111222221"
    "122222111221111012211111122222211222222111111111",
    "111111101222221112222221111112210111122101222221"
    "012222210111122111111221122222211222221111111110",
    "111111111221122112211221122112211221122112222221"
    "122222211111122100001221000012210000122100001111",
    "111111111222222112222221122111111221111012222211"
    "122222211111122111111221122222211222221111111110",
    "011111101122221012222210122111101221111012222211"
    "122222211221122112211221122222211122221101111110",
    "111111111222222112222221111112210011222101122211"
    "012221100122110001221000012210000122100001111000",
    "011111101122221112222221122112211221122111222211"
    "122222211221122112211221122222211122221101111110",
    "011111101122221112222221122112211221122112222221"
    "112222210111122101111221012222210122221101111110"
};

#define MARKWIDTH       8
#define MARKHEIGHT      12

static void AM_drawMarks(void)
{
    int i;

    for (i = 0; i < markpointnum; ++i)
    {
        int     number = i + 1;
        int     temp = number;
        int     digits = 1;
        int     x = markpoints[i].x;
        int     y = markpoints[i].y;

        if (rotate)
            AM_rotatePoint(&x, &y);

        x = CXMTOF(x) - (MARKWIDTH >> 1) + 1;
        y = CYMTOF(y) - (MARKHEIGHT >> 1) - 1;

        while (temp /= 10)
            ++digits;
        x += (digits - 1) * (MARKWIDTH >> 1);
        x -= (number > 1 && number % 10 == 1);
        x -= (number / 10 == 1);

        do
        {
            int digit = number % 10;
            int j;

            if (i > 0 && digit == 1)
                x += 2;
            for (j = 0; j < MARKWIDTH * MARKHEIGHT; ++j)
            {
                int fx = x + j % MARKWIDTH;

                if ((unsigned)fx < MAPWIDTH)
                {
                    int fy = y + j / MARKWIDTH;

                    if ((unsigned)fy < MAPHEIGHT)
                    {
                        char src = marknums[digit][j];
                        byte *dest = *screens + fy * MAPWIDTH + fx;

                        if (src == '2')
                            *dest = MARKCOLOR;
                        else if (src == '1' && *dest != MARKCOLOR && *dest != GRIDCOLOR)
                            *dest = *(*dest + tinttab80);
                    }
                }
            }
            x -= MARKWIDTH;
            number /= 10;
        }
        while (number > 0);
    }
}

static __inline void AM_DrawScaledPixel(int x, int y, byte *color)
{
    byte        *dest = *screens + ((y << 1) - 1) * MAPWIDTH + (x << 1) - 1;

    *dest = *(*dest + color);
    ++dest;
    *dest = *(*dest + color);
    dest += MAPWIDTH;
    *dest = *(*dest + color);
    --dest;
    *dest = *(*dest + color);
}

#define CENTERX         (ORIGINALWIDTH >> 1)
#define CENTERY         ((ORIGINALHEIGHT - 32) >> 1)

static void AM_drawCrosshair(void)
{
    byte        *color = tinttab60 + (CROSSHAIRCOLOR << 8);

    AM_DrawScaledPixel(CENTERX - 2, CENTERY, color);
    AM_DrawScaledPixel(CENTERX - 1, CENTERY, color);
    AM_DrawScaledPixel(CENTERX, CENTERY, color);
    AM_DrawScaledPixel(CENTERX + 1, CENTERY, color);
    AM_DrawScaledPixel(CENTERX + 2, CENTERY, color);
    AM_DrawScaledPixel(CENTERX, CENTERY - 2, color);
    AM_DrawScaledPixel(CENTERX, CENTERY - 1, color);
    AM_DrawScaledPixel(CENTERX, CENTERY + 1, color);
    AM_DrawScaledPixel(CENTERX, CENTERY + 2, color);
}

#define DARKLEVELS      6

static void AM_darkenEdges(void)
{
    int i;

    for (i = 0; i < DARKLEVELS; ++i)
    {
        byte    *colormap = colormaps + (DARKLEVELS - i) * 1024;
        int     x, y;

        for (x = i; x < MAPWIDTH - i; ++x)
        {
            byte        *dot = *screens + i * MAPWIDTH + x;

            *dot = *(*dot + colormap);
            dot = *screens + (MAPHEIGHT - i - 1) * MAPWIDTH + x;
            *dot = *(*dot + colormap);
        }

        for (y = i + 1; y < MAPHEIGHT - i - 1; ++y)
        {
            byte        *dot = *screens + y * MAPWIDTH + i;

            *dot = *(*dot + colormap);
            dot = *screens + (y + 1) * MAPWIDTH - i - 1;
            *dot = *(*dot + colormap);
        }
    }
}

void AM_Drawer(void)
{
    AM_clearFB();
    AM_drawWalls();
    if (grid)
        AM_drawGrid();
    if (plr->cheats & CF_ALLMAP_THINGS)
        AM_drawThings();
    if (markpointnum)
        AM_drawMarks();
    AM_drawPlayers();
    AM_darkenEdges();
    if (!followplayer)
        AM_drawCrosshair();
}
