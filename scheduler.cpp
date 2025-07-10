#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>

// Ensure CD-ROM ioctl structures are defined
#ifndef CDROM_GET_MCN
#define CDROM_GET_MCN 0x5311
struct cdrom_mcn {
	u_char medium_catalog_number[14];
};
#endif
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

// Generate a CDDB-style disc ID from TOC data
static int generate_cddb_disc_id(int fd, char* disc_id, size_t disc_id_size)
{
	struct cdrom_tochdr tochdr;
	if (ioctl(fd, CDROMREADTOCHDR, &tochdr) != 0) {
		return -1;
	}
	
	int num_tracks = tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1;
	if (num_tracks <= 0 || num_tracks > 99) {
		return -1;
	}
	
	// Get track offsets for CDDB calculation
	int track_offsets[100];
	int total_seconds = 0;
	
	for (int track = tochdr.cdth_trk0; track <= tochdr.cdth_trk1; track++) {
		struct cdrom_tocentry tocentry;
		tocentry.cdte_track = track;
		tocentry.cdte_format = CDROM_MSF;
		
		if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == 0) {
			// Convert MSF to seconds offset
			int offset = tocentry.cdte_addr.msf.minute * 60 + tocentry.cdte_addr.msf.second;
			track_offsets[track - tochdr.cdth_trk0] = offset + 2; // +2 for lead-in
		} else {
			return -1;
		}
	}
	
	// Get lead-out track for total disc length
	struct cdrom_tocentry lead_out;
	lead_out.cdte_track = CDROM_LEADOUT;
	lead_out.cdte_format = CDROM_MSF;
	
	if (ioctl(fd, CDROMREADTOCENTRY, &lead_out) == 0) {
		total_seconds = lead_out.cdte_addr.msf.minute * 60 + lead_out.cdte_addr.msf.second;
	} else {
		return -1;
	}
	
	// Calculate CDDB disc ID using standard algorithm
	int checksum = 0;
	for (int i = 0; i < num_tracks; i++) {
		int offset = track_offsets[i];
		while (offset > 0) {
			checksum += (offset % 10);
			offset /= 10;
		}
	}
	
	int disc_length = total_seconds - track_offsets[0];
	unsigned int cddb_id = ((checksum % 0xff) << 24) | ((disc_length & 0xffff) << 8) | (num_tracks & 0xff);
	
	// Generate more descriptive ID including track pattern
	snprintf(disc_id, disc_id_size, "%08x-%02d", cddb_id, num_tracks);
	
	printf("CD-ROM: Generated TOC-based disc ID: %s (tracks: %d, length: %d sec)\n", 
	       disc_id, num_tracks, disc_length);
	
	return 0;
}

// Extract CD-Text metadata from embedded disc data
static int extract_cdtext_metadata(int fd, char* album, size_t album_size, char* artist, size_t artist_size)
{
	// Try to read Media Catalog Number first
	struct cdrom_mcn mcn;
	if (ioctl(fd, CDROM_GET_MCN, &mcn) == 0) {
		printf("CD-ROM: Media Catalog Number: %s\n", mcn.medium_catalog_number);
	}
	
	// CD-Text reading requires accessing subchannel data
	// This is a simplified approach - full CD-Text parsing is complex
	struct cdrom_subchnl subchnl;
	subchnl.cdsc_format = CDROM_MSF;
	
	if (ioctl(fd, CDROMSUBCHNL, &subchnl) == 0) {
		printf("CD-ROM: Subchannel data available for metadata extraction\n");
		
		// For now, we'll use basic fallback naming
		// Full CD-Text implementation would require parsing R-W subchannel packs
		snprintf(album, album_size, "Unknown Album");
		snprintf(artist, artist_size, "Unknown Artist");
		
		return 1; // Indicate basic metadata available
	}
	
	// No CD-Text or subchannel data available
	album[0] = '\0';
	artist[0] = '\0';
	return 0;
}

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
					printf("CD-ROM: Running cleanup command: rm -f /media/fat/\\x97*.mgl, /media/fat/CD*.mgl, /media/fat/[0-9]*.mgl, and /media/fat/Audio*.mgl\n");
					int cleanup_result = system("rm -f /media/fat/\x97*.mgl /media/fat/CD*.mgl /media/fat/[0-9]*.mgl \"/media/fat/Audio\"*.mgl 2>/dev/null");
					printf("CD-ROM: Cleanup command result: %d\n", cleanup_result);
					printf("CD-ROM: Cleaned up selection and audio player MGL files\n");
				}
				
				// Handle audio CD detection
				if (cd_present && audio_cd && (!last_cd_present || !last_audio_cd)) {
					printf("CD-ROM: Audio CD detected, creating audio player MGL\n");
					
					// Generate unique disc ID and extract embedded metadata
					char disc_id[64] = "";
					char album[256] = "";
					char artist[256] = "";
					int track_count = 0;
					int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
					if (fd >= 0) {
						struct cdrom_tochdr tochdr;
						if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == 0) {
							track_count = tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1;
						}
						
						// Generate CDDB-style disc ID for potential metadata lookup
						if (generate_cddb_disc_id(fd, disc_id, sizeof(disc_id)) == 0) {
							printf("CD-ROM: Audio CD disc ID: %s\n", disc_id);
						}
						
						// Try to extract embedded CD-Text metadata
						if (extract_cdtext_metadata(fd, album, sizeof(album), artist, sizeof(artist))) {
							printf("CD-ROM: Found embedded metadata - Album: %s, Artist: %s\n", album, artist);
						}
						
						close(fd);
					}
					
					// Create descriptive audio CD MGL filename with available metadata
					char audio_mgl_path[512];
					if (strlen(album) > 0 && strlen(artist) > 0) {
						// Use CD-Text metadata if available
						char clean_album[256], clean_artist[256];
						strncpy(clean_album, album, sizeof(clean_album) - 1);
						strncpy(clean_artist, artist, sizeof(clean_artist) - 1);
						
						// Sanitize for filename
						for (int i = 0; clean_album[i]; i++) {
							if (clean_album[i] == '/' || clean_album[i] == '\\' || clean_album[i] == ':' || 
							    clean_album[i] == '*' || clean_album[i] == '?' || clean_album[i] == '"' ||
							    clean_album[i] == '<' || clean_album[i] == '>' || clean_album[i] == '|') {
								clean_album[i] = '_';
							}
						}
						for (int i = 0; clean_artist[i]; i++) {
							if (clean_artist[i] == '/' || clean_artist[i] == '\\' || clean_artist[i] == ':' || 
							    clean_artist[i] == '*' || clean_artist[i] == '?' || clean_artist[i] == '"' ||
							    clean_artist[i] == '<' || clean_artist[i] == '>' || clean_artist[i] == '|') {
								clean_artist[i] = '_';
							}
						}
						
						snprintf(audio_mgl_path, sizeof(audio_mgl_path), "/media/fat/%s - %s.mgl", 
						         clean_artist, clean_album);
					} else if (strlen(disc_id) > 0) {
						// Use disc ID if no CD-Text
						snprintf(audio_mgl_path, sizeof(audio_mgl_path), "/media/fat/Audio CD %s.mgl", disc_id);
					} else {
						// Fallback naming
						snprintf(audio_mgl_path, sizeof(audio_mgl_path), "/media/fat/Audio CD.mgl");
					}
					
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
