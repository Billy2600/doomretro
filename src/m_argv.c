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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "i_system.h"
#include "m_misc.h"

int     myargc;
char    **myargv;

//
// M_CheckParm
// Checks for the given parameter
// in the program's command line arguments.
// Returns the argument number (1 to argc-1)
// or 0 if not present
//
int M_CheckParmWithArgs(char *check, int num_args)
{
    int i;

    for (i = 1; i < myargc - num_args; i++)
        if (!strcasecmp(check, myargv[i]))
            return i;

    return 0;
}

int M_CheckParmsWithArgs(char *check1, char *check2, int num_args)
{
    int i;

    for (i = 1; i < myargc - num_args; i++)
        if (!strcasecmp(check1, myargv[i]) || !strcasecmp(check2, myargv[i]))
            return i;

    return 0;
}

int M_CheckParm(char *check)
{
    return M_CheckParmWithArgs(check, 0);
}
