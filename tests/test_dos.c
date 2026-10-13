#include "hle.h"
#include "game.h"

#include <stdio.h>
#include <string.h>

/* The OFS reader and the hunk loader over synthetic images: a volume built here block by block
 * (docs/formats/adf.md, "OFS files and directories") and load files built longword by longword
 * (docs/formats/hunk.md). No game bytes. */

const struct GameDef game_def = {
	.id = "test",
	.title = "Test Game",
};

static int failures;
static struct Amiga m;
static uint8_t adf[AMIGA_ADF_SIZE];
static uint32_t next_block = 881;

static void check(int ok, const char *name)
{
	printf("%s %s\n", ok ? "ok" : "FAIL", name);
	if(!ok)
		failures++;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t *blk(uint32_t n)
{
	return adf + n * 512;
}

static void set_name(uint32_t b, const char *name)
{
	size_t n = strlen(name);
	blk(b)[432] = (uint8_t)n;
	memcpy(blk(b) + 433, name, n);
}

/* A header block in `dir`, put at the head of its hash chain. */
static uint32_t entry(uint32_t dir, const char *name, int32_t type)
{
	uint32_t b = next_block++, slot = 24 + 4 * hle_ofs_hash(name);
	put32(blk(b), 2);
	put32(blk(b) + 4, b);
	set_name(b, name);
	put32(blk(b) + 496, get32(blk(dir) + slot));
	put32(blk(b) + 500, dir);
	put32(blk(b) + 508, (uint32_t)type);
	put32(blk(dir) + slot, b);
	return b;
}

static uint8_t file_byte(uint32_t i)
{
	return (uint8_t)(i * 7 + i / 488);
}

/* A file of `size` bytes: data blocks of 488 bytes, 72 to the header and to each extension. */
static uint32_t file(uint32_t dir, const char *name, uint32_t size)
{
	uint32_t h = entry(dir, name, (uint32_t)-3), table = h, k, count = (size + 487) / 488;
	put32(blk(h) + 324, size);
	for(k = 0; k < count; k++)
	{
		uint32_t d = next_block++, n = size - k * 488 < 488 ? size - k * 488 : 488, i;
		if(k && k % 72 == 0)
		{
			uint32_t e = d;
			d = next_block++;
			put32(blk(e), 16);
			put32(blk(e) + 4, e);
			put32(blk(e) + 500, h);
			put32(blk(e) + 508, (uint32_t)-3);
			put32(blk(table) + 504, e);
			table = e;
		}
		put32(blk(d), 8);
		put32(blk(d) + 4, h);
		put32(blk(d) + 8, k + 1);
		put32(blk(d) + 12, n);
		for(i = 0; i < n; i++)
			blk(d)[24 + i] = file_byte(k * 488 + i);
		put32(blk(table) + 308 - 4 * (k % 72), d);
		put32(blk(table) + 8, k % 72 + 1);
	}
	return h;
}

static void test_ofs(void)
{
	static uint8_t out[40000];
	char vol[32], other[8];
	uint32_t s, big, alpha, collide = 0, n, i, ok;
	struct HleOfsEntry e;

	memcpy(adf, "DOS\0", 4);
	put32(blk(880), 2);
	put32(blk(880) + 12, 72);
	put32(blk(880) + 508, 1);
	set_name(880, "Synthetic");
	s = entry(880, "s", 2);
	file(s, "Startup-Sequence", 25);
	big = file(880, "Big", 40000);
	alpha = file(880, "Alpha", 10);
	for(i = 0; i < 1000 && !collide; i++)
	{
		snprintf(other, sizeof other, "x%u", i);
		if(hle_ofs_hash(other) == hle_ofs_hash("Alpha"))
			collide = file(880, other, 20);
	}

	check(hle_ofs_volume(adf, vol) == 0 && !strcmp(vol, "Synthetic"), "the volume name is read");
	adf[3] = 1;
	check(hle_ofs_volume(adf, vol) < 0, "an FFS volume is refused");
	adf[3] = 0;
	check(collide && hle_ofs_lookup(adf, 880, "Alpha") == (int32_t)alpha && hle_ofs_lookup(adf, 880, other) == (int32_t)collide,
		"two names in one hash slot are both found through the chain");
	check(hle_ofs_lookup(adf, 880, "Missing") < 0, "a missing name is not found");
	check(hle_ofs_lookup(adf, (uint32_t)hle_ofs_lookup(adf, 880, "S"), "STARTUP-sequence") > 0,
		"a mixed-case lookup finds the file in a subdirectory");
	check(hle_ofs_entry(adf, s, &e) == 0 && e.type == 2 && e.parent == 880 && !strcmp(e.name, "s"),
		"a directory's entry gives its type, name and parent");
	check(hle_ofs_entry(adf, big, &e) == 0 && e.type == -3 && e.size == 40000 && e.blocks == 82,
		"a file's entry gives its size and data blocks");

	n = hle_ofs_read(adf, big, 0, out, sizeof out);
	for(i = 0, ok = 1; i < n; i++)
		ok &= out[i] == file_byte(i);
	check(n == 40000 && ok, "a whole read through the extension block is byte-exact");
	n = hle_ofs_read(adf, big, 72 * 488 - 10, out, 20);
	for(i = 0, ok = 1; i < n; i++)
		ok &= out[i] == file_byte(72 * 488 - 10 + i);
	check(n == 20 && ok, "a read across the 72-block boundary is byte-exact");
	check(hle_ofs_read(adf, big, 39990, out, 100) == 10 && out[9] == file_byte(39999), "a read stops at the end of the file");
	check(hle_ofs_read(adf, big, 40000, out, 1) == 0, "a read at the end of the file copies nothing");
	check(hle_ofs_read(adf, s, 0, out, 1) == 0, "a directory reads as nothing");
}

/* --- hunks ---------------------------------------------------------------------------------- */

struct Buf
{
	uint8_t b[512];
	uint32_t len, pos;
};

static uint32_t buf_read(struct Amiga *mm, void *ctx, uint8_t *out, uint32_t n)
{
	struct Buf *f = ctx;
	(void)mm;
	if(n > f->len - f->pos)
		n = f->len - f->pos;
	memcpy(out, f->b + f->pos, n);
	f->pos += n;
	return n;
}

static void emit(struct Buf *f, uint32_t v)
{
	put32(f->b + f->len, v);
	f->len += 4;
}

static uint32_t seg_data(uint32_t bptr)
{
	return (bptr << 2) + 4;
}

static void test_hunks(void)
{
	static struct Buf f, ov;
	struct HleHunkSource src = {buf_read, &f}, osrc = {buf_read, &ov};
	struct HleHunkOverlay o;
	int32_t err;
	uint32_t seg, h0, h1, h2, table, node, root, node2_at, i;

	amiga_init(&m);
	hle_build(&m);

	/* CODE (2 longwords, both relocated), DATA in chip RAM, BSS of 4 longwords. */
	emit(&f, 0x3F3); emit(&f, 0); emit(&f, 3); emit(&f, 0); emit(&f, 2);
	emit(&f, 2); emit(&f, 0x40000001); emit(&f, 4);
	emit(&f, 0x3E9); emit(&f, 2); emit(&f, 0x10); emit(&f, 4);
	emit(&f, 0x3EC); emit(&f, 1); emit(&f, 2); emit(&f, 0); emit(&f, 1); emit(&f, 1); emit(&f, 4); emit(&f, 0);
	emit(&f, 0x3F2);
	emit(&f, 0x3EA); emit(&f, 1); emit(&f, 0xCAFEBABE); emit(&f, 0x3F2);
	emit(&f, 0x3EB); emit(&f, 4); emit(&f, 0x3F2);
	seg = hle_hunk_load(&m, &src, 0, &err, &o);
	h0 = seg_data(seg);
	h1 = seg ? seg_data(hle_r32(&m, seg << 2)) : 0;
	h2 = h1 ? seg_data(hle_r32(&m, h1 - 4)) : 0;
	check(seg && !err && !o.table && h2 && !hle_r32(&m, h2 - 4), "a three-hunk file loads as a chain of three segments");
	check(h0 >= AMIGA_SLOW_BASE && h1 < AMIGA_CHIP_SIZE, "the chip hunk lands in chip RAM, the others in slow RAM");
	check(hle_r32(&m, h0) == h2 + 0x10 && hle_r32(&m, h0 + 4) == h1 + 4, "RELOC32 adds each target hunk's address");
	check(hle_r32(&m, h1) == 0xCAFEBABE && hle_r32(&m, h2 + 12) == 0, "data is copied and BSS is clear");
	check(hle_r32(&m, h0 - 8) == 2 * 4 + 8, "a segment records its allocation size before its next-pointer");
	hle_hunk_unload(&m, seg);

	/* A root of one hunk, an overlay table of 4 longwords, and two nodes. */
	emit(&ov, 0x3F3); emit(&ov, 0); emit(&ov, 3); emit(&ov, 0); emit(&ov, 0); emit(&ov, 6);
	emit(&ov, 0x3E9); emit(&ov, 6);
	for(i = 0; i < 6; i++)
		emit(&ov, i == 1 ? 0xABCD : 0);
	emit(&ov, 0x3F2);
	emit(&ov, 0x3F5); emit(&ov, 3); emit(&ov, 4); emit(&ov, 0); emit(&ov, 0); emit(&ov, 7);
	emit(&ov, 0x3F3); emit(&ov, 0); emit(&ov, 3); emit(&ov, 1); emit(&ov, 1); emit(&ov, 1);
	emit(&ov, 0x3E9); emit(&ov, 1); emit(&ov, 8);
	emit(&ov, 0x3EC); emit(&ov, 1); emit(&ov, 0); emit(&ov, 0); emit(&ov, 0);
	emit(&ov, 0x3F2); emit(&ov, 0x3F6);
	node2_at = ov.len;
	emit(&ov, 0x3F3); emit(&ov, 0); emit(&ov, 3); emit(&ov, 1); emit(&ov, 2); emit(&ov, 1); emit(&ov, 1);
	emit(&ov, 0x3EA); emit(&ov, 1); emit(&ov, 0x55); emit(&ov, 0x3F2);
	emit(&ov, 0x3EA); emit(&ov, 1); emit(&ov, 0x66);
	emit(&ov, 0x3F2); emit(&ov, 0x3F6);
	seg = hle_hunk_load(&m, &osrc, 0x1234, &err, &o);
	root = seg_data(seg);
	table = hle_r32(&m, root + 0x10) << 2;
	check(seg && o.table && o.size == 16 && hle_r32(&m, o.table) == 4 && hle_r32(&m, o.table + 12) == 7,
		"HUNK_OVERLAY's table is copied");
	check(hle_r32(&m, root + 8) == 0x1234 && hle_r32(&m, root + 0x0C) == o.table && table == o.hunks &&
		hle_r32(&m, table) == seg && !hle_r32(&m, table + 4),
		"the manager gets the stream, the overlay table and the hunk table");

	node = hle_hunk_node(&m, &osrc, table, &err);
	check(node && hle_r32(&m, table + 4) == node && hle_r32(&m, seg_data(node)) == root + 8,
		"a node fills its slot and relocates against the root");
	ov.pos = node2_at;
	node = hle_hunk_node(&m, &osrc, table, &err);
	check(node && hle_r32(&m, table + 4) == node && hle_r32(&m, seg_data(hle_r32(&m, table + 8))) == 0x66 &&
		hle_r32(&m, (node << 2)) == hle_r32(&m, table + 8),
		"a node of two hunks fills both slots, chained");
}

int main(void)
{
	test_ofs();
	test_hunks();
	return failures ? 1 : 0;
}
