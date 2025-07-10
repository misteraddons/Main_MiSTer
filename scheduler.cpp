#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "libco.h"
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "fpga_io.h"
#include "osd.h"
#include "profiling.h"
#include "cdrom.h"
#include "cmd_bridge.h"
#include "cfg.h"

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
	static bool last_audio_cd = false;
	static int cdrom_autoload_delay = 0;
	static int cdrom_fd = -1;
	
	printf("CD-ROM: Auto-detection coroutine started\n");
	
	for (;;)
	{
		// Initialize CD-ROM subsystem after a delay
		if (!cdrom_initialized && check_counter > 100) { // ~5 seconds delay
			cdrom_init();
			// Open device once and keep it open
			if (access("/dev/sr0", F_OK) == 0) {
				cdrom_fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
				if (cdrom_fd >= 0) {
					printf("CD-ROM: Device opened persistently (fd=%d)\n", cdrom_fd);
				} else {
					printf("CD-ROM: Failed to open device persistently: %s\n", strerror(errno));
				}
			}
			cdrom_initialized = true;
		}
		
		// Check for CD-ROM much less frequently - every 20 seconds  
		if (cdrom_initialized && (check_counter % 200000) == 0) { // Check every ~20 seconds
			
			// Only run CD-ROM detection when in menu mode to prevent boot loops  
			if (is_menu()) {
				bool cd_present = false;
				bool audio_cd = false;
				
				// Fork a background process to handle all CD-ROM I/O operations
				// This completely isolates blocking operations from the main process
				pid_t pid = fork();
				if (pid == 0) {
					// Child process - handle all CD-ROM I/O here
					bool child_cd_present = false;
					bool child_audio_cd = false;
					
					// Open device in child process (won't block main process)
					int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
					if (fd >= 0) {
						int status = ioctl(fd, CDROM_DISC_STATUS);
						if (status == CDS_DISC_OK || status == CDS_DATA_1 || status == CDS_DATA_2 || 
						    status == CDS_AUDIO || status == CDS_MIXED) {
							child_cd_present = true;
							
							// Check if this is an audio CD
							if (status == CDS_AUDIO) {
								child_audio_cd = true;
							}
						}
						close(fd);
					}
					
					// Write result to a simple flag file for main process to read
					FILE* flag = fopen("/tmp/cdrom_status", "w");
					if (flag) {
						fprintf(flag, "%d %d\n", child_cd_present ? 1 : 0, child_audio_cd ? 1 : 0);
						fclose(flag);
					}
					
					exit(0); // Child process exits
				} else if (pid > 0) {
					// Parent process - non-blocking check of result file
					FILE* flag = fopen("/tmp/cdrom_status", "r");
					if (flag) {
						int status = 0;
						int audio_status = 0;
						if (fscanf(flag, "%d %d", &status, &audio_status) >= 1) {
							cd_present = (status == 1);
							audio_cd = (audio_status == 1);
						}
						fclose(flag);
					}
				} else {
					printf("CD-ROM: Failed to fork CD-ROM detection process\n");
				}
				
				// Only print status when state changes
				if (cd_present != last_cd_present || audio_cd != last_audio_cd) {
					printf("CD-ROM: cd_present=%d, audio_cd=%d\n", cd_present, audio_cd);
				}
				
				// If CD status changed from present to not present, clean up MGL files
				if (!cd_present && last_cd_present) {
					printf("CD-ROM: Disc ejected, cleaning up MGL files\n");
					
					// Get the current MGL file path and delete it
					const char* mgl_path = cmd_bridge_get_current_mgl_path();
					if (mgl_path && strlen(mgl_path) > 0) {
						if (unlink(mgl_path) == 0) {
							printf("CD-ROM: Deleted MGL file: %s\n", mgl_path);
						} else {
							printf("CD-ROM: Failed to delete MGL file: %s\n", mgl_path);
						}
						// Clear the tracked path
						cmd_bridge_clear_current_mgl_path();
					}
					
					// Also clean up numbered selection MGL files and audio CD player files
					system("rm -f /media/fat/\"\\0x97 \"*.mgl 2>/dev/null");
					printf("CD-ROM: Cleaned up selection and audio player MGL files\n");
				}
				
				// Handle audio CD detection
				if (cd_present && audio_cd && (!last_cd_present || !last_audio_cd)) {
					printf("CD-ROM: Audio CD detected, creating audio player MGL\n");
					
					// Create a simple audio CD player MGL file
					char audio_mgl_path[512];
					snprintf(audio_mgl_path, sizeof(audio_mgl_path), "/media/fat/\\0x97 Audio Disc.mgl");
					
					printf("CD-ROM: Attempting to create MGL at: %s\n", audio_mgl_path);
					FILE* mgl = fopen(audio_mgl_path, "w");
					if (mgl) {
						fprintf(mgl, "<mistergamedescription>\n");
						fprintf(mgl, "    <rbf>_Utility/CD_Audio_Player</rbf>\n");
						fprintf(mgl, "    <file delay=\"1\" type=\"s\" index=\"0\" path=\"/dev/sr0\"/>\n");
						fprintf(mgl, "</mistergamedescription>\n");
						fclose(mgl);
						printf("CD-ROM: Successfully created audio player MGL: %s\n", audio_mgl_path);
						
						// Verify file was created
						if (access(audio_mgl_path, F_OK) == 0) {
							printf("CD-ROM: Verified MGL file exists\n");
						} else {
							printf("CD-ROM: ERROR: MGL file does not exist after creation!\n");
						}
						
						// Update current MGL path tracking
						cmd_bridge_set_current_mgl_path(audio_mgl_path);
						
						// Refresh menu to show the new audio player option
						printf("CD-ROM: Checking if menu is present...\n");
						if (menu_present()) {
							printf("CD-ROM: Menu is present, triggering refresh\n");
							usleep(200000); // Increased delay to 200ms
							menu_key_set(102); // HOME key for refresh
							printf("CD-ROM: Menu refresh triggered with HOME key\n");
						} else {
							printf("CD-ROM: Menu not present, skipping refresh\n");
						}
					} else {
						printf("CD-ROM: ERROR: Failed to create audio player MGL at: %s (errno: %d, %s)\n", 
						       audio_mgl_path, errno, strerror(errno));
					}
				}
				// If CD status changed from not present to present and it's not audio, delay auto-load
				else if (cd_present && !audio_cd && !last_cd_present) {
					printf("CD-ROM: Data disc detected, scheduling auto-load...\n");
					// Use configurable delay (convert seconds to ticks: seconds * 50 ticks/second)
					// If delay is 0, use 1 tick minimum to ensure proper execution flow
					cdrom_autoload_delay = cfg.cdrom_autoload_delay > 0 ? cfg.cdrom_autoload_delay * 50 : 1;
				}
				
				last_cd_present = cd_present;
				last_audio_cd = audio_cd;
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
