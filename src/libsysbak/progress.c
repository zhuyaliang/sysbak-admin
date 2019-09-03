/**
 * progress.c - part of Partclone project
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * progress bar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include "progress.h"
#include "sysbak-share.h"


/// initial progress bar
extern void progress_init(progress_bar *prog, int start, ull stop,int size)
{
    memset(prog, 0, sizeof(progress_bar));
    prog->start = start;
    prog->stop = stop;
    prog->unit = 100.0 / (stop - start);
    prog->initial_time = time(0);
    prog->block_size = size;
}

static void calculate_speed(progress_bar *prog, ull copied,progress_data *progress_data)
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

    speedps  = prog->block_size * copied / elapsed;
    progress_data->percent   = percent;
	if (speedps >= kbyte)
	{
		dspeed = (double)speedps / (double)kbyte;
    }
	else
	{
		dspeed = speedps;
    }

    progress_data->speed = dspeed;
    progress_data->remained = (time_t)((elapsed/percent*100) - elapsed);
	progress_data->elapsed = elapsed;
}

/// update information at progress bar
extern gboolean progress_update(progress_bar *prog, ull copied,progress_data *progress_data)
{
	if (copied >= prog->stop)
	{
		return FALSE;
	}
    calculate_speed(prog, copied, progress_data);
	
	return TRUE;
}
