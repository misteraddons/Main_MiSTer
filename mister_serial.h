#ifndef MISTER_SERIAL_H
#define MISTER_SERIAL_H

#include <stdint.h>

void mister_serial_init(const char *device, const char *core);
void mister_serial_set_game_loading(const char *filename);
void mister_serial_set_game(const char *filename, uint32_t crc32 = 0,
	const char *serial = 0);
void mister_serial_set_osd(int visible);
void mister_serial_set_preview(const char *kind, const char *name, const char *path);
void mister_serial_set_progress(int current, int maximum);
void mister_serial_set_idle(int idle);
void mister_serial_set_video(uint32_t source_width, uint32_t source_height,
	uint32_t source_refresh_millihz, int interlaced,
	uint32_t output_width, uint32_t output_height,
	uint32_t output_refresh_millihz, int direct_video);
void mister_serial_controller_scan_begin();
void mister_serial_controller_scan_add(const char *id, const char *name,
	uint16_t vid, uint16_t pid, int player);
void mister_serial_controller_scan_end();
void mister_serial_controller_player(const char *id, int player);
void mister_serial_poll();

#endif
