/*
====================================================================

DOOM RETRO
A classic, refined DOOM source port. For Windows PC.

Copyright � 1993-1996 id Software LLC, a ZeniMax Media company.
Copyright � 2005-2014 Simon Howard.
Copyright � 2013-2014 Brad Harding.

This file is part of DOOM RETRO.

DOOM RETRO is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DOOM RETRO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with DOOM RETRO. If not, see http://www.gnu.org/licenses/.

====================================================================
*/

#include "i_system.h"
#include "z_zone.h"
#include "m_random.h"

#include "doomdef.h"
#include "p_local.h"
#include "sounds.h"

#include "st_stuff.h"
#include "hu_stuff.h"

#include "s_sound.h"

#include "doomstat.h"


void G_PlayerReborn(int player);


//
// P_SetMobjState
// Returns true if the mobj is still present.
//
boolean P_SetMobjState(mobj_t *mobj, statenum_t state)
{
    state_t     *st;

    do
    {
        if (state == S_NULL)
        {
            mobj->state = (state_t *)S_NULL;
            P_RemoveMobj(mobj);
            return false;
        }

        st = &states[state];
        mobj->state = st;
        mobj->tics = st->tics;
        mobj->sprite = st->sprite;
        mobj->frame = st->frame;

        // Modified handling.
        // Call action functions when the state is set
        if (st->action.acp1)
            st->action.acp1(mobj);

        state = st->nextstate;
    } while (!mobj->tics);

    return true;
}


//
// P_ExplodeMissile
//
void P_ExplodeMissile(mobj_t *mo)
{
    mo->momx = mo->momy = mo->momz = 0;

    P_SetMobjState(mo, (statenum_t)mo->info->deathstate);

    mo->tics -= P_Random() & 3;

    if (mo->tics < 1)
        mo->tics = 1;

    mo->flags &= ~MF_MISSILE;

    if (mo->type == MT_ROCKET)
        mo->flags2 |= MF2_TRANSLUCENT;

    if (mo->info->deathsound)
        S_StartSound(mo, mo->info->deathsound);
}


//
// P_XYMovement
//
#define STOPSPEED       0x1000
#define FRICTION        0xe800

boolean         shootingsky = false;
int             puffcount = 0;

void P_XYMovement(mobj_t *mo)
{
    fixed_t     ptryx;
    fixed_t     ptryy;
    player_t    *player;
    fixed_t     xmove;
    fixed_t     ymove;

    if (!mo->momx && !mo->momy)
    {
        if (mo->flags & MF_SKULLFLY)
        {
            // the skull slammed into something
            mo->flags &= ~MF_SKULLFLY;
            mo->momx = mo->momy = mo->momz = 0;

            P_SetMobjState(mo, (statenum_t)mo->info->spawnstate);
        }
        return;
    }

    player = mo->player;

    if (mo->type == MT_ROCKET)
    {
        if (puffcount++ > 1)
            P_SpawnPuff(mo->x, mo->y, mo->z, mo->angle);
    }

    if (mo->momx > MAXMOVE)
        mo->momx = MAXMOVE;
    else if (mo->momx < -MAXMOVE)
        mo->momx = -MAXMOVE;

    if (mo->momy > MAXMOVE)
        mo->momy = MAXMOVE;
    else if (mo->momy < -MAXMOVE)
        mo->momy = -MAXMOVE;

    xmove = mo->momx;
    ymove = mo->momy;

    do
    {
        if (xmove > MAXMOVE / 2 || ymove > MAXMOVE / 2
            || xmove < -MAXMOVE / 2 || ymove < -MAXMOVE / 2)
        {
            ptryx = mo->x + xmove / 2;
            ptryy = mo->y + ymove / 2;
            xmove >>= 1;
            ymove >>= 1;
        }
        else
        {
            ptryx = mo->x + xmove;
            ptryy = mo->y + ymove;
            xmove = ymove = 0;
        }

        if (!P_TryMove(mo, ptryx, ptryy))
        {
            // blocked move
            if (mo->player)
            {
                // try to slide along it
                P_SlideMove(mo);
            }
            else if (mo->flags & MF_MISSILE)
            {
                // explode a missile
                if (ceilingline
                    && ceilingline->backsector
                    && ceilingline->backsector->ceilingpic == skyflatnum
                    && mo->z > ceilingline->backsector->ceilingheight)
                {
                    // Hack to prevent missiles exploding
                    // against the sky.
                    // Does not handle sky floors.
                    shootingsky = true;
                    P_RemoveMobj(mo);
                    return;
                }
                P_ExplodeMissile(mo);
            }
            else
                mo->momx = mo->momy = 0;
        }
    } while (xmove || ymove);


    if (mo->flags & (MF_MISSILE | MF_SKULLFLY))
        return;         // no friction for missiles ever

    if (mo->z > mo->floorz)
        return;         // no friction when airborne

    if (mo->flags & MF_CORPSE)
    {
        // do not stop sliding
        //  if halfway off a step with some momentum
        if (mo->momx > FRACUNIT / 4
            || mo->momx < -FRACUNIT / 4
            || mo->momy > FRACUNIT / 4
            || mo->momy < -FRACUNIT / 4)
        {
            if (mo->floorz != mo->subsector->sector->floorheight)
                return;
        }
    }

    if (mo->momx > -STOPSPEED
        && mo->momx < STOPSPEED
        && mo->momy > -STOPSPEED
        && mo->momy < STOPSPEED
        && (!player
            || (player->cmd.forwardmove == 0
                && player->cmd.sidemove == 0)))
    {
        // if in a walking frame, stop moving
        if (player && (unsigned)((player->mo->state - states) - S_PLAY_RUN1) < 4)
            P_SetMobjState(player->mo, S_PLAY);

        mo->momx = 0;
        mo->momy = 0;
    }
    else
    {
        mo->momx = FixedMul(mo->momx, FRICTION);
        mo->momy = FixedMul(mo->momy, FRICTION);
    }
}

//
// P_ZMovement
//
void P_ZMovement(mobj_t *mo)
{
    fixed_t     dist;
    fixed_t     delta;

    // check for smooth step up
    if (mo->player && mo->z < mo->floorz)
    {
        mo->player->viewheight -= mo->floorz - mo->z;

        mo->player->deltaviewheight
            = (VIEWHEIGHT - mo->player->viewheight) >> 3;
    }

    // adjust height
    mo->z += mo->momz;

    if ((mo->flags & MF_FLOAT)
        && mo->target)
    {
        // float down towards target if too close
        if (!(mo->flags & MF_SKULLFLY)
            && !(mo->flags & MF_INFLOAT))
        {
            dist = P_ApproxDistance(mo->x - mo->target->x,
                                    mo->y - mo->target->y);

            delta = mo->target->z + (mo->height >> 1) - mo->z;

            if (delta < 0 && dist < -delta * 3)
                mo->z -= FLOATSPEED;
            else if (delta > 0 && dist < delta * 3)
                mo->z += FLOATSPEED;
        }
    }

    // clip movement
    if (mo->z <= mo->floorz)
    {
        // hit the floor

        if (mo->flags & MF_SKULLFLY)
        {
            // the skull slammed into something
            mo->momz = -mo->momz;
        }

        if (mo->momz < 0)
        {
            if (mo->player
                && mo->momz < -GRAVITY * 8)
            {
                // Squat down.
                // Decrease viewheight for a moment
                // after hitting the ground (hard),
                // and utter appropriate sound.
                mo->player->deltaviewheight = mo->momz >> 3;

                if (mo->health > 0)
                    S_StartSound(mo, sfx_oof);
            }
            mo->momz = 0;
        }
        mo->z = mo->floorz;

        if ((mo->flags & MF_MISSILE)
            && !(mo->flags & MF_NOCLIP))
        {
            P_ExplodeMissile(mo);
            return;
        }
    }
    else if (!(mo->flags & MF_NOGRAVITY))
    {
        if (mo->momz == 0)
            mo->momz = -GRAVITY * 2;
        else
            mo->momz -= GRAVITY;
    }

    if (mo->z + mo->height > mo->ceilingz)
    {
        if (mo->flags & MF_SKULLFLY)
        {
            // the skull slammed into something
            mo->momz = -mo->momz;
        }

        // hit the ceiling
        if (mo->momz > 0)
            mo->momz = 0;

        mo->z = mo->ceilingz - mo->height;

        if ((mo->flags & MF_MISSILE)
            && !(mo->flags & MF_NOCLIP))
        {
            P_ExplodeMissile(mo);
            return;
        }
    }
}



//
// P_NightmareRespawn
//
void P_NightmareRespawn(mobj_t *mobj)
{
    fixed_t             x;
    fixed_t             y;
    fixed_t             z;
    subsector_t         *ss;
    mobj_t              *mo;
    mapthing_t          *mthing;

    x = mobj->spawnpoint.x << FRACBITS;
    y = mobj->spawnpoint.y << FRACBITS;

    if (!x && !y)
    {
        x = mobj->x;
        y = mobj->y;
    }

    // something is occupying it's position?
    if (!P_CheckPosition(mobj, x, y))
        return; // no respwan

    // spawn a teleport fog at old spot
    //  because of removal of the body?
    mo = P_SpawnMobj(mobj->x,
                     mobj->y,
                     mobj->subsector->sector->floorheight, MT_TFOG);
    mo->angle = mobj->angle;

    // initiate teleport sound
    S_StartSound(mo, sfx_telept);

    // spawn a teleport fog at the new spot
    ss = R_PointInSubsector(x, y);

    mo = P_SpawnMobj(x, y, ss->sector->floorheight, MT_TFOG);

    S_StartSound(mo, sfx_telept);

    // spawn the new monster
    mthing = &mobj->spawnpoint;

    // spawn it
    if (mobj->info->flags & MF_SPAWNCEILING)
        z = ONCEILINGZ;
    else
        z = ONFLOORZ;

    // inherit attributes from deceased one
    mo = P_SpawnMobj(x, y, z, mobj->type);
    mo->spawnpoint = mobj->spawnpoint;
    mo->angle = ANG45 * (mthing->angle / 45);

    if (mthing->options & MTF_AMBUSH)
        mo->flags |= MF_AMBUSH;

    mo->flags2 &= ~MF2_FLIPPEDCORPSE;

    mo->reactiontime = 18;

    // remove the old monster
    P_RemoveMobj(mobj);
}


//
// P_MobjThinker
//
void P_MobjThinker(mobj_t *mobj)
{
    // momentum movement
    if (mobj->momx
        || mobj->momy
        || (mobj->flags & MF_SKULLFLY))
    {
        P_XYMovement(mobj);

        if (mobj->thinker.function.acv == (actionf_v)(-1))
            return;             // mobj was removed
    }

    if (mobj->flags2 & MF2_BOB)
    {
        if (++mobj->bobcount == BOBCOUNT)
        {
            mobj->boblevel += mobj->bobdirection;

            if (mobj->bobdirection == 1
                && mobj->boblevel > 1)
            {
                mobj->bobdirection = -1;
            }
            else if (mobj->bobdirection == -1
                     && mobj->boblevel < -1)
            {
                mobj->bobdirection = 1;
            }

            mobj->bobcount = 0;

            mobj->z += mobj->boblevel * FRACUNIT;
        }
        if (mobj->z < mobj->floorz - FRACUNIT * 2
            || mobj->z > mobj->floorz + FRACUNIT * 2)
        {
            P_ZMovement(mobj);

            if (mobj->thinker.function.acv == (actionf_v)(-1))
                return;
        }
    }
    else if (mobj->z != mobj->floorz
             || mobj->momz)
    {
        P_ZMovement(mobj);

        if (mobj->thinker.function.acv == (actionf_v)(-1))
            return;             // mobj was removed
    }

    // cycle through states,
    //  calling action functions at transitions
    if (mobj->tics != -1)
    {
        mobj->tics--;

        // you can cycle through multiple states in a tic
        if (!mobj->tics)
            if (!P_SetMobjState(mobj, mobj->state->nextstate))
                return;         // freed itself
    }
    else
    {
        // check for nightmare respawn
        if (!(mobj->flags & MF_COUNTKILL))
            return;

        if (!respawnmonsters)
            return;

        mobj->movecount++;

        if (mobj->movecount < 12 * TICRATE)
            return;

        if (leveltime & 31)
            return;

        if (P_Random () > 4)
            return;

        P_NightmareRespawn(mobj);
    }
}


//
// P_SpawnMobj
//
mobj_t *P_SpawnMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type)
{
    mobj_t      *mobj;
    state_t     *st;
    mobjinfo_t  *info;

    mobj = (mobj_t *)Z_Malloc(sizeof(*mobj), PU_LEVEL, NULL);
    memset(mobj, 0, sizeof(*mobj));
    info = &mobjinfo[type];

    mobj->type = type;
    mobj->info = info;
    mobj->x = x;
    mobj->y = y;
    mobj->radius = info->radius;
    mobj->height = info->height;
    mobj->flags = info->flags;
    mobj->flags2 = info->flags2;
    mobj->health = info->spawnhealth;

    if (gameskill != sk_nightmare)
        mobj->reactiontime = info->reactiontime;

    mobj->lastlook = P_Random () % MAXPLAYERS;

    // do not set the state with P_SetMobjState,
    // because action routines cannot be called yet
    if (info->frames > 1)
        st = &states[info->spawnstate + M_RandomInt(0, info->frames - 1)];
    else
        st = &states[info->spawnstate];

    mobj->state = st;
    mobj->tics = st->tics;
    mobj->sprite = st->sprite;
    mobj->frame = st->frame;

    // set subsector and/or block links
    P_SetThingPosition(mobj);

    mobj->floorz = mobj->subsector->sector->floorheight;
    mobj->ceilingz = mobj->subsector->sector->ceilingheight;

    if (mobj->flags2 & MF2_BOB)
    {
        mobj->boblevel = 0;
        mobj->bobdirection = (M_RandomInt(0, 1) ? -1 : 1);
        mobj->bobcount = M_RandomInt(0, BOBCOUNT - 1);
    }

    if (z == ONFLOORZ)
        mobj->z = mobj->floorz;
    else if (z == ONCEILINGZ)
        mobj->z = mobj->ceilingz - mobj->info->height;
    else
        mobj->z = z;

    mobj->thinker.function.acp1 = (actionf_p1)P_MobjThinker;

    mobj->target = mobj->tracer = NULL;

    P_AddThinker(&mobj->thinker);

    return mobj;
}


//
// P_RemoveMobj
//
mapthing_t      itemrespawnque[ITEMQUESIZE];
int             itemrespawntime[ITEMQUESIZE];
int             iquehead;
int             iquetail;

void P_RemoveMobj(mobj_t *mobj)
{
    if ((mobj->flags & MF_SPECIAL)
        && !(mobj->flags & MF_DROPPED)
        && (mobj->type != MT_INV)
        && (mobj->type != MT_INS))
    {
        itemrespawnque[iquehead] = mobj->spawnpoint;
        itemrespawntime[iquehead] = leveltime;
        iquehead = (iquehead + 1) & (ITEMQUESIZE - 1);

        // lose one off the end?
        if (iquehead == iquetail)
            iquetail = (iquetail + 1) & (ITEMQUESIZE - 1);
    }

    // unlink from sector and block lists
    P_UnsetThingPosition(mobj);

    if (shootingsky)
    {
        if (mobj->type == MT_BFG)
            S_StartSound(mobj, mobj->info->deathsound);
        shootingsky = false;
    }
    else
    {
        // stop any playing sound
        S_StopSound(mobj);
    }

    if (!demorecording && !demoplayback)
        mobj->target = mobj->tracer = NULL;

    // free block
    P_RemoveThinker((thinker_t *)mobj);
}




//
// P_RespawnSpecials
//
void P_RespawnSpecials(void)
{
    fixed_t             x;
    fixed_t             y;
    fixed_t             z;

    subsector_t         *ss;
    mobj_t              *mo;
    mapthing_t          *mthing;

    int                 i;

    // only respawn items in deathmatch
    if (deathmatch != 2)
        return;

    // nothing left to respawn?
    if (iquehead == iquetail)
        return;

    // wait at least 30 seconds
    if (leveltime - itemrespawntime[iquetail] < 30 * TICRATE)
        return;

    mthing = &itemrespawnque[iquetail];

    x = mthing->x << FRACBITS;
    y = mthing->y << FRACBITS;

    // spawn a teleport fog at the new spot
    ss = R_PointInSubsector(x, y);
    mo = P_SpawnMobj(x, y, ss->sector->floorheight, MT_IFOG);
    mo->angle = mthing->angle;
    S_StartSound(mo, sfx_itmbk);

    // find which type to spawn
    for (i = 0; i < NUMMOBJTYPES; i++)
    {
        if (mthing->type == mobjinfo[i].doomednum)
            break;
    }

    // spawn it
    if (mobjinfo[i].flags & MF_SPAWNCEILING)
        z = ONCEILINGZ;
    else
        z = ONFLOORZ;

    mo = P_SpawnMobj(x, y, z, (mobjtype_t)i);
    mo->spawnpoint = *mthing;
    mo->angle = ANG45 * (mthing->angle / 45);

    // pull it from the queue
    iquetail = (iquetail + 1) & (ITEMQUESIZE - 1);
}




//
// P_SpawnPlayer
// Called when a player is spawned on the level.
// Most of the player structure stays unchanged
//  between levels.
//
extern int      lastlevel;
extern int      lastepisode;

void P_SpawnPlayer(mapthing_t *mthing)
{
    player_t            *p;
    fixed_t             x;
    fixed_t             y;
    fixed_t             z;

    mobj_t              *mobj;

    int                 i;

    // not playing?
    if (!playeringame[mthing->type - 1])
        return;

    p = &players[mthing->type - 1];

    if (p->playerstate == PST_REBORN)
        G_PlayerReborn(mthing->type - 1);

    x           = mthing->x << FRACBITS;
    y           = mthing->y << FRACBITS;
    z           = ONFLOORZ;
    mobj        = P_SpawnMobj(x, y, z, MT_PLAYER);

    // set color translations for player sprites
    if (mthing->type > 1)
        mobj->flags |= (mthing->type - 1) << MF_TRANSSHIFT;

    if (mthing->angle % 45 != 0)
    {
        mobj->angle = mthing->angle * (ANG45 / 45);
    }
    else
    {
        mobj->angle = ANG45 * (mthing->angle / 45);
    }

    mobj->player = p;
    mobj->health = p->health;

    p->mo = mobj;
    p->playerstate = PST_LIVE;
    p->refire = 0;
    p->message = NULL;
    p->damagecount = 0;
    p->bonuscount = 0;
    p->extralight = 0;
    p->fixedcolormap = 0;
    p->viewheight = VIEWHEIGHT;

    p->viewz = p->mo->z + p->viewheight;
    p->psprites[ps_weapon].sx = 0;
    p->mo->momx = p->mo->momy = 0;

    // setup gun psprite
    P_SetupPsprites(p);

    // give all cards in deathmatch mode
    if (deathmatch)
        for (i = 0; i < NUMCARDS; i++)
            p->cards[i] = true;

    lastlevel = -1;
    lastepisode = -1;

    if (mthing->type - 1 == consoleplayer)
    {
        // wake up the status bar
        ST_Start();
        // wake up the heads up text
        HU_Start();
    }
}


//
// P_SpawnMapThing
// The fields of the mapthing should
//  already be in host byte order.
//
void P_SpawnMapThing(mapthing_t *mthing)
{
    int                 i;
    int                 bit;
    mobj_t              *mobj;
    fixed_t             x;
    fixed_t             y;
    fixed_t             z;

    // count deathmatch start positions
    if (mthing->type == 11)
    {
        if (deathmatch_p < &deathmatchstarts[10])
        {
            memcpy(deathmatch_p, mthing, sizeof(*mthing));
            deathmatch_p++;
        }
        return;
    }

    // check for players specially
    if (mthing->type > 0 && mthing->type <= 4)
    {
        // save spots for respawning in network games
        playerstarts[mthing->type - 1] = *mthing;
        if (!deathmatch)
            P_SpawnPlayer(mthing);

        return;
    }

    // check for appropriate skill level
    if (!netgame && (mthing->options & 16))
        return;

    if (gameskill == sk_baby)
        bit = 1;
    else if (gameskill == sk_nightmare)
        bit = 4;
    else
        bit = 1 << (gameskill - 1);

    if (!(mthing->options & bit))
        return;

    // find which type to spawn
    for (i = 0; i < NUMMOBJTYPES; i++)
        if (mthing->type == mobjinfo[i].doomednum)
            break;

    if (i == NUMMOBJTYPES)
        return;

    // don't spawn keycards and players in deathmatch
    if (deathmatch && (mobjinfo[i].flags & MF_NOTDMATCH))
        return;

    // don't spawn any monsters if -nomonsters
    if (nomonsters
        && (i == MT_SKULL
            || (mobjinfo[i].flags & MF_COUNTKILL))
        && i != MT_KEEN)
    {
        return;
    }

    // spawn it
    x = mthing->x << FRACBITS;
    y = mthing->y << FRACBITS;

    if (mobjinfo[i].flags & MF_SPAWNCEILING)
        z = ONCEILINGZ;
    else
        z = ONFLOORZ;

    mobj = P_SpawnMobj(x, y, z, (mobjtype_t)i);
    mobj->spawnpoint = *mthing;

    if (mobj->tics > 0)
        mobj->tics = 1 + (P_Random () % mobj->tics);
    if (mobj->flags & MF_COUNTKILL)
        totalkills++;
    if (mobj->flags & MF_COUNTITEM)
        totalitems++;

    mobj->angle = ANG45 * (mthing->angle / 45);
    if (mthing->options & MTF_AMBUSH)
        mobj->flags |= MF_AMBUSH;
}



//
// GAME SPAWN FUNCTIONS
//


//
// P_SpawnPuff
//
extern fixed_t  attackrange;
extern angle_t  shootangle;

void P_SpawnPuff(fixed_t x, fixed_t y, fixed_t z, angle_t angle)
{
    mobj_t      *th;

    z += ((P_Random() - P_Random()) << 10);

    th = P_SpawnMobj(x, y, z, MT_PUFF);
    th->momz = FRACUNIT;
    th->tics -= P_Random() & 3;

    if (th->tics < 1)
        th->tics = 1;

    th->angle = angle;

    // don't make punches spark on the wall
    if (attackrange == MELEERANGE)
    {
        P_SetMobjState(th, S_PUFF3);
        S_StartSound(th, sfx_punch);
    }
}



//
// P_SpawnBlood
//
void P_SpawnBlood(fixed_t x, fixed_t y, fixed_t z, angle_t angle, int damage, mobjflag2_t flag)
{
    mobj_t      *th;
    int         i;

    angle += ANG180;

    for (i = (P_Random() % 7) + (damage >> 2) < 7 ? (damage >> 2) : 7; i; i--)
    {
        z += ((P_Random() - P_Random()) << 10);
        th = P_SpawnMobj(x, y, z, MT_BLOOD);
        th->momz = FRACUNIT * (2 + i / 6);
        th->tics -= P_Random() & 3;

        if (th->tics < 1)
          th->tics = 1;

        th->momx = FixedMul(i * FRACUNIT / 3,
                            finecosine[angle >> ANGLETOFINESHIFT]);
        th->momy = FixedMul(i * FRACUNIT / 3,
                            finesine[angle >> ANGLETOFINESHIFT]);

        th->flags2 |= flag;

        th->angle = angle;

        if (damage <= 12)
            P_SetMobjState(th, th->state->nextstate);
        if (damage < 9)
            P_SetMobjState(th, th->state->nextstate);

        angle += ((P_Random() - P_Random()) * 0xB60B60);
    }
}



//
// P_CheckMissileSpawn
// Moves the missile forward a bit
//  and possibly explodes it right there.
//
void P_CheckMissileSpawn(mobj_t *th)
{
    th->tics -= P_Random() & 3;
    if (th->tics < 1)
        th->tics = 1;

    // move a little forward so an angle can
    // be computed if it immediately explodes
    th->x += (th->momx >> 1);
    th->y += (th->momy >> 1);
    th->z += (th->momz >> 1);

    if (!P_TryMove(th, th->x, th->y))
        P_ExplodeMissile(th);
}

// Certain functions assume that a mobj_t pointer is non-NULL,
// causing a crash in some situations where it is NULL.  Vanilla
// Doom did not crash because of the lack of proper memory
// protection. This function substitutes NULL pointers for
// pointers to a dummy mobj, to avoid a crash.

mobj_t *P_SubstNullMobj(mobj_t *mobj)
{
    if (mobj == NULL)
    {
        static mobj_t dummy_mobj;

        dummy_mobj.x = 0;
        dummy_mobj.y = 0;
        dummy_mobj.z = 0;
        dummy_mobj.flags = 0;

        mobj = &dummy_mobj;
    }

    return mobj;
}

//
// P_SpawnMissile
//
mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *dest, mobjtype_t type)
{
    mobj_t      *th;
    angle_t     an;
    int         dist;

    if (!dest)
    {
        dest->x = 0;
        dest->y = 0;
        dest->z = 0;
        dest->flags = 0;
    }

    th = P_SpawnMobj(source->x,
                     source->y,
                     source->z + 4 * 8 * FRACUNIT, type);

    if (th->info->seesound)
        S_StartSound(th, th->info->seesound);

    th->target = source;        // where it came from
    an = R_PointToAngle2(source->x, source->y, dest->x, dest->y);

    // fuzzy player
    if (dest->flags & MF_SHADOW)
        an += (P_Random() - P_Random()) << 20;

    th->angle = an;
    an >>= ANGLETOFINESHIFT;
    th->momx = FixedMul(th->info->speed, finecosine[an]);
    th->momy = FixedMul(th->info->speed, finesine[an]);

    dist = P_ApproxDistance(dest->x - source->x, dest->y - source->y);
    dist = dist / th->info->speed;

    if (dist < 1)
        dist = 1;

    th->momz = (dest->z - source->z) / dist;
    P_CheckMissileSpawn(th);

    return th;
}


//
// P_SpawnPlayerMissile
// Tries to aim at a nearby monster.
//
void P_SpawnPlayerMissile(mobj_t *source, mobjtype_t type)
{
    mobj_t      *th;
    angle_t     an;

    fixed_t     x;
    fixed_t     y;
    fixed_t     z;
    fixed_t     slope;

    // see which target is to be aimed at
    an = source->angle;
    slope = P_AimLineAttack(source, an, 16 * 64 * FRACUNIT);

    if (!linetarget)
    {
        an += 1 << 26;
        slope = P_AimLineAttack(source, an, 16 * 64 * FRACUNIT);

        if (!linetarget)
        {
            an -= 2 << 26;
            slope = P_AimLineAttack(source, an, 16 * 64 * FRACUNIT);
        }

        if (!linetarget)
        {
            an = source->angle;
            slope = 0;
        }
    }

    x = source->x;
    y = source->y;
    z = source->z + 4 * 8 * FRACUNIT;

    th = P_SpawnMobj(x, y, z, type);

    if (th->info->seesound)
        S_StartSound(th, th->info->seesound);

    th->target = source;
    th->angle = an;
    th->momx = FixedMul(th->info->speed,
                        finecosine[an >> ANGLETOFINESHIFT]);
    th->momy = FixedMul(th->info->speed,
                        finesine[an >> ANGLETOFINESHIFT]);
    th->momz = FixedMul(th->info->speed, slope);

    puffcount = 0;

    P_CheckMissileSpawn(th);
}