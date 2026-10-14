#include "hle.h"

/* AmigaDOS load files: docs/formats/hunk.md. Block types from NDK 3.2 dos/doshunks.i. */

#define HUNK_CODE 0x3E9u
#define HUNK_DATA 0x3EAu
#define HUNK_BSS 0x3EBu
#define HUNK_RELOC32 0x3ECu
#define HUNK_SYMBOL 0x3F0u
#define HUNK_DEBUG 0x3F1u
#define HUNK_END 0x3F2u
#define HUNK_HEADER 0x3F3u
#define HUNK_OVERLAY 0x3F5u
#define HUNK_BREAK 0x3F6u
#define HUNKF_CHIP 0x40000000u
#define HUNKF_FAST 0x80000000u

struct HunkLoad
{
	struct Amiga *m;
	const struct HleHunkSource *src;
	uint32_t table;      /* the hunk table: one segment address (APTR to the next-pointer) per slot */
	uint32_t table_size;
	uint32_t first, last;
	uint32_t seglist;    /* BPTR of the first hunk loaded */
	int32_t error;
};

static int hunk_long(struct HunkLoad *h, uint32_t *v)
{
	uint8_t b[4];
	if(h->src->read(h->m, h->src->ctx, b, 4) != 4)
		return 0;
	*v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
	return 1;
}

static int hunk_need(struct HunkLoad *h, uint32_t *v)
{
	if(hunk_long(h, v))
		return 1;
	amiga_fatal(h->m, "LoadSeg: the file ends inside a hunk");
	return 0;
}

static int hunk_skip(struct HunkLoad *h, uint32_t longs)
{
	uint32_t v;
	while(longs--)
		if(!hunk_need(h, &v))
			return 0;
	return 1;
}

/* The data address of the hunk in table slot n: the table holds segment BPTRs, and the data
 * follows the segment's next-pointer. */
static uint32_t hunk_addr(struct HunkLoad *h, uint32_t n)
{
	return (hle_r32(h->m, h->table + 4 * n) << 2) + 4;
}

void hle_hunk_unload(struct Amiga *m, uint32_t seglist)
{
	while(seglist)
	{
		uint32_t seg = seglist << 2, next = hle_r32(m, seg);
		hle_free(m, seg - 4, hle_r32(m, seg - 4));
		seglist = next;
	}
}

/* HUNK_HEADER's body: resident library names, table size, first and last slot, and the slots'
 * sizes. Allocates every slot first..last, chained in order; a new table when h->table is 0. */
static int hunk_header(struct HunkLoad *h)
{
	uint32_t v, n, prev = 0, *table_size = &h->table_size;
	if(!hunk_need(h, &v))
		return 0;
	if(v)
	{
		amiga_fatal(h->m, "LoadSeg: resident library names in HUNK_HEADER are not implemented");
		return 0;
	}
	if(!hunk_need(h, table_size) || !hunk_need(h, &h->first) || !hunk_need(h, &h->last))
		return 0;
	if(h->last < h->first || h->last >= *table_size)
	{
		amiga_fatal(h->m, "LoadSeg: HUNK_HEADER slots %u-%u in a table of %u", h->first, h->last, *table_size);
		return 0;
	}
	if(!h->table)
	{
		h->table = hle_alloc(h->m, *table_size * 4, MEMF_PUBLIC | MEMF_CLEAR, 0);
		if(!h->table)
		{
			h->error = HLE_DOS_NO_FREE_STORE;
			return 0;
		}
	}
	for(n = h->first; n <= h->last; n++)
	{
		uint32_t size, req = MEMF_PUBLIC | MEMF_CLEAR, bytes, seg;
		if(!hunk_need(h, &size))
			return 0;
		if((size & HUNKF_CHIP) && (size & HUNKF_FAST))
		{
			amiga_fatal(h->m, "LoadSeg: a hunk size with both memory bits (extended attributes) is not implemented");
			return 0;
		}
		if(size & HUNKF_CHIP)
			req |= MEMF_CHIP;
		if(size & HUNKF_FAST)
			req |= MEMF_FAST;
		bytes = (size & 0x3FFFFFFFu) * 4 + 8;
		seg = hle_alloc(h->m, bytes, req, 0);
		if(!seg)
		{
			h->error = HLE_DOS_NO_FREE_STORE;
			return 0;
		}
		hle_w32(h->m, seg, bytes);
		hle_w32(h->m, seg + 4, 0);
		hle_w32(h->m, h->table + 4 * n, (seg + 4) >> 2);
		if(prev)
			hle_w32(h->m, prev, (seg + 4) >> 2);
		else
			h->seglist = (seg + 4) >> 2;
		prev = seg + 4;
	}
	return 1;
}

/* RELOC32: groups of (count, slot, offsets...), ending with a count of 0. */
static int hunk_reloc(struct HunkLoad *h, uint32_t at)
{
	uint32_t count, slot, off;
	for(;;)
	{
		if(!hunk_need(h, &count))
			return 0;
		if(!count)
			return 1;
		if(!hunk_need(h, &slot))
			return 0;
		if(slot >= h->table_size || !hle_r32(h->m, h->table + 4 * slot))
		{
			amiga_fatal(h->m, "LoadSeg: RELOC32 against hunk %u, which is not loaded", slot);
			return 0;
		}
		while(count--)
		{
			if(!hunk_need(h, &off))
				return 0;
			hle_w32(h->m, at + off, hle_r32(h->m, at + off) + hunk_addr(h, slot));
		}
	}
}

/* The hunks of slots first..last, each ending in HUNK_END or in the next hunk's CODE, DATA or BSS
 * block, which Kickstart 1.3's LoadSeg takes too (docs/formats/hunk.md). Returns the block type
 * after the last one (HUNK_OVERLAY, HUNK_BREAK), 0 at the end of the file, or -1 on failure. */
static int64_t hunk_bodies(struct HunkLoad *h)
{
	uint32_t n = h->first, type, size, at = 0;
	for(;;)
	{
		if(!hunk_long(h, &type))
		{
			if(n <= h->last)
			{
				amiga_fatal(h->m, "LoadSeg: the file ends before hunk %u", n);
				return -1;
			}
			return 0;
		}
		type &= 0x3FFFFFFFu;
		switch(type)
		{
		case HUNK_CODE:
		case HUNK_DATA:
		case HUNK_BSS:
		{
			uint32_t room;
			if(at)
			{
				n++;
				at = 0;
			}
			if(n > h->last)
			{
				amiga_fatal(h->m, "LoadSeg: more hunks than HUNK_HEADER lists");
				return -1;
			}
			if(!hunk_need(h, &size))
				return -1;
			size &= 0x3FFFFFFFu;
			at = hunk_addr(h, n);
			room = hle_r32(h->m, at - 8) - 8;
			if(size * 4 > room)
			{
				amiga_fatal(h->m, "LoadSeg: hunk %u holds %u bytes, HUNK_HEADER gave it %u", n, size * 4, room);
				return -1;
			}
			if(type != HUNK_BSS && size)
			{
				uint8_t *p = mem_ram(h->m, at);
				if(!p || h->src->read(h->m, h->src->ctx, p, size * 4) != size * 4)
				{
					amiga_fatal(h->m, "LoadSeg: the file ends inside hunk %u", n);
					return -1;
				}
			}
			break;
		}
		case HUNK_RELOC32:
			if(!at)
			{
				amiga_fatal(h->m, "LoadSeg: HUNK_RELOC32 before any hunk");
				return -1;
			}
			if(!hunk_reloc(h, at))
				return -1;
			break;
		case HUNK_SYMBOL:
			for(;;)
			{
				if(!hunk_need(h, &size))
					return -1;
				if(!size)
					break;
				if(!hunk_skip(h, (size & 0xFFFFFFu) + 1))
					return -1;
			}
			break;
		case HUNK_DEBUG:
			if(!hunk_need(h, &size) || !hunk_skip(h, size))
				return -1;
			break;
		case HUNK_END:
			if(!at)
			{
				amiga_fatal(h->m, "LoadSeg: HUNK_END before any hunk");
				return -1;
			}
			n++;
			at = 0;
			break;
		case HUNK_OVERLAY:
		case HUNK_BREAK:
			if(n <= h->last)
			{
				amiga_fatal(h->m, "LoadSeg: block 0x%X before hunk %u", type, n);
				return -1;
			}
			return type;
		default:
			amiga_fatal(h->m, "LoadSeg: hunk type 0x%X is not implemented", type);
			return -1;
		}
	}
}

static uint32_t hunk_fail(struct HunkLoad *h, int32_t *error, int own_table)
{
	uint32_t n;
	hle_hunk_unload(h->m, h->seglist);
	if(own_table && h->table)
		hle_free(h->m, h->table, h->table_size * 4);
	else if(h->seglist)
		for(n = h->first; n <= h->last; n++)
			hle_w32(h->m, h->table + 4 * n, 0);
	*error = h->error;
	return 0;
}

uint32_t hle_hunk_load(struct Amiga *m, const struct HleHunkSource *src, uint32_t stream, int32_t *error,
	struct HleHunkOverlay *ov)
{
	struct HunkLoad h = {m, src, 0, 0, 0, 0, 0, 0};
	uint32_t v;
	int64_t next;
	ov->table = 0;
	*error = 0;
	if(!hunk_long(&h, &v) || v != HUNK_HEADER)
	{
		*error = HLE_DOS_OBJECT_WRONG_TYPE;
		return 0;
	}
	if(!hunk_header(&h))
		return hunk_fail(&h, error, 1);
	next = hunk_bodies(&h);
	if(next < 0)
		return hunk_fail(&h, error, 1);
	if(next == 0)
	{
		hle_free(m, h.table, h.table_size * 4);
		return h.seglist;
	}
	if(next != HUNK_OVERLAY)
	{
		amiga_fatal(m, "LoadSeg: HUNK_BREAK outside an overlay node");
		return hunk_fail(&h, error, 1);
	}
	/* The overlay table: its size in longwords, then that many longwords plus one. */
	if(!hunk_need(&h, &v))
		return hunk_fail(&h, error, 1);
	ov->size = (v + 1) * 4;
	ov->table = hle_alloc(m, ov->size, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!ov->table)
	{
		h.error = HLE_DOS_NO_FREE_STORE;
		return hunk_fail(&h, error, 1);
	}
	{
		uint8_t *p = mem_ram(m, ov->table);
		if(!p || src->read(m, src->ctx, p, ov->size) != ov->size)
		{
			amiga_fatal(m, "LoadSeg: the file ends inside HUNK_OVERLAY");
			hle_free(m, ov->table, ov->size);
			ov->table = 0;
			return hunk_fail(&h, error, 1);
		}
	}
	ov->hunks = h.table;
	ov->hunks_size = h.table_size * 4;
	ov->stream = stream;
	/* The overlay manager in the first hunk: +8 the stream (a FileHandle BPTR), +0x0C the overlay
	 * table (APTR), +0x10 the hunk table (BPTR), +0x14 the BCPL global vector, which this
	 * runtime does not have (docs/formats/hunk.md, "Overlays"). */
	v = hunk_addr(&h, h.first);
	hle_w32(m, v + 0x08, stream);
	hle_w32(m, v + 0x0C, ov->table);
	hle_w32(m, v + 0x10, h.table >> 2);
	hle_w32(m, v + 0x14, 0);
	return h.seglist;
}

uint32_t hle_hunk_node(struct Amiga *m, const struct HleHunkSource *src, uint32_t table, int32_t *error)
{
	struct HunkLoad h = {m, src, table, 0, 0, 0, 0, 0};
	uint32_t v;
	int64_t next;
	*error = 0;
	if(!hunk_need(&h, &v))
		return 0;
	if(v != HUNK_HEADER)
	{
		amiga_fatal(m, "LoadSeg: an overlay node starts with 0x%X, not HUNK_HEADER", v);
		return 0;
	}
	if(!hunk_header(&h))
		return hunk_fail(&h, error, 0);
	next = hunk_bodies(&h);
	if(next < 0)
		return hunk_fail(&h, error, 0);
	if(next != HUNK_BREAK)
	{
		amiga_fatal(m, "LoadSeg: an overlay node ends without HUNK_BREAK");
		return hunk_fail(&h, error, 0);
	}
	return h.seglist;
}
