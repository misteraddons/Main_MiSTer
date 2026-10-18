// Host-only wrapper around the Define buttons pad compositor; OSD rows are
// captured instead of sent.
#include "../joymap_pad.cpp"

static unsigned char drawn[32][256];
static int drawn_width[32];
static uint32_t drawn_rows;

void OsdDrawBitmapRow(unsigned char n, const unsigned char *cols, int width)
{
	if (n >= 32 || width > 256) return;
	memcpy(drawn[n], cols, width);
	drawn_width[n] = width;
	drawn_rows |= 1u << n;
}

// Bit set of the PAD_* controls a pad has.
extern "C" uint32_t joymap_test_pad_controls(int pad)
{
	uint32_t ids = 0;
	for (int i = 0; i < joymap_pads[pad].count; i++) ids |= PAD_BIT(joymap_pads[pad].controls[i].id);
	return ids;
}

extern "C" uint32_t joymap_test_pad_controls_for_pos(int pos)
{
	return joymap_pad_controls_for_pos(pos);
}

// Every control's flash frame is (drawing | fill) ^ glyph inside its box and
// the drawing elsewhere; its glyph lies inside both the drawing and the fill,
// and the fill is not empty.
extern "C" bool joymap_test_pad_flash_frames(int pad)
{
	const pad_bitmap *p = &joymap_pads[pad];
	uint8_t cols[256];
	for (int i = 0; i < p->count; i++)
	{
		const pad_control *c = &p->controls[i];
		int filled = 0;
		for (int row = 0; row < p->rows; row++)
		{
			joymap_pad_compose_row(pad, row, PAD_BIT(c->id), cols);
			const uint8_t *base = p->pixels + row * p->w;
			for (int x = 0; x < p->w; x++)
			{
				uint8_t want = base[x];
				if (row >= c->row && row < c->row + c->rows && x >= c->x && x < c->x + c->w)
				{
					const int k = (row - c->row) * c->w + (x - c->x);
					if ((c->glyph[k] & ~(base[x] & c->fill[k])) != 0) return false;
					want = (base[x] | c->fill[k]) ^ c->glyph[k];
					filled |= c->fill[k];
				}
				if (cols[x] != want) return false;
			}
		}
		if (!filled) return false;
	}
	for (int row = 0; row < p->rows; row++)
	{
		joymap_pad_compose_row(pad, row, 0, cols);
		if (memcmp(cols, p->pixels + row * p->w, p->w)) return false;
	}
	return true;
}

// joymap_pad_draw() sends rows first_row.. with the composed contents.
extern "C" bool joymap_test_pad_draw(int pad, int first_row, uint32_t flash)
{
	drawn_rows = 0;
	joymap_pad_draw(pad, first_row, flash);
	const pad_bitmap *p = &joymap_pads[pad];
	uint8_t cols[256];
	for (int row = 0; row < p->rows; row++)
	{
		joymap_pad_compose_row(pad, row, flash, cols);
		if (!(drawn_rows & (1u << (first_row + row))) || drawn_width[first_row + row] != p->w ||
			memcmp(drawn[first_row + row], cols, p->w))
			return false;
	}
	return drawn_rows == (((1u << p->rows) - 1) << first_row);
}
