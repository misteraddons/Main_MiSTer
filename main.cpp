/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski
Copyright 2012 Till Harbaum

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "fpga_io.h"
#include "scheduler.h"
#include "osd.h"
#include "offload.h"
#include "hardware.h"

const char *version = "$VER:" VDATE;

// 1ms input polling for responsive controls without uboot.txt dependency
#define INPUT_POLL_INTERVAL_MS 1

int main(int argc, char *argv[])
{
	// Always pin main worker process to core #1 as core #0 is the
	// hardware interrupt handler in Linux.  This reduces idle latency
	// in the main loop by about 6-7x.
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(1, &set);
	sched_setaffinity(0, sizeof(set), &set);

	offload_start();

	fpga_io_init();

	DISKLED_OFF;

	printf("\nMinimig by Dennis van Weeren");
	printf("\nARM Controller by Jakub Bednarski");
	printf("\nMiSTer code by Sorgelig\n\n");

	printf("Version %s\n\n", version + 5);

	if (argc > 1) printf("Core path: %s\n", argv[1]);
	if (argc > 2) printf("XML path: %s\n", argv[2]);

	if (!is_fpga_ready(1))
	{
		printf("\nGPI[31]==1. FPGA is uninitialized or incompatible core loaded.\n");
		printf("Quitting. Bye bye...\n");
		exit(0);
	}

	FindStorage();
	user_io_init((argc > 1) ? argv[1] : "",(argc > 2) ? argv[2] : NULL);

#ifdef USE_SCHEDULER
	scheduler_init();
	scheduler_run();
#else
	// 1ms input polling for responsive controls without uboot.txt dependency
	uint32_t next_poll_time = GetTimer(0);
	struct timespec sleep_time = {0, 100000}; // 0.1ms sleep to reduce CPU usage
	
	while (1)
	{
		if (!is_fpga_ready(1))
		{
			fpga_wait_to_reset();
		}

		uint32_t current_time = GetTimer(0);
		
		// Input polling at configured intervals for responsive controls
		if (current_time >= next_poll_time)
		{
			input_poll(0);
			next_poll_time = current_time + INPUT_POLL_INTERVAL_MS;
		}
		
		// Always run these
		user_io_poll();
		HandleUI();
		OsdUpdate();
		
		// Small sleep to reduce CPU usage between polls
		nanosleep(&sleep_time, NULL);
	}
#endif
	return 0;
}
