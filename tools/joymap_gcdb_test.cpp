// Host-only wrappers around the production GCDB parser. Synthetic evdev axes.
#include "../gamecontroller_db.cpp"
#include <stdarg.h>
#include <string>
#include <unistd.h>

static input_absinfo fixture_axis;
static char fixture_line[256];
static int fixture_line_read;

extern "C" int __wrap_ioctl(int, unsigned long request, ...)
{
	va_list args;
	va_start(args, request);
	void *out = va_arg(args, void *);
	va_end(args);
	if (request == EVIOCGABS(ABS_X))
	{
		*(input_absinfo *)out = fixture_axis;
		return 0;
	}
	if (_IOC_NR(request) == _IOC_NR(EVIOCGBIT(EV_ABS, 0)))
	{
		memset(out, 0, _IOC_SIZE(request));
		((unsigned char *)out)[0] = 1;
		return 0;
	}
	if (_IOC_NR(request) == _IOC_NR(EVIOCGBIT(EV_KEY, 0)))
	{
		memset(out, 0, _IOC_SIZE(request));
		return 0;
	}
	return -1;
}

fileTextReader::fileTextReader() {}
fileTextReader::~fileTextReader() {}
bool FileOpenTextReader(fileTextReader *, const char *) { fixture_line_read = 0; return true; }
const char *FileReadLine(fileTextReader *) { return fixture_line_read++ ? NULL : fixture_line; }
char *user_io_get_core_name(int) { static char name[] = "Menu"; return name; }

extern "C" uint32_t joymap_test_trigger(const char *token)
{
	return trigger_axis_map_from_token(token, KEY_EMU + 1);
}

extern "C" uint32_t joymap_test_cached_trigger(int captured, const char *token, int version)
{
	fixture_axis = {};
	fixture_axis.maximum = 255;
	fixture_axis.value = captured;
#ifdef JOYMAP_TEST_HAS_BASELINES
	gcdb_reset_axis_baselines();
	gcdb_capture_axis_baseline(17);
#endif
	snprintf(fixture_line, sizeof(fixture_line), "030000003412000078560000%02x000000,Fixture,lefttrigger:%s,platform:Linux,", version, token);
	uint32_t map[NUMBUTTONS] = {};
	if (!gcdb_map_for_controller(3, 0x1234, 0x5678, version, 17, map)) return 0;
	return map[SYS_AXIS_L2];
}

extern "C" int joymap_test_trigger_code(uint32_t map)
{
	return trigger_button_code_from_axis_map(map);
}

extern "C" bool joymap_test_cached_trigger_pair(const char *left, const char *right, int version, uint32_t *map)
{
	fixture_axis = {};
	fixture_axis.minimum = -32768;
	fixture_axis.maximum = 32767;
	snprintf(fixture_line, sizeof(fixture_line), "030000003412000078560000%02x000000,Fixture,lefttrigger:%s,righttrigger:%s,platform:Linux,", version, left, right);
	memset(map, 0, NUMBUTTONS * sizeof(*map));
	return gcdb_map_for_controller(3, 0x1234, 0x5678, version, 17, map);
}

extern "C" bool joymap_test_export(uint32_t map, const char *expected)
{
	uint16_t axes[GCDB_AXIS_MAP_SIZE];
	memset(axes, 0xff, sizeof(axes));
	axes[0] = ABS_X;
	FILE *capture = tmpfile();
	if (!capture) return false;
	fflush(stdout);
	int saved = dup(STDOUT_FILENO);
	if (saved < 0) { fclose(capture); return false; }
	if (dup2(fileno(capture), STDOUT_FILENO) < 0) { close(saved); fclose(capture); return false; }
	bool printed = print_axis_mapping("lefttrigger", map, axes);
	fflush(stdout);
	dup2(saved, STDOUT_FILENO);
	close(saved);
	rewind(capture);
	char result[128] = {};
	const size_t length = fread(result, 1, sizeof(result) - 1, capture);
	fclose(capture);
	return printed && length == strlen(expected) && !strcmp(result, expected);
}
