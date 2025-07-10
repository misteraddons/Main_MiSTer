#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <errno.h>
#include <string.h>
#include "libco.h"
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "fpga_io.h"
#include "osd.h"
#include "profiling.h"
#include "cdrom.h"
#include "cmd_bridge.h"

static cothread_t co_scheduler = nullptr;
static cothread_t co_poll = nullptr;
static cothread_t co_ui = nullptr;
static cothread_t co_cdrom = nullptr;
static cothread_t co_last = nullptr;

static void scheduler_wait_fpga_ready(void)
{
	while (!is_fpga_ready(1))
	{
		fpga_wait_to_reset();
	}
}

static void scheduler_co_poll(void)
{
	for (;;)
	{
		scheduler_wait_fpga_ready();

		{
			SPIKE_SCOPE("co_poll", 1000);
			user_io_poll();
			input_poll(0);
		}

		scheduler_yield();
	}
}

static void scheduler_co_ui(void)
{
	for (;;)
	{
		{
			SPIKE_SCOPE("co_ui", 1000);
			HandleUI();
			OsdUpdate();
		}

		scheduler_yield();
	}
}

static void scheduler_co_cdrom(void)
{
	static bool cdrom_initialized = false;
	static int check_counter = 0;
	static bool last_cd_present = false;
	static int cdrom_autoload_delay = 0;
	
	printf("CD-ROM: Auto-detection coroutine started\n");
	
	for (;;)
	{
		// Initialize CD-ROM subsystem after a delay
		if (!cdrom_initialized && check_counter > 100) { // ~5 seconds delay
			cdrom_init();
			cdrom_initialized = true;
		}
		
		// Check for CD very infrequently when in menu mode to prevent blocking
		if (cdrom_initialized && (check_counter % 1000) == 0) { // Check every ~50 seconds
			// Only show debug message every 20000 ticks to reduce spam
			if ((check_counter % 20000) == 0) {
				printf("CD-ROM: Checking for disc... (counter=%d, in_menu=%d)\n", check_counter, is_menu());
			}
			
			// Only run CD-ROM detection when in menu mode to prevent boot loops
			if (is_menu()) {
				// Quick, non-blocking disc detection
				bool cd_present = false;
				if (access("/dev/sr0", F_OK) == 0) {
					int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
					if (fd >= 0) {
						// Use simpler approach - just check if device opens successfully
						cd_present = true;
						close(fd);
						if (!last_cd_present) {
							printf("CD-ROM: Disc present and accessible\n");
						}
					} else {
						if (last_cd_present) {
							printf("CD-ROM: Cannot open device: %s\n", strerror(errno));
						}
					}
				} else {
					printf("CD-ROM: Device /dev/sr0 does not exist\n");
				}
				
				// Only print status when state changes
				if (cd_present != last_cd_present) {
					printf("CD-ROM: cd_present=%d, last_cd_present=%d\n", cd_present, last_cd_present);
				}
				
				// If CD status changed from present to not present, clean up flags
				if (!cd_present && last_cd_present) {
					printf("CD-ROM: Disc ejected, cleaning up processed game flags\n");
					int result = system("rm -f /tmp/cdrom_processed_* 2>/dev/null");
					printf("CD-ROM: Flag cleanup completed (exit code: %d)\n", result);
				}
				
				// If CD status changed from not present to present, delay auto-load
				if (cd_present && !last_cd_present) {
					printf("CD-ROM: Disc detected, scheduling auto-load...\n");
					cdrom_autoload_delay = 100; // Longer delay to let disc fully settle
				}
				
				last_cd_present = cd_present;
			}
		}
		
		// Handle delayed auto-load (only when in menu and delay is active)
		if (cdrom_autoload_delay > 0 && is_menu()) {
			cdrom_autoload_delay--;
			if (cdrom_autoload_delay == 0) {
				printf("CD-ROM: Executing auto-load (optimized)...\n");
				
				// Run auto-load but only when we have enough time between scheduler yields
				cmd_result_t result = cmd_bridge_process("cdrom_autoload");
				if (result.success) {
					printf("CD-ROM: Auto-load completed successfully\n");
				} else {
					printf("CD-ROM: Auto-load failed: %s\n", result.message);
				}
			}
		}
		
		check_counter++;
		scheduler_yield();
	}
}

static void scheduler_schedule(void)
{
	if (co_last == co_poll)
	{
		co_last = co_ui;
		co_switch(co_ui);
	}
	else if (co_last == co_ui)
	{
		co_last = co_cdrom;
		co_switch(co_cdrom);
	}
	else
	{
		co_last = co_poll;
		co_switch(co_poll);
	}
}

void scheduler_init(void)
{
	const unsigned int co_stack_size = 262144 * sizeof(void*);

	co_poll = co_create(co_stack_size, scheduler_co_poll);
	co_ui = co_create(co_stack_size, scheduler_co_ui);
	co_cdrom = co_create(co_stack_size, scheduler_co_cdrom);
}

void scheduler_run(void)
{
	co_scheduler = co_active();

	for (;;)
	{
		scheduler_schedule();
	}

	co_delete(co_cdrom);
	co_delete(co_ui);
	co_delete(co_poll);
	co_delete(co_scheduler);
}

void scheduler_yield(void)
{
	co_switch(co_scheduler);
}
