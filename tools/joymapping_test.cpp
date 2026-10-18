// Host-only wrapper around the production core button-name translation.
// The core's button names come from set_ovr_buttons(); no core is loaded.
#include "../joymapping.cpp"

cfg_t cfg;
void user_io_read_confstr() {}
char *user_io_get_confstr(int) { return NULL; }
char is_psx() { return 0; }

// Same field split as user_io.cpp's substrcpy().
int substrcpy(char *d, const char *s, char idx)
{
	char p = 0;
	char *b = d;
	while (*s)
	{
		if ((p == idx) && *s && (*s != ',')) *d++ = *s;
		if (*s == ',')
		{
			if (p == idx) break;
			p++;
		}
		s++;
	}
	*d = 0;
	return (int)(d - b);
}

// Translates a core's J1 button names against a system map.
extern "C" void joymap_test_core_names(const char *names, const uint32_t *mmap, uint32_t *map)
{
	static char buffer[1024];
	snprintf(buffer, sizeof(buffer), "%s", names);
	set_ovr_buttons(buffer, 0);
	memset(map, 0, NUMBUTTONS * sizeof(*map));
	map_joystick(map, (uint32_t *)mmap);
}
