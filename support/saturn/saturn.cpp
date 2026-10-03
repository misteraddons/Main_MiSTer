#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "../../cheats.h"
#include "saturn.h"
#include "saturn_ramcart.h"

static int need_reset = 0;
uint32_t saturn_frame_cnt = 0;
uint8_t time_mode;

static uint32_t CalcTimerOffset(uint8_t speed) {
	static uint8_t adj0 = 0x1, adj1 = 0x1, adj2 = 0x1;

	uint32_t offs;
	if (speed == 2) {
		offs = 6 + ((adj2 & 0x03) != 0x00 ? 1 : 0);	//6.6
		adj2 <<= 1;
		if (adj2 >= 0x08) adj2 = 0x01;
		adj0 = adj1 = 0x1;
	}
	else if (speed == 1) {
		offs = 13 + ((adj1 & 0x01) != 0x00 ? 1 : 0);	//13.3
		adj1 <<= 1;
		if (adj1 >= 0x08) adj1 = 0x01;
		adj0 = adj2 = 0x1;
	}
	else {
		offs = 16 + ((adj0 & 0x03) != 0x00 ? 1 : 0);	//16.7
		adj0 <<= 1;
		if (adj0 >= 0x08) adj0 = 0x01;
		adj1 = adj2 = 0x1;
	}
	return offs;
}

void saturn_poll()
{
	static unsigned long poll_timer = 0;
	static uint8_t last_req = 255;

	if (!poll_timer || CheckTimer(poll_timer))
	{
		poll_timer = GetTimer(0);

		uint16_t data_in[6];
		uint8_t req = spi_uio_cmd_cont(UIO_CD_GET);
		if (req != last_req)
		{
			last_req = req;

			for (int i = 0; i < 6; i++) data_in[i] = spi_w(0);
			DisableIO();

			satcdd.SetCommand((uint8_t*)data_in);
			satcdd.CommandExec();
		}
		else
			DisableIO();

		satcdd.Process(&time_mode);
		poll_timer += CalcTimerOffset(time_mode);

		uint16_t* s = (uint16_t*)satcdd.GetStatus();
		spi_uio_cmd_cont(UIO_CD_SET);
		for (int i = 0; i < 6; i++) spi_w(s[i]);
		DisableIO();

		satcdd.Update();
		saturn_frame_cnt++;

		unsigned long curr_timer = GetTimer(0);
		if (curr_timer >= poll_timer) {
			poll_timer = curr_timer + CalcTimerOffset(time_mode);
#ifdef SATURN_DEBUG
			user_io_status_set("[63]", 1); 
			printf("\x1b[32mSaturn: ");
			printf("Time over: next = %lu, curr = %lu", poll_timer, curr_timer);
			printf("\n\x1b[0m");
#endif // SATURN_DEBUG
		}
#ifdef SATURN_DEBUG
		else 
			user_io_status_set("[63]", 0);
#endif // SATURN_DEBUG
	}
}

static char buf[1024];

static void saturn_get_save_without_disk(char *buf)
{
	char *p1, *p2;

	if ((p1 = strstr(buf, "disc")) != 0 || (p1 = strstr(buf, "Disc")) != 0 || (p1 = strstr(buf, "DISC")) != 0)
	{
		p2 = p1 + 4;

		if (p1 > buf && *(--p1) == '(') p1--;
		if (p1 > buf && *(--p1) == ' ') p1--;
		if (*p2 == ' ') p2++;
		if ((*p2 >= '0' && *p2 <= '9') || (*p2 >= 'a' && *p2 <= 'f') || (*p2 >= 'A' && *p2 <= 'F')) {
			p2++;
			if (*p2 == ')') p2++;
			p1++;
			strcpy(p1, p2);
		}
	}
}

void saturn_mount_save(const char *filename, bool is_auto)
{
	user_io_set_index(SAVE_IO_INDEX);
	user_io_set_download(1);
	if (strlen(filename))
	{
		if (is_auto)
		{
			FileGenerateSavePath(filename, buf);
			saturn_get_save_without_disk(buf);
		} else {
			strncpy(buf, filename, sizeof(buf));
		}
#ifdef SATURN_DEBUG
		printf("Saturn save filename = %s\n", buf);
#endif // SATURN_DEBUG
		user_io_file_mount(buf, 1, 1);
	}
	else
	{
		user_io_file_mount("", 1);
	}
	user_io_set_download(0);
}

static int saturn_load_rom(const char *basename, const char *name, int sub_index)
{
	strcpy(buf, basename);
	char *p = strrchr(buf, '/');
	if (p)
	{
		p++;
		strcpy(p, name);
		if (user_io_file_tx(buf, sub_index << 6)) return 1;
	}

	return 0;
}

static int saturn_lookup_ram_cart(const char *uuid, uint32_t *cart_type)
{
	fileTextReader reader = {};
	const char *db_path = user_io_make_filepath(HomeDir(), SATURN_RAM_CART_DB_PATH);

	if (!FileExists(db_path, 0))
	{
		printf("Saturn: RAM cart lookup failed, database not found: %s\n", db_path);
		return 0;
	}

	if (!FileOpenTextReader(&reader, db_path))
	{
		printf("Saturn: RAM cart lookup failed, database could not be read: %s\n", db_path);
		return 0;
	}

	while (const char *line = FileReadLine(&reader))
	{
		if (!saturn_ramcart_parse_db_line(line, uuid, cart_type)) continue;

		printf("Saturn: RAM cart lookup %s -> %s\n", uuid, saturn_ramcart_type_name(*cart_type));
		return 1;
	}

	printf("Saturn: RAM cart lookup failed for UUID %s\n", uuid);
	return 0;
}

enum saturn_cart_option_kind
{
	SATURN_CART_OPTION_UNKNOWN,
	SATURN_CART_OPTION_LEGACY, // None..BACKUP: Main selects the cart while None is chosen
	SATURN_CART_OPTION_AUTO,   // Auto..None: Main reports the cart in SATURN_RAM_CART_AUTO_STATUS_OPT
};

// Matches the core's exact Cartridge option list. Any other list leaves the
// cartridge alone, since its values may mean something else.
static saturn_cart_option_kind saturn_cart_option_kind_of(const char *opt)
{
	if (!strcmp(opt, SATURN_RAM_CART_AUTO_OPTION)) return SATURN_CART_OPTION_AUTO;
	if (!strcmp(opt, SATURN_RAM_CART_LEGACY_OPTION)) return SATURN_CART_OPTION_LEGACY;
	return SATURN_CART_OPTION_UNKNOWN;
}

static saturn_cart_option_kind saturn_cart_option()
{
	for (int i = 2; ; i++)
	{
		const char *opt = user_io_get_confstr(i);
		if (!opt) break;

		// Skip page and disable prefixes such as "P1" or "D0".
		const char *cart = strstr(opt, "O" SATURN_RAM_CART_MODE_STATUS_OPT ",");
		if (cart) return saturn_cart_option_kind_of(cart);
	}

	return SATURN_CART_OPTION_UNKNOWN;
}

// Callers only use this while a disc insert holds the core in reset, so a
// running game never sees its cartridge change.
static void saturn_apply_ram_cart(const char *uuid)
{
	const saturn_cart_option_kind option = saturn_cart_option();
	if (option == SATURN_CART_OPTION_UNKNOWN)
	{
		printf("Saturn: RAM cart auto lookup skipped, core has no supported Cartridge option\n");
		return;
	}

	const uint32_t cart_mode = user_io_status_get(SATURN_RAM_CART_MODE_STATUS_OPT);
	const int automatic = (option == SATURN_CART_OPTION_AUTO) ?
		cart_mode == SATURN_RAM_CART_MODE_AUTO :
		cart_mode == SATURN_RAM_CART_NONE || user_io_status_automated(SATURN_RAM_CART_MODE_STATUS_OPT);
	if (!automatic)
	{
		printf("Saturn: RAM cart auto lookup skipped, Cartridge is user-selected\n");
		return;
	}

	uint32_t cart_type = SATURN_RAM_CART_NONE;
	saturn_lookup_ram_cart(uuid, &cart_type);
	user_io_status_set((option == SATURN_CART_OPTION_AUTO) ? SATURN_RAM_CART_AUTO_STATUS_OPT : SATURN_RAM_CART_MODE_STATUS_OPT,
		cart_type, 0, USER_IO_STATUS_AUTOMATED);
	printf("Saturn: RAM cart auto applied -> %s\n", saturn_ramcart_type_name(cart_type));
}

int saturn_cart_option_note(const char *opt, uint32_t value, char *note, size_t note_size)
{
	uint32_t cart_type;
	switch (saturn_cart_option_kind_of(opt))
	{
	case SATURN_CART_OPTION_AUTO:
		if (value != SATURN_RAM_CART_MODE_AUTO) return 0;
		cart_type = user_io_status_get(SATURN_RAM_CART_AUTO_STATUS_OPT);
		break;

	case SATURN_CART_OPTION_LEGACY:
		// The current core has no Auto value; an automatically chosen cart is labelled instead.
		if (!user_io_status_automated(SATURN_RAM_CART_MODE_STATUS_OPT)) return 0;
		cart_type = value;
		break;

	default:
		return 0;
	}

	snprintf(note, note_size, "Auto (%s)", saturn_ramcart_type_name(cart_type));
	return 1;
}

void saturn_set_image(int num, const char *filename)
{
	static char last_dir[1024] = {};

	(void)num;

	int reset_after_insert_disc = !user_io_status_get("[4]");

	satcdd.Unload();
	satcdd.Reset();

	int same_game = *filename && *last_dir && !strncmp(last_dir, filename, strlen(last_dir));
	strcpy(last_dir, filename);
	char *p = strrchr(last_dir, '/');
	if (p) *p = 0;

	if (!same_game && reset_after_insert_disc)
	{
		saturn_mount_save("", true);

		user_io_status_set("[0]", 1);
		saturn_reset();

		// load CD BIOS
		if (!saturn_load_rom(filename, "cd_bios.rom", 0)) // from disk folder.
		{
			if (!saturn_load_rom(last_dir, "cd_bios.rom", 0)) // from parent folder.
			{
				sprintf(buf, "%s/boot.rom", HomeDir()); // from home folder.
				if (!user_io_file_tx(buf))
				{
					Info("CD BIOS not found!", 4000);
				}
			}
		}
	}

	satcdd.wwf_hack = false;
	satcdd.roadrash_hack = false;
	if (strlen(filename))
	{
		if (satcdd.Load(filename) > 0)
		{
			satcdd.SendData = saturn_send_data;

			if (!same_game && reset_after_insert_disc)
			{
				//saturn_load_rom(filename, "cart.rom", 1);
				saturn_mount_save(filename, true);
				//cheats_init(filename, 0);
			}

			if (satcdd.GetBootHeader((uint8_t*)buf) > 0)
			{
				saturn_send_data((uint8_t*)buf, 256, BOOT_IO_INDEX);

				char uuid[32];
				if (!saturn_ramcart_make_uuid((uint8_t*)buf, uuid, sizeof(uuid)))
				{
					printf("Saturn: RAM cart lookup failed, invalid boot header\n");
				}
				else
				{
					user_io_write_gameid(filename, 0, uuid);
					if (!same_game && reset_after_insert_disc) saturn_apply_ram_cart(uuid);
					else printf("Saturn: RAM cart unchanged, disc inserted without a reset\n");
				}

				char *id = buf + 0x20;
				if (!strncmp(id,"T-8126H",7) ||
					!strncmp(id, "T-8120G", 7) ||
					!strncmp(id, "T-8112H", 7) ||
					!strncmp(id, "T-99901G", 8) ||
					!strncmp(id, "T-8112G", 7)) satcdd.wwf_hack = true;
				if (satcdd.wwf_hack) {
#ifdef SATURN_DEBUG
					printf("\x1b[32mSaturn: WWF games hack!!!\n\x1b[0m");
#endif // SATURN_DEBUG
				}

				if (!strncmp(id, "T-5008H", 7) ||
					!strncmp(id, "T-10609G", 8)) satcdd.roadrash_hack = true;
				if (satcdd.roadrash_hack) {
#ifdef SATURN_DEBUG
					printf("\x1b[32mSaturn: Road Rash games hack!!!\n\x1b[0m");
#endif // SATURN_DEBUG
				}
			}
		}
	}

	user_io_status_set("[0]", 0);
}

void saturn_reset() {
	need_reset = 1;
}

int saturn_send_data(uint8_t* buf, int len, uint8_t index) {
	// set index byte
	user_io_set_index(index);

	user_io_set_download(1);
	user_io_file_tx_data(buf, len);
	user_io_set_download(0);
	return 1;
}

static char save_blank[] = {
	0x00, 0x42, 0x00, 0x61, 0x00, 0x63, 0x00, 0x6B, 0x00, 0x55, 0x00, 0x70, 0x00, 0x52, 0x00, 0x61,
	0x00, 0x6D, 0x00, 0x20, 0x00, 0x46, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x6D, 0x00, 0x61, 0x00, 0x74,
	0x00, 0x42, 0x00, 0x61, 0x00, 0x63, 0x00, 0x6B, 0x00, 0x55, 0x00, 0x70, 0x00, 0x52, 0x00, 0x61,
	0x00, 0x6D, 0x00, 0x20, 0x00, 0x46, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x6D, 0x00, 0x61, 0x00, 0x74,
	0x00, 0x42, 0x00, 0x61, 0x00, 0x63, 0x00, 0x6B, 0x00, 0x55, 0x00, 0x70, 0x00, 0x52, 0x00, 0x61,
	0x00, 0x6D, 0x00, 0x20, 0x00, 0x46, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x6D, 0x00, 0x61, 0x00, 0x74,
	0x00, 0x42, 0x00, 0x61, 0x00, 0x63, 0x00, 0x6B, 0x00, 0x55, 0x00, 0x70, 0x00, 0x52, 0x00, 0x61,
	0x00, 0x6D, 0x00, 0x20, 0x00, 0x46, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x6D, 0x00, 0x61, 0x00, 0x74,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void saturn_fill_blanksave(uint8_t *buffer, uint32_t lba)
{
	if (lba == 0 || lba == 128)
	{
		memcpy(buffer, save_blank, 512);
	}
	else
	{
		memset(buffer, 0, 512);
	}
}
