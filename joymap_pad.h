#ifndef JOYMAP_PAD_H
#define JOYMAP_PAD_H

#include <stdint.h>

// Pad drawings for the Define buttons screen, from tools/joymap_pads/*.bmp:
// the PlayStation DualShock.
enum { JOYMAP_PAD_PSX, JOYMAP_PAD_COUNT };

// Pad controls. The first twelve follow the system map (SYS_BTN_RIGHT ..
// SYS_BTN_START); on the PlayStation pad A, B, X, Y, L and R are Circle,
// Cross, Triangle, Square, L1 and R1.
enum
{
	PAD_RIGHT, PAD_LEFT, PAD_DOWN, PAD_UP, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R,
	PAD_SELECT, PAD_START, PAD_L2, PAD_R2, PAD_L3, PAD_R3, PAD_HOME, PAD_CONTROL_COUNT
};

#define PAD_BIT(c) (1u << (c))

typedef struct
{
	uint8_t id;                  // PAD_*
	uint8_t x, w, row, rows;     // box: columns x..x+w-1, 8-px rows row..row+rows-1
	const uint8_t *fill, *glyph; // over the box, row by row, one column byte each
} pad_control;

typedef struct
{
	uint8_t w, rows;             // width in pixels, height in 8-px rows
	const uint8_t *pixels;       // rows * w column bytes, bit 0 = top
	const pad_control *controls;
	uint8_t count;
} pad_bitmap;

// The controls a Define buttons prompt asks for (a get_map_button() position).
uint32_t joymap_pad_controls_for_pos(int pos);

// Builds one 8-px row into cols; controls in flash are drawn solid with their
// glyphs inverted. Returns the row width.
int joymap_pad_compose_row(int pad, int row, uint32_t flash, uint8_t *cols);

// Draws the pad on OSD rows first_row onward.
void joymap_pad_draw(int pad, int first_row, uint32_t flash);

#endif
