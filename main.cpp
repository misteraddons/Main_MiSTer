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
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "fpga_io.h"
#include "scheduler.h"
#include "osd.h"
#include "offload.h"
#include "cdrom.h"
#include "cmd_bridge.h"

const char *version = "$VER:" VDATE;

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
	
	// Initialize command bridge
	printf("Initializing command bridge...\n");
	cmd_bridge_init();

#ifdef USE_SCHEDULER
	printf("Using scheduler mode - CD-ROM auto-detection disabled\n");
	scheduler_init();
	scheduler_run();
#else
	printf("Using main loop mode - CD-ROM auto-detection enabled\n");
	// Initialize CD-ROM processing in background
	static bool cdrom_initialized = false;
	static int boot_delay_counter = 0;
	static bool last_cd_present = false;
	
	while (1)
	{
		if (!is_fpga_ready(1))
		{
			fpga_wait_to_reset();
		}

		user_io_poll();
		input_poll(0);
		HandleUI();
		OsdUpdate();
		
		// After system has been running for a while, start CD-ROM detection
		// Wait ~15 seconds to allow network connection to establish
		boot_delay_counter++;
		
		
		if (!cdrom_initialized && boot_delay_counter > 1200) { // ~15 seconds delay
			cdrom_init();
			cdrom_initialized = true;
		}
		
		
		// CD-ROM background detection - check every ~5 seconds when in menu (for testing)
		if (cdrom_initialized && (boot_delay_counter % 400) == 0) { // Check every ~5 seconds
			printf("CD-ROM: Checking for disc... (counter=%d, in_menu=%d)\n", boot_delay_counter, is_menu());
			
			// Only run CD-ROM detection when in menu mode to prevent boot loops
			if (!is_menu()) {
				continue;
			}
			
			// Simple, non-blocking check - just see if device exists and is accessible
			bool cd_present = false;
			if (access("/dev/sr0", F_OK) == 0) {
				// Device exists, try a quick non-blocking open/close test
				int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
				if (fd >= 0) {
					close(fd);
					cd_present = true;
					printf("CD-ROM: Device accessible\n");
				} else {
					printf("CD-ROM: Device exists but not accessible\n");
				}
			} else {
				printf("CD-ROM: Device /dev/sr0 does not exist\n");
			}
			
			printf("CD-ROM: cd_present=%d, last_cd_present=%d\n", cd_present, last_cd_present);
			
			// If CD status changed from not present to present, auto-load the game
			if (cd_present && !last_cd_present) {
				printf("CD-ROM: Device accessible, attempting auto-load...\n");
				
				// Use command bridge to trigger automatic loading
				cmd_result_t result = cmd_bridge_process("cdrom_autoload");
				if (result.success) {
					printf("CD-ROM: Auto-load initiated successfully\n");
				} else {
					printf("CD-ROM: Auto-load failed: %s\n", result.message);
				}
			}
			
			last_cd_present = cd_present;
		}
	}
#endif
	return 0;
}
