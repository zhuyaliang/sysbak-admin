/*  sysbak-admin 
*   Copyright (C) 2019  zhuyaliang https://github.com/zhuyaliang/
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.

*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.

*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include "progress.h"
#include "sysbak-share.h"

// initial progress bar
extern void progress_init(progress_bar *prog, int start, ull stop,int size)
{
    memset(prog, 0, sizeof(progress_bar));
    prog->start = start;
    prog->stop = stop;
    prog->unit = 100.0 / (stop - start);
    prog->initial_time = time(0);
    prog->block_size = size;
}

static void calculate_speed(progress_bar *prog, ull copied,progress_data *progressdata)
{
    uint64_t speedps = 1;
    double dspeed = 1.0;
    float percent = 1.0;
    time_t elapsed;
    uint64_t kbyte=1000;


    percent  = prog->unit * copied;
    if (percent <= 0)
        percent = 1;
    else if (percent >= 100)
        percent = 99.99;

    elapsed  = (time(0) - prog->initial_time);
    if (elapsed <= 0)
        elapsed = 1;

    speedps = prog->block_size * copied / elapsed;
    progressdata->percent   = percent;
    if (speedps >= kbyte)
    {
        dspeed = (double)speedps / (double)kbyte;
    }
    else
    {
        dspeed = speedps;
    }

    progressdata->speed = dspeed;
    progressdata->remained = (time_t)((elapsed/percent*100) - elapsed);
    progressdata->elapsed = elapsed;
}

// update information at progress bar
extern gboolean progress_update(progress_bar *prog, ull copied,progress_data *progressdata)
{
    if (copied >= prog->stop)
    {
        return FALSE;
    }
    calculate_speed(prog, copied, progressdata);

    return TRUE;
}
