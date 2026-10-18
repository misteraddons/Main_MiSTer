// Production-linked host regressions. No device or on-disk configuration writes.
#include "../input.cpp"
#include <map>
#include <string>
#include <vector>
#include "joymap_test_seams.h"

static std::map<std::string, std::vector<unsigned char> > configs;
char is_menu() { return 1; }
int FileSaveConfig(const char *name, void *data, int size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	configs[name] = std::vector<unsigned char>(bytes, bytes + size);
	return size;
}
int FileLoadConfig(const char *name, void *data, int size)
{
	auto found = configs.find(name);
	if (found == configs.end()) return 0;
	if (data) memcpy(data, found->second.data(), std::min((size_t)size, found->second.size()));
	return (int)found->second.size();
}
int FileDeleteConfig(const char *name) { configs.erase(name); return 1; }

// Fake millisecond clock with the production CheckTimer() contract (0 = expired).
static unsigned long fake_ms = 1000;
unsigned long GetTimer(unsigned long offset) { return fake_ms + offset; }
unsigned long CheckTimer(unsigned long time) { return !time || fake_ms >= time; }

extern "C" uint32_t joymap_test_trigger(const char *);
extern "C" uint32_t joymap_test_cached_trigger(int, const char *, int);
extern "C" int joymap_test_trigger_code(uint32_t);
extern "C" bool joymap_test_export(uint32_t, const char *);
extern "C" bool joymap_test_cached_trigger_pair(const char *, const char *, int, uint32_t *);
extern "C" bool joymap_test_export_map(const uint32_t *, char *, size_t);
extern "C" void joymap_test_core_names(const char *, const uint32_t *, uint32_t *);
extern "C" void joymap_test_core_jn(const char *, const char *, const uint32_t *, uint32_t *);
#ifdef JOYMAP_TEST_HAS_PAD
#include "joymap_pad.h"
extern "C" uint32_t joymap_test_pad_controls(int);
extern "C" uint32_t joymap_test_pad_controls_for_pos(int);
extern "C" bool joymap_test_pad_flash_frames(int);
extern "C" bool joymap_test_pad_draw(int, int, uint32_t);
extern "C" bool joymap_test_pad_centre(int, int, int *, int *);
#endif

static int failures;
static int checks;
static void check(bool passed, const char *name)
{
	++checks;
	printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
	if (!passed) ++failures;
}

static void mouse_session_tests()
{
	memset(&input[0], 0, sizeof(input[0]));
	input[0].mmap[SYS_BTN_A] = BTN_A;
	input[0].mmap[SYS_AXIS_L2] = ABS_Z | MAP_FLAG_TRIGGER | MAP_FLAG_ANALOG;
	input[0].mmap[SYS_AXIS_R2] = ABS_RZ | MAP_FLAG_TRIGGER | MAP_FLAG_ANALOG;
	input[0].mmap[SYS_MS_BTN_L] = BTN_LEFT;
	mapping_dev = -1;
	set_menu_mouse_map(1);
	claim_mapping_device(0);
	input[0].map[SYS_MS_BTN_L] = BTN_RIGHT;
	uint32_t reloaded[NUMBUTTONS] = {};
	save_map("fixture_v4.map", input[0].map, sizeof(input[0].map));
	load_map("fixture_v4.map", reloaded, sizeof(reloaded));
	check(reloaded[SYS_BTN_A] == BTN_A && reloaded[SYS_AXIS_L2] == input[0].mmap[SYS_AXIS_L2] &&
		reloaded[SYS_AXIS_R2] == input[0].mmap[SYS_AXIS_R2] && reloaded[SYS_MS_BTN_L] == BTN_RIGHT,
		"1 mouse-only claim/save/load retains effective GCDB joypad and triggers");
	uint32_t map[NUMBUTTONS] = {};
	map[SYS_AXIS_L2] = input[0].mmap[SYS_AXIS_L2];
	map[SYS_AXIS_R2] = input[0].mmap[SYS_AXIS_R2];
	clear_mapping_slot_for_pos(map, SYS_MAP_BTN_L2);
	clear_mapping_slot_for_pos(map, SYS_MAP_BTN_L2 + 1);
	check(map[SYS_AXIS_L2] && map[SYS_AXIS_R2], "2 mouse skip retains both trigger axes");
	memcpy(input[0].map, map, sizeof(map));
	mapping = 1; mapping_type = 1; mapping_dev = 0; mapping_count = 12; mapping_button = 12;
	step_back_map_setting();
	step_back_map_setting();
	check(input[0].map[SYS_AXIS_L2] && input[0].map[SYS_AXIS_R2], "2 mouse back retains both trigger axes");
	set_menu_mouse_map(0);
	map[SYS_AXIS_L2] = ABS_Z | MAP_FLAG_TRIGGER | MAP_FLAG_ANALOG;
	map[SYS_AXIS_R2] = ABS_RZ | MAP_FLAG_TRIGGER | MAP_FLAG_ANALOG;
	clear_mapping_slot_for_pos(map, SYS_MAP_BTN_L2);
	check(!map[SYS_AXIS_L2] && map[SYS_AXIS_R2], "control gamepad L2 unmap clears only L2");
}

static void trigger_tests()
{
	input_absinfo axis = {};
	axis.minimum = -32768; axis.maximum = 32767;
	const char *tokens[] = { "+a0", "-a0", "+a0~", "-a0~" };
	for (int i = 0; i < 4; ++i)
	{
		const uint32_t map = joymap_test_trigger(tokens[i]);
		const bool negative = i & 1;
		const bool inverted = i & 2;
		const int selected = negative ? axis.minimum : axis.maximum;
		const int opposite = negative ? axis.maximum : axis.minimum;
		char label[160];
		snprintf(label, sizeof(label), "3 %s preserves selected half and reverses endpoints only", tokens[i]);
		check(normalize_analog_trigger(map, selected, &axis) == (inverted ? 0 : 255) &&
			normalize_analog_trigger(map, 0, &axis) == (inverted ? 255 : 0) &&
			normalize_analog_trigger(map, opposite, &axis) == 0, label);
		memset(input[0].mmap, 0, sizeof(input[0].mmap));
		input[0].mmap[SYS_AXIS_L2] = map;
		uint8_t edge = 0;
		const int active = inverted ? 0 : selected;
		trigger_axis_edge(0, ABS_X, active, &axis, 0, &edge);
		snprintf(label, sizeof(label), "3 %s normalized digital edge matches selected-half button code", tokens[i]);
		check(edge == (negative ? 1 : 2) && joymap_test_trigger_code(map) == KEY_EMU + (negative ? 0 : 1), label);
		trigger_axis_edge(0, ABS_X, opposite, &axis, edge, &edge);
		check(edge == 0, "3 opposite half releases digital trigger");
		char exported[64];
		snprintf(exported, sizeof(exported), "lefttrigger:%s,", tokens[i]);
		check(joymap_test_export(map, exported), exported);
	}
	axis.minimum = 0; axis.maximum = 255;
	uint32_t released = joymap_test_cached_trigger(0, "a0", 1);
	uint32_t held = joymap_test_cached_trigger(255, "a0", 1);
	check(released && held && released == held && normalize_analog_trigger(held, 0, &axis) == 0 &&
		normalize_analog_trigger(held, 255, &axis) == 255, "4 held reconnect/cache lookup never reverses plain a0");
	uint32_t held_first = joymap_test_cached_trigger(255, "a0", 2);
	check(held_first && normalize_analog_trigger(held_first, 0, &axis) == 0 &&
		normalize_analog_trigger(held_first, 255, &axis) == 255, "4 first lookup while held never reverses plain a0");
	uint32_t explicit_inverse = joymap_test_cached_trigger(255, "a0~", 3);
	check(normalize_analog_trigger(explicit_inverse, 0, &axis) == 255 &&
		normalize_analog_trigger(explicit_inverse, 255, &axis) == 0, "control explicit a0~ remains inverted");
	axis.minimum = -32768; axis.maximum = 32767;
	input[0].mmap[SYS_AXIS_L2] = joymap_test_trigger("-a0~");
	input[0].mmap[SYS_AXIS_R2] = joymap_test_trigger("+a0~");
	uint8_t edge = 0;
	trigger_axis_edge(0, ABS_X, 0, &axis, 0, &edge);
	check(edge == 3, "3 shared inverted halves press both digital triggers at center");
	trigger_axis_edge(0, ABS_X, -8192, &axis, 3, &edge);
	check(edge == 1, "3 shared inverted halves release positive side independently");
	trigger_axis_edge(0, ABS_X, 8192, &axis, 3, &edge);
	check(edge == 2, "3 shared inverted halves release negative side independently");
	trigger_axis_edge(0, ABS_X, -24000, &axis, 3, &edge);
	check(edge == 1, "3 shared-half hysteresis retains active negative bit from state 3");
	trigger_axis_edge(0, ABS_X, -24000, &axis, 0, &edge);
	check(edge == 0, "control shared-half hysteresis does not newly press below threshold");
	const int base_code = KEY_EMU + (ABS_Z << 1);
	joymap_test_emit_axis(0, 3);
	check(joymap_test_events == std::vector<std::pair<int, int> >{{base_code, 1}, {base_code + 1, 1}},
		"3 source-derived output emits both shared-axis trigger presses");
	joymap_test_emit_axis(3, 1);
	check(joymap_test_events == std::vector<std::pair<int, int> >{{base_code + 1, 0}},
		"3 source-derived output releases positive trigger without repeating negative press");
	joymap_test_emit_axis(3, 2);
	check(joymap_test_events == std::vector<std::pair<int, int> >{{base_code, 0}},
		"3 source-derived output releases negative trigger independently");
	joymap_test_emit_axis(1, 2);
	check(joymap_test_events == std::vector<std::pair<int, int> >{{base_code, 0}, {base_code + 1, 1}},
		"3 source-derived output releases old side before pressing new side");
	joymap_test_emit_axis(1, 1);
	check(joymap_test_events.empty(), "control unchanged axis state emits no synthetic key transition");
}

static void mixed_half_trigger_tests()
{
	input_absinfo axis = {};
	axis.minimum = -32768; axis.maximum = 32767;
	for (int negative = 0; negative < 2; ++negative)
		for (int inverted_left = 0; inverted_left < 2; ++inverted_left)
		{
			char left[8], right[8], label[160];
			snprintf(left, sizeof(left), "%ca0%s", negative ? '-' : '+', inverted_left ? "~" : "");
			snprintf(right, sizeof(right), "%ca0%s", negative ? '-' : '+', inverted_left ? "" : "~");
			uint32_t map[NUMBUTTONS] = {}, cached[NUMBUTTONS] = {};
			const int version = 4 + negative * 2 + inverted_left;
			const bool imported = joymap_test_cached_trigger_pair(left, right, version, map);
			const bool from_cache = joymap_test_cached_trigger_pair(left, right, version, cached);
			snprintf(label, sizeof(label), "3 public GCDB %s/%s retains distinct trigger keys through cache", left, right);
			check(imported && from_cache && map[SYS_BTN_L2] != map[SYS_BTN_R2] &&
				!memcmp(map, cached, sizeof(map)), label);
			char exported[64];
			snprintf(exported, sizeof(exported), "lefttrigger:%s,", left);
			check(joymap_test_export(map[SYS_AXIS_L2], exported), "3 mixed-half L2 metadata export retains original token");
			snprintf(exported, sizeof(exported), "lefttrigger:%s,", right);
			check(joymap_test_export(map[SYS_AXIS_R2], exported), "3 mixed-half R2 metadata export retains original token");
			memcpy(input[0].mmap, map, sizeof(map));
			const int samples[] = {0, negative ? axis.minimum : axis.maximum, negative ? axis.maximum : axis.minimum};
			for (int sample = 0; sample < 3; ++sample)
			{
				const bool expected_left = sample == 0 ? inverted_left : sample == 1 ? !inverted_left : false;
				const bool expected_right = sample == 0 ? !inverted_left : sample == 1 ? inverted_left : false;
				uint8_t edge = 0;
				trigger_axis_edge(0, ABS_X, samples[sample], &axis, 0, &edge);
				const int left_edge = (int)map[SYS_BTN_L2] - KEY_EMU + 1;
				const int right_edge = (int)map[SYS_BTN_R2] - KEY_EMU + 1;
				const bool valid_keys = (left_edge == 1 || left_edge == 2) && (right_edge == 1 || right_edge == 2);
				snprintf(label, sizeof(label), "3 mixed-half %s/%s sample=%d activates only normalized trigger", left, right, sample);
				check(valid_keys && !!(edge & left_edge) == expected_left && !!(edge & right_edge) == expected_right &&
					normalize_analog_trigger(map[SYS_AXIS_L2], samples[sample], &axis) == (expected_left ? 255 : 0) &&
					normalize_analog_trigger(map[SYS_AXIS_R2], samples[sample], &axis) == (expected_right ? 255 : 0), label);
			}
		}
	uint32_t aliases[NUMBUTTONS] = {};
	check(joymap_test_cached_trigger_pair("+a0", "+a0", 8, aliases) &&
		aliases[SYS_AXIS_L2] == aliases[SYS_AXIS_R2] && aliases[SYS_BTN_L2] == aliases[SYS_BTN_R2],
		"control identical GCDB trigger metadata retains intentional digital aliases");
	for (int full_left = 0; full_left < 2; ++full_left)
	{
		uint32_t map[NUMBUTTONS] = {};
		check(joymap_test_cached_trigger_pair(full_left ? "a0" : "+a0", full_left ? "+a0" : "a0", 9 + full_left, map) &&
			map[SYS_BTN_L2] != map[SYS_BTN_R2], "3 distinct full/half-range trigger metadata gets distinct digital keys");
		memcpy(input[0].mmap, map, sizeof(map));
		for (int value : {axis.minimum, 0, 8192, axis.maximum})
		{
			const bool expected_left = normalize_analog_trigger(map[SYS_AXIS_L2], value, &axis) >= TRIGGER_PRESS_LEVEL;
			const bool expected_right = normalize_analog_trigger(map[SYS_AXIS_R2], value, &axis) >= TRIGGER_PRESS_LEVEL;
			uint8_t edge = 0;
			trigger_axis_edge(0, ABS_X, value, &axis, 0, &edge);
			const int left_edge = (int)map[SYS_BTN_L2] - KEY_EMU + 1;
			const int right_edge = (int)map[SYS_BTN_R2] - KEY_EMU + 1;
			check((left_edge == 1 || left_edge == 2) && (right_edge == 1 || right_edge == 2) &&
				!!(edge & left_edge) == expected_left && !!(edge & right_edge) == expected_right,
				"3 full/half-range pair digital states agree with individual analog levels");
		}
	}
	memset(input[0].mmap, 0, sizeof(input[0].mmap));
	input[0].mmap[SYS_AXIS_L2] = joymap_test_trigger("-a0");
	input[0].mmap[SYS_BTN_L2] = BTN_TL2;
	uint8_t edge = 0;
	trigger_axis_edge(0, ABS_X, axis.minimum, &axis, 0, &edge);
	check(edge == 1, "control physical trigger button uses metadata direction fallback");
	input[0].mmap[SYS_BTN_L2] = KEY_EMU + (ABS_Y << 1) + 1;
	trigger_axis_edge(0, ABS_X, axis.minimum, &axis, 0, &edge);
	check(edge == 1, "control synthetic button on different axis uses metadata direction fallback");
}

static void navigation_tests()
{
	memset(&input[0], 0, sizeof(input[0]));
	input[0].quirk = QUIRK_JOYCON;
	input[0].misc_flags = 1U << 31;
	for (int i = SYS_AXIS1_X; i <= SYS_AXIS_Y; ++i) input[0].mmap[i] = MAP_FLAG_ANALOG | MAP_FLAG_INVERT;
	joymap_test_combined_orientation(0, 1);
	bool retained = true;
	for (int i = SYS_AXIS1_X; i <= SYS_AXIS_Y; ++i) retained &= (input[0].mmap[i] & MAP_FLAG_INVERT) != 0;
	check(retained, "5 combined Joy-Con retains saved v4 stick inversion");
	joymap_test_combined_orientation(0, 0);
	bool corrected = true;
	for (int i = SYS_AXIS1_X; i <= SYS_AXIS_Y; ++i) corrected &= (input[0].mmap[i] & MAP_FLAG_INVERT) == 0;
	check(corrected, "control combined Joy-Con inherited sideways orientation corrected");
	for (int axis = 0; axis < 2; ++axis)
		for (int invert = 0; invert < 2; ++invert)
			for (int max = 0; max < 2; ++max)
			{
				uint32_t map = (axis ? ABS_Y : ABS_X) | MAP_FLAG_ANALOG | (invert ? MAP_FLAG_INVERT : 0);
				const int expected = axis * 2 + ((max != invert) ? 0 : 1);
				char label[100];
				snprintf(label, sizeof(label), "6 OSD axis=%d invert=%d endpoint=%d direction=%d", axis, invert, max, expected);
				check(joymap_test_osd_direction(0, axis, map, max) == expected, label);
			}
	set_menu_mouse_map(0);
	mapping = 1; mapping_type = 1; mapping_dev = 0; mapping_count = SYS_MAP_GAMEPAD_COUNT; mapping_button = -6;
	tmp_axis_n = 0; memset(tmp_axis, 0, sizeof(tmp_axis));
	advance_map_button();
	check(mapping_button == -4 && tmp_axis_n == 0, "7 initial DPAD skip advances past both detector prompts");
	append_tmp_axis_for_pos(mapping_button, ABS_X, 0);
	advance_map_button();
	append_tmp_axis_for_pos(mapping_button, ABS_Y, 0);
	check(map_axis_code(tmp_axis[0]) == ABS_X && map_axis_code(tmp_axis[1]) == ABS_Y && tmp_axis_n == 2,
		"7 DPAD skip then stick X/Y occupies correct detector slots");
	mapping_button = -6; tmp_axis_n = 0;
	memset(tmp_axis, 0, sizeof(tmp_axis));
	memset(tmp_axis_stage_count, 0, sizeof(tmp_axis_stage_count));
	advance_map_button();
	step_back_map_setting();
	check(mapping_button == -6 && tmp_axis_n == 0, "7 Previous after paired DPAD skip returns to initial detector");
	advance_map_button();
	append_tmp_axis_for_pos(mapping_button, ABS_X, 0);
	advance_map_button();
	append_tmp_axis_for_pos(mapping_button, ABS_Y, 0);
	check(mapping_button == -3 && map_axis_code(tmp_axis[0]) == ABS_X && map_axis_code(tmp_axis[1]) == ABS_Y,
		"7 skip/back/skip then stick axes preserves horizontal and vertical stage identity");
	mapping_button = -6; tmp_axis_n = 0;
	memset(tmp_axis, 0, sizeof(tmp_axis));
	memset(tmp_axis_stage_count, 0, sizeof(tmp_axis_stage_count));
	append_tmp_axis_for_pos(-6, ABS_X, 0);
	mapping_button = -5;
	record_menu_tmp_axis_stage();
	step_back_map_setting();
	advance_map_button();
	step_back_map_setting();
	check(mapping_button == -6 && tmp_axis_n == 0,
		"7 recaptured DPAD X then back/paired-skip/back discards stale DOWN-stage snapshot");
	for (int standard_hat = 0; standard_hat < 2; ++standard_hat)
	{
		mapping_button = -6; tmp_axis_n = 0;
		memset(tmp_axis, 0, sizeof(tmp_axis));
		memset(tmp_axis_stage_count, 0, sizeof(tmp_axis_stage_count));
		append_tmp_axis_for_pos(-6, ABS_X, 0);
		mapping_button = -5;
		record_menu_tmp_axis_stage();
		step_back_map_setting();
		check(tmp_axis_stage_count[1] == 0,
			"7 returning to initial DPAD detector centrally invalidates stale DOWN snapshot");
		// Replay the direct +=2 transition used by digital-DPAD and standard-hat detection.
		mapping_button += 2;
		step_back_map_setting();
		check(mapping_button == -6 && tmp_axis_n == 0, standard_hat ?
			"7 analog-X/back/implicit standard-hat pair/back restores initial detector" :
			"7 analog-X/back/implicit digital-DPAD pair/back restores initial detector");
	}
}

static input_event key_event(int code, int value)
{
	input_event ev = {};
	ev.type = EV_KEY;
	ev.code = code;
	ev.value = value;
	return ev;
}

static int key(int dev, int code, int value)
{
	const input_event ev = key_event(code, value);
	return osd_combo_event(dev, &ev);
}

static void osd_hold_tests()
{
	uint32_t map[NUMBUTTONS] = {};
	map[SYS_BTN_OSD_1] = OSD_HOLD_MARKER;
	map[SYS_BTN_OSD_2] = BTN_SELECT;
	check(osd_map_is_hold(map) && osd_map_hold_ms(map) == OSD_HOLD_DEFAULT_MS,
		"H1 marker in slot 1 is hold mode with the default 2 s");
	map[SYS_BTN_OSD_1] = OSD_HOLD_MARKER | (10 << 16);
	check(osd_map_hold_ms(map) == 1000, "H1 high half of slot 1 is the hold time in 100 ms units");
	check(osd_map_has_code(map, BTN_SELECT) && !osd_map_has_code(map, OSD_HOLD_MARKER) && !osd_map_has_code(map, map[SYS_BTN_OSD_1]),
		"H1 hold button is a Menu button; the marker never matches a code");
	map[SYS_BTN_OSD_2] = 0;
	osd_map_fill_second(map);
	check(!map[SYS_BTN_OSD_2], "H1 an empty second slot is not filled with the marker");
	uint32_t tap[NUMBUTTONS] = {};
	tap[SYS_BTN_OSD_1] = BTN_MODE;
	osd_map_fill_second(tap);
	check(!osd_map_is_hold(tap) && tap[SYS_BTN_OSD_2] == BTN_MODE, "control a single button still fills the second slot");

	// runtime: tap vs hold on a hold-mode map
	memset(&input[0], 0, sizeof(input[0]));
	input[0].mmap[SYS_BTN_OSD_1] = OSD_HOLD_MARKER;
	input[0].mmap[SYS_BTN_OSD_2] = BTN_SELECT;
	fake_ms = 1000;
	check(key(0, BTN_SELECT, 1) == 0 && input[0].osd_hold_timer == 3000, "H2 hold press starts the timer, no Menu event yet");
	fake_ms = 2500;
	check(!osd_hold_expired(0), "H2 no Menu press before the hold time");
	check(key(0, BTN_SELECT, 0) == 0 && !input[0].osd_hold_timer, "H2 a tap releases without a Menu event");
	fake_ms = 4000;
	check(!osd_hold_expired(0), "H2 a released tap never completes later");
	check(key(0, BTN_SELECT, 1) == 0, "H2 second press starts again");
	fake_ms = 5000;
	check(key(0, BTN_SELECT, 2) == 0 && input[0].osd_hold_timer == 6000, "H2 key auto-repeat does not restart the hold");
	fake_ms = 6000;
	check(osd_hold_expired(0) && input[0].osd_combo == 3, "H2 held for 2 s completes the Menu press");
	check(!osd_hold_expired(0), "H2 a hold completes only once");
	check(key(0, BTN_SELECT, 0) == 2 && !input[0].osd_combo, "H2 release after the hold ends the Menu press");
	check(key(0, BTN_START, 1) == 0 && key(0, BTN_START, 0) == 0 && !input[0].osd_hold_timer,
		"H2 other buttons do not touch the hold");

	// controls: tap and combo maps behave as before
	memset(&input[0], 0, sizeof(input[0]));
	input[0].mmap[SYS_BTN_OSD_1] = BTN_MODE;
	input[0].mmap[SYS_BTN_OSD_2] = BTN_MODE;
	check(key(0, BTN_MODE, 1) == 1 && key(0, BTN_MODE, 0) == 2 && !input[0].osd_hold_timer,
		"control single-button Menu press and release are immediate");
	input[0].mmap[SYS_BTN_OSD_2] = BTN_START;
	input[0].mmap[SYS_BTN_OSD_1] = BTN_SELECT;
	check(key(0, BTN_SELECT, 1) == 0 && key(0, BTN_START, 1) == 1 && key(0, BTN_SELECT, 0) == 2,
		"control combo Menu press needs both buttons");

	// mapping capture at the Menu prompt
	set_menu_mouse_map(0);
	memset(&input[0], 0, sizeof(input[0]));
	mapping = 1; mapping_type = 1; mapping_dev = 0; mapping_count = SYS_MAP_GAMEPAD_COUNT;
	mapping_button = SYS_MAP_POS_OSD; mapping_key_mapped = 0; mapping_last_axis = 0;
	clear_mapping_combo_state();
	fake_ms = 10000;
	check(map_menu_button_code(0, BTN_SELECT, NULL) && input[0].map[SYS_BTN_OSD_1] == BTN_SELECT &&
		input[0].map[SYS_BTN_OSD_2] == BTN_SELECT && get_map_osd_hold() == 0,
		"H3 first Menu button maps as a single button and starts the hold capture");
	fake_ms = 11000;
	check(!map_osd_hold_poll() && get_map_osd_hold() == 50, "H3 hold progress is reported halfway through");
	fake_ms = 12000;
	check(map_osd_hold_poll() && osd_map_is_hold(input[0].map) && input[0].map[SYS_BTN_OSD_2] == BTN_SELECT &&
		get_map_osd_hold() == 100, "H3 held alone for 2 s it becomes hold mode");
	uint32_t reloaded[NUMBUTTONS] = {};
	save_map("hold_v4.map", input[0].map, sizeof(input[0].map));
	load_map("hold_v4.map", reloaded, sizeof(reloaded));
	osd_map_fill_second(reloaded);
	check(osd_map_is_hold(reloaded) && reloaded[SYS_BTN_OSD_1] == OSD_HOLD_MARKER && reloaded[SYS_BTN_OSD_2] == BTN_SELECT,
		"H3 hold mode survives save and load as FF FF 00 00 + button");

	clear_mapping_combo_state();
	memset(input[0].map, 0, sizeof(input[0].map));
	mapping_key_mapped = 0;
	fake_ms = 20000;
	map_menu_button_code(0, BTN_SELECT, NULL);
	mapping_combo_second_key = BTN_START;  // what the combo branch records
	input[0].map[SYS_BTN_OSD_2] = BTN_START;
	fake_ms = 23000;
	check(!map_osd_hold_poll() && input[0].map[SYS_BTN_OSD_1] == BTN_SELECT && input[0].map[SYS_BTN_OSD_2] == BTN_START,
		"H3 a second button first makes a combo, never hold mode");

	clear_mapping_combo_state();
	memset(input[0].map, 0, sizeof(input[0].map));
	mapping_key_mapped = 0;
	fake_ms = 30000;
	map_menu_button_code(0, BTN_SELECT, NULL);
	clear_mapping_combo_state();  // the release path advances the prompt
	fake_ms = 33000;
	check(!map_osd_hold_poll() && !osd_map_is_hold(input[0].map) && get_map_osd_hold() == -1,
		"H3 releasing early keeps a single button");

	memset(input[0].map, 0, sizeof(input[0].map));
	mapping_key_mapped = 0;
	mapping_button = SYS_MAP_POS_OSD;
	fake_ms = 40000;
	map_menu_button_code(0, BTN_SELECT, NULL);
	mapping_button = SYS_MAP_POS_OSD + 1;
	step_back_map_setting();
	check(get_map_osd_hold() == -1 && mapping_button == SYS_MAP_POS_OSD, "H3 Previous cancels a hold capture in progress");
	mapping = 0;

	// GamecontrollerDB export: SDL has no hold mode, so hold exports as the guide button
	char held[512], single[512], combo[512];
	uint32_t hold_map[NUMBUTTONS] = {}, tap_map[NUMBUTTONS] = {}, combo_map[NUMBUTTONS] = {};
	hold_map[SYS_BTN_OSD_1] = OSD_HOLD_MARKER; hold_map[SYS_BTN_OSD_2] = BTN_SELECT;
	tap_map[SYS_BTN_OSD_1] = tap_map[SYS_BTN_OSD_2] = BTN_SELECT;
	combo_map[SYS_BTN_OSD_1] = BTN_SELECT; combo_map[SYS_BTN_OSD_2] = BTN_START;
	const bool exported = joymap_test_export_map(hold_map, held, sizeof(held)) &&
		joymap_test_export_map(tap_map, single, sizeof(single)) && joymap_test_export_map(combo_map, combo, sizeof(combo));
	// the first export also logs the index-map cache fill; compare the mapping lines
	const char *held_line = exported ? strstr(held, "Gamecontrollerdb for mapping:") : NULL;
	const char *single_line = exported ? strstr(single, "Gamecontrollerdb for mapping:") : NULL;
	if (!held_line || !single_line || strcmp(held_line, single_line)) printf("hold:   %s\nsingle: %s\n", held, single);
	check(held_line && single_line && !strcmp(held_line, single_line) && strstr(held_line, "guide:b") && !strstr(held_line, "guide2"),
		"H4 hold map exports exactly like a single guide button");
	check(exported && strstr(combo, "guide:b") && strstr(combo, "guide2:b"), "control combo map exports guide and guide2");
}

static void core_name_tests()
{
	uint32_t mmap[NUMBUTTONS] = {}, map[NUMBUTTONS];
	const int slots[] = { SYS_BTN_A, SYS_BTN_B, SYS_BTN_X, SYS_BTN_Y, SYS_BTN_L, SYS_BTN_R, SYS_BTN_L2, SYS_BTN_R2,
		SYS_BTN_L3, SYS_BTN_R3, SYS_BTN_SELECT, SYS_BTN_START };
	for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) mmap[slots[i]] = BTN_TRIGGER_HAPPY1 + i;

	joymap_test_core_names("Triangle,Circle,Cross,Square,L1,R1,L2,R2,L3,R3,Select,Start", mmap, map);
	check(map[4] == mmap[SYS_BTN_X] && map[5] == mmap[SYS_BTN_A] && map[6] == mmap[SYS_BTN_B] && map[7] == mmap[SYS_BTN_Y],
		"P1 PlayStation face names map Triangle=X, Circle=A, Cross=B, Square=Y");
	check(map[8] == mmap[SYS_BTN_L] && map[9] == mmap[SYS_BTN_R] && map[10] == mmap[SYS_BTN_L2] && map[11] == mmap[SYS_BTN_R2] &&
		map[12] == mmap[SYS_BTN_L3] && map[13] == mmap[SYS_BTN_R3] && map[14] == mmap[SYS_BTN_SELECT] && map[15] == mmap[SYS_BTN_START],
		"P1 L1/R1 map to L/R; L2/R2/L3/R3, Select and Start unchanged");
	joymap_test_core_names("circle(Fire),CROSS", mmap, map);
	check(map[4] == mmap[SYS_BTN_A] && map[5] == mmap[SYS_BTN_B], "P1 PlayStation names ignore case and a (description)");
	joymap_test_core_jn("Fire,Jump", "Circle,Cross", mmap, map);
	check(map[4] == mmap[SYS_BTN_A] && map[5] == mmap[SYS_BTN_B], "P1 PlayStation names work in a jn default map");
	joymap_test_core_jn("Triangle,Circle,Cross,Square", "X,A,B,Y", mmap, map);
	check(map[4] == mmap[SYS_BTN_X] && map[5] == mmap[SYS_BTN_A] && map[6] == mmap[SYS_BTN_B] && map[7] == mmap[SYS_BTN_Y],
		"control a jn map still takes precedence over the J1 names (PSX core)");
	joymap_test_core_names("A,B,X,Y,L,R,Select,Start", mmap, map);
	check(map[4] == mmap[SYS_BTN_A] && map[5] == mmap[SYS_BTN_B] && map[6] == mmap[SYS_BTN_X] && map[7] == mmap[SYS_BTN_Y] &&
		map[8] == mmap[SYS_BTN_L] && map[9] == mmap[SYS_BTN_R], "control SNES names map as before; X stays the SNES X");
}

#ifdef JOYMAP_TEST_HAS_PAD
static void pad_tests()
{
	const uint32_t all = PAD_BIT(PAD_CONTROL_COUNT) - 1;
	const uint32_t snes = PAD_BIT(PAD_START + 1) - 1;  // RIGHT .. START
	check(joymap_test_pad_controls(JOYMAP_PAD_PSX) == all, "D1 PSX pad has every control, L2/R2/L3/R3 and Home included");
	check(joymap_test_pad_controls(JOYMAP_PAD_SNES) == snes, "D1 SNES fallback pad has the twelve SNES controls");
	check(joymap_test_pad_flash_frames(JOYMAP_PAD_PSX) && joymap_test_pad_flash_frames(JOYMAP_PAD_SNES),
		"D2 each control flashes solid with its glyph inverted and nothing else changes");
	check(joymap_test_pad_draw(JOYMAP_PAD_PSX, 8, PAD_BIT(PAD_L2)) && joymap_test_pad_draw(JOYMAP_PAD_SNES, 8, PAD_BIT(PAD_A)),
		"D2 the pad is drawn on OSD rows 8-15");

	bool table = true;
	for (int pos = 0; pos <= SYS_BTN_R; pos++) table &= joymap_test_pad_controls_for_pos(pos) == PAD_BIT(PAD_RIGHT + pos);
	table &= joymap_test_pad_controls_for_pos(SYS_MAP_BTN_L2) == PAD_BIT(PAD_L2) &&
		joymap_test_pad_controls_for_pos(SYS_MAP_BTN_L2 + 3) == PAD_BIT(PAD_R3) &&
		joymap_test_pad_controls_for_pos(SYS_MAP_BTN_SELECT) == PAD_BIT(PAD_SELECT) &&
		joymap_test_pad_controls_for_pos(SYS_MAP_BTN_SELECT + 1) == PAD_BIT(PAD_START) &&
		joymap_test_pad_controls_for_pos(SYS_MAP_POS_OSD) == PAD_BIT(PAD_HOME) &&
		joymap_test_pad_controls_for_pos(-6) == PAD_BIT(PAD_RIGHT) && joymap_test_pad_controls_for_pos(-5) == PAD_BIT(PAD_DOWN) &&
		joymap_test_pad_controls_for_pos(-4) == PAD_BIT(PAD_L3) && joymap_test_pad_controls_for_pos(-1) == PAD_BIT(PAD_R3);
	check(table, "D3 each Define buttons prompt flashes its pad control");
	for (int pad = 0; pad < JOYMAP_PAD_COUNT; pad++)
	{
		int x[PAD_CONTROL_COUNT] = {}, y[PAD_CONTROL_COUNT] = {};
		bool has[PAD_CONTROL_COUNT] = {};
		for (int id = 0; id < PAD_CONTROL_COUNT; id++) has[id] = joymap_test_pad_centre(pad, id, &x[id], &y[id]);
		const int dpad = (x[PAD_LEFT] + x[PAD_RIGHT]) / 2, face = (x[PAD_Y] + x[PAD_A]) / 2;
		bool placed = x[PAD_LEFT] < x[PAD_UP] && x[PAD_UP] < x[PAD_RIGHT] && y[PAD_UP] < y[PAD_LEFT] && y[PAD_LEFT] < y[PAD_DOWN] &&
			x[PAD_Y] < x[PAD_X] && x[PAD_X] < x[PAD_A] && y[PAD_X] < y[PAD_Y] && y[PAD_Y] < y[PAD_B] &&
			dpad < x[PAD_SELECT] && x[PAD_SELECT] < x[PAD_START] && x[PAD_START] < face &&
			x[PAD_L] < x[PAD_SELECT] && x[PAD_R] > x[PAD_START] && y[PAD_L] < y[PAD_UP] && y[PAD_R] < y[PAD_X];
		if (pad == JOYMAP_PAD_PSX)
			placed &= y[PAD_L2] < y[PAD_L] && y[PAD_R2] < y[PAD_R] && x[PAD_L2] < x[PAD_R2] && x[PAD_L3] < x[PAD_R3] &&
				y[PAD_L3] > y[PAD_DOWN] && y[PAD_R3] > y[PAD_X] && x[PAD_SELECT] < x[PAD_HOME] && x[PAD_HOME] < x[PAD_START];
		check(placed, pad == JOYMAP_PAD_PSX ? "D6 PSX regions sit on the right buttons (directions, face, shoulders, sticks)" :
			"D6 SNES regions sit on the right buttons (directions, face, shoulders)");
	}
	bool covered = true;
	for (int pos = -6; pos <= SYS_MAP_POS_OSD + 2; pos++)
		covered &= joymap_test_pad_controls_for_pos(pos) && !(joymap_test_pad_controls_for_pos(pos) & ~joymap_test_pad_controls(JOYMAP_PAD_PSX));
	check(covered, "D3 every prompt of a gamepad session has a control on the PSX pad");

	// Menu prompt: the pad flashes the buttons pressed for Menu
	set_menu_mouse_map(0);
	memset(&input[0], 0, sizeof(input[0]));
	input[0].map[SYS_BTN_SELECT] = BTN_SELECT;
	input[0].map[SYS_BTN_START] = BTN_START;
	mapping = 1; mapping_type = 1; mapping_dev = 0; mapping_count = SYS_MAP_GAMEPAD_COUNT;
	mapping_button = SYS_MAP_POS_OSD; mapping_key_mapped = 0;
	clear_mapping_combo_state();
	int first, second;
	get_map_osd_positions(&first, &second);
	check(first == -1 && second == -1, "D4 nothing flashes before a Menu button is pressed");
	map_menu_button_code(0, BTN_SELECT, NULL);
	get_map_osd_positions(&first, &second);
	check(first == SYS_MAP_BTN_SELECT && second == SYS_MAP_BTN_SELECT, "D4 a single Menu button flashes its position");
	input[0].map[SYS_BTN_OSD_2] = BTN_START;
	get_map_osd_positions(&first, &second);
	check(first == SYS_MAP_BTN_SELECT && second == SYS_MAP_BTN_SELECT + 1, "D4 a combo flashes both buttons");
	input[0].map[SYS_BTN_OSD_1] = OSD_HOLD_MARKER;
	input[0].map[SYS_BTN_OSD_2] = BTN_SELECT;
	get_map_osd_positions(&first, &second);
	check(first == -1 && second == SYS_MAP_BTN_SELECT, "D4 hold mode flashes the hold button, never the marker");
	clear_mapping_combo_state();
	mapping = 0;
}
#endif

#ifdef JOYMAP_TEST_HAS_HOLD_POLL
static void hold_poll_tests()
{
	typedef std::vector<std::pair<int, int> > joy_events;
	memset(&input[0], 0, sizeof(input[0]));
	input[0].num = 1;
	input[0].mmap[SYS_BTN_OSD_1] = OSD_HOLD_MARKER;
	input[0].mmap[SYS_BTN_OSD_2] = BTN_SELECT;
	mapping = 0; joymap_test_is_menu = 0; joymap_test_osd_visible = 0;

	joymap_test_joy.clear();
	fake_ms = 50000; key(0, BTN_SELECT, 1);
	fake_ms = 52000; joymap_test_osd_hold_poll();
	check(joymap_test_joy == joy_events{{BTN_OSD, 1}} && input[0].osd_combo == 0 && input[0].osd_hold_release_at == 52100,
		"H6 a completed hold sends the Menu press at 2 s");
	joymap_test_joy.clear();
	fake_ms = 52050; joymap_test_osd_hold_poll();
	check(joymap_test_joy.empty(), "H6 no Menu release before the click time");
	fake_ms = 52100; joymap_test_osd_hold_poll();
	check(joymap_test_joy == joy_events{{BTN_OSD, 0}}, "H6 the Menu release follows 100 ms later, while the button is still held");
	joymap_test_joy.clear();
	check(key(0, BTN_SELECT, 0) == 0 && joymap_test_joy.empty(), "H6 the button's own release sends nothing more");

	fake_ms = 60000; key(0, BTN_SELECT, 1);
	fake_ms = 62000; joymap_test_osd_hold_poll();
	fake_ms = 62020; key(0, BTN_SELECT, 0);
	fake_ms = 62100; joymap_test_osd_hold_poll();
	check(joymap_test_joy == joy_events{{BTN_OSD, 1}, {BTN_OSD, 0}}, "H6 releasing during the click still completes it");

	joymap_test_joy.clear();
	input[0].mmap[SYS_BTN_OSD_2] = KEY_BACK;  // Android-mode Back, a code below 256
	fake_ms = 70000; key(0, KEY_BACK, 1);
	fake_ms = 72000; joymap_test_osd_hold_poll();
	fake_ms = 72100; joymap_test_osd_hold_poll();
	check(joymap_test_joy == joy_events{{BTN_OSD, 1}, {BTN_OSD, 0}}, "H6 a hold on a key code below 256 sends press and release too");
	input[0].mmap[SYS_BTN_OSD_2] = BTN_SELECT;
	key(0, KEY_BACK, 0);

	joymap_test_joy.clear();
	mapping = 1; mapping_type = 1; osd_timer = 0;
	fake_ms = 80000; key(0, BTN_SELECT, 1);
	fake_ms = 82000; joymap_test_osd_hold_poll();
	check(joymap_test_joy.empty() && osd_timer == 1 && input[0].osd_combo == 3 && key(0, BTN_SELECT, 0) == 2,
		"H6 in a core's button definition a completed hold cancels on release");

	joymap_test_is_menu = 1; osd_timer = 0;
	fake_ms = 90000; key(0, BTN_SELECT, 1);
	fake_ms = 92000; joymap_test_osd_hold_poll();
	check(joymap_test_joy.empty() && !osd_timer, "H6 the Menu core's mapping screen ignores a Menu hold");
	key(0, BTN_SELECT, 0);
	mapping = 0; joymap_test_is_menu = 0;

	input[0].lightgun_req = 1;
	fake_ms = 100000; key(0, BTN_SELECT, 1);
	fake_ms = 102000; joymap_test_osd_hold_poll();
	check(joymap_test_joy.empty() && input[0].lightgun == 1, "H6 with a light gun connected a completed hold toggles light gun mode");
	key(0, BTN_SELECT, 0);
	input[0].lightgun_req = 0; input[0].lightgun = 0;

	osd_autofire_consumed[0] = true;
	fake_ms = 110000; key(0, BTN_SELECT, 1);
	fake_ms = 111000; key(0, BTN_SELECT, 0);
	check(!osd_autofire_consumed[0], "H7 an early hold release clears an advanced-map chord's Menu inhibit");

	fake_ms = 120000; key(0, BTN_SELECT, 1);
	input[0].mmap[SYS_BTN_OSD_1] = BTN_START;  // the map is reloaded as a combo mid-hold
	fake_ms = 122000; joymap_test_osd_hold_poll();
	check(joymap_test_joy.empty(), "H7 a hold timer does not fire once the map is no longer hold mode");
	input[0].mmap[SYS_BTN_OSD_1] = OSD_HOLD_MARKER;
	key(0, BTN_SELECT, 0);

	// a mapping hold action on the hold button (Select = Clear in a core) replaces the Menu hold
	mapping = 1; mapping_type = 1; mapping_dev = 0;
	fake_ms = 130000; key(0, BTN_SELECT, 1);
	mapping_hold_key = BTN_SELECT; mapping_current_dev = 0; mapping_current_key = BTN_SELECT;
	mapping_hold_timer = GetTimer(MAP_HOLD_ACTION_MS); mapping_hold_action = MAP_HOLD_CLEAR;
	fake_ms = 131000;
	const int action = poll_map_hold_action();
	fake_ms = 132000; joymap_test_osd_hold_poll();
	check(action == MAP_HOLD_CLEAR && !input[0].osd_hold_timer && osd_timer != 1 && joymap_test_joy.empty(),
		"H7 a mapping hold action on the Menu button cancels its Menu hold");
	key(0, BTN_SELECT, 0);
	mapping = 0;
}
#endif

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--negative-control"))
	{
		check(normalize_analog_trigger(joymap_test_trigger("a0"), 255, NULL) == 255,
			"negative control deliberately wrong expectation must fail");
		return failures ? 1 : 0;
	}
	mouse_session_tests();
	trigger_tests();
	mixed_half_trigger_tests();
	navigation_tests();
	osd_hold_tests();
#ifdef JOYMAP_TEST_HAS_HOLD_POLL
	hold_poll_tests();
#endif
	core_name_tests();
#ifdef JOYMAP_TEST_HAS_PAD
	pad_tests();
#endif
	printf("Joymap v4 runtime regressions: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
