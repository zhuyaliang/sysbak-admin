/**
 * progress.h - part of Partclone project
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
#include <time.h>

typedef unsigned long long ull;
typedef unsigned long      ul;
typedef unsigned int       uint;
// the progress bar structure
typedef struct progress_bar 
{
	int start;
	ull stop;
	int block_size;
	time_t initial_time;
    float unit;
}progress_bar;

typedef struct
{
	float  percent;
    float  speed;
    time_t remained;
    time_t elapsed;
}progress_data;

extern void       update_pui        (struct progress_bar *prog, 
		                             ull                  copied);

// initial progress bar
extern void       progress_init     (struct progress_bar *prog, 
		                             int                  start, 
									 ull                  stop, 
									 int                  size);

// update number
extern gboolean   progress_update   (struct progress_bar *prog, 
		                             ull                  copied,
								     progress_data       *pdata);
