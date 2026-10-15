#include "hle.h"

/* layers.library, by negative LVO from NDK 3.2 FD/layers_lib.fd, each from its autodoc
 * (layers.doc). The only layers are the ones Intuition makes for windows (hle_intuition.c), so
 * a Layer here is a rectangle of its screen's BitMap, and the graphics HLE clips a RastPort's
 * drawing to its Layer's bounds and clip region. Returns 0 to return to the caller, -1 when the
 * LVO is not implemented. */

void hle_layers_build(struct Amiga *m)
{
	hle_jump_table(m, HLE_LAYERS_BASE, HLE_LAYERS_NEGSIZE, HLE_LIB_LAYERS);
	hle_w8(m, HLE_LAYERS_BASE + LN_TYPE, NT_LIBRARY);
	hle_w32(m, HLE_LAYERS_BASE + LN_NAME, hle_string(m, HLE_STRINGS2 + 0x20, "layers.library"));
	hle_w16(m, HLE_LAYERS_BASE + LIB_NEGSIZE, HLE_LAYERS_NEGSIZE);
	hle_w16(m, HLE_LAYERS_BASE + LIB_POSSIZE, LIB_SIZE);
	hle_w16(m, HLE_LAYERS_BASE + LIB_VERSION, HLE_LAYERS_VERSION);
	hle_w16(m, HLE_LAYERS_BASE + LIB_REVISION, HLE_LAYERS_REVISION);
}

int hle_layers_call(struct Amiga *m, uint32_t lvo)
{
	switch(lvo)
	{
	case 174: /* InstallClipRegion(a0 layer, a1 region): the old region */
		m->cpu.d[0] = hle_r32(m, m->cpu.a[0] + LR_CLIPREGION);
		hle_w32(m, m->cpu.a[0] + LR_CLIPREGION, m->cpu.a[1]);
		return 0;
	default:
		return -1;
	}
}
