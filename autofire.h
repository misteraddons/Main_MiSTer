#ifndef AUTOFIRE_H
#define AUTOFIRE_H

#include <stdint.h>

const char *get_autofire_rate_hz_button(int player, uint32_t code);
const char *get_autofire_rate_hz(int rate_idx);
int get_autofire_rate_count();
int get_autofire_code_idx(int player, uint32_t code);
bool is_autofire_enabled(int player, uint32_t code);
void clear_autofire(int player);
void inc_autofire_code(int player, uint32_t code, uint32_t mask);
bool parse_autofire_cfg();
bool get_autofire_bit(int player, uint32_t code, uint32_t frame_count);
bool get_autofire_bit_for_rate(int rate_idx, uint32_t frame_count);
bool get_autofire_cycle_for_rate(int rate_idx, uint64_t *cycle_mask, int *cycle_length);
void set_autofire_code(int player, uint32_t code, uint32_t mask, int index, bool locked = false);

#endif
