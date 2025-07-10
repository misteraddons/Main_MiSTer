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
	
	printf("CD-ROM: Auto-detection coroutine started\n");
	
	for (;;)
	{
		// Initialize CD-ROM subsystem after a delay
		if (!cdrom_initialized && check_counter > 300) { // ~15 seconds delay
			cdrom_init();
			cdrom_initialized = true;
		}
		
		// Check for CD every ~5 seconds when in menu mode
		if (cdrom_initialized && (check_counter % 100) == 0) { // Check every ~5 seconds
			// Only show debug message every 10000 ticks to reduce spam
			if ((check_counter % 10000) == 0) {
				printf("CD-ROM: Checking for disc... (counter=%d, in_menu=%d)\n", check_counter, is_menu());
			}
			
			// Only run CD-ROM detection when in menu mode to prevent boot loops
			if (is_menu()) {
				// Proper disc detection using ioctl
				bool cd_present = false;
				if (access("/dev/sr0", F_OK) == 0) {
					int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
					if (fd >= 0) {
						// Use CDROM_DISC_STATUS to check if disc is actually present
						int status = ioctl(fd, CDROM_DISC_STATUS);
						close(fd);
						
						if (status == CDS_DISC_OK || status == CDS_DATA_1 || status == CDS_DATA_2 || 
						    status == CDS_AUDIO || status == CDS_MIXED) {
							cd_present = true;
							if (!last_cd_present) {
								printf("CD-ROM: Disc present and ready (status=%d)\n", status);
							}
						} else if (status == CDS_NO_DISC) {
							if (last_cd_present) {
								printf("CD-ROM: No disc in drive\n");
							}
						} else if (status == CDS_TRAY_OPEN) {
							if (last_cd_present) {
								printf("CD-ROM: Tray is open\n");
							}
						} else if (status == CDS_DRIVE_NOT_READY) {
							if (last_cd_present) {
								printf("CD-ROM: Drive not ready\n");
							}
						} else {
							if (last_cd_present) {
								printf("CD-ROM: Drive status unknown (%d)\n", status);
							}
						}
					} else {
						printf("CD-ROM: Cannot open device: %s\n", strerror(errno));
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
				
				// If CD status changed from not present to present, auto-load the game
				if (cd_present && !last_cd_present) {
					printf("CD-ROM: Disc detected, attempting auto-load...\n");
					
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
