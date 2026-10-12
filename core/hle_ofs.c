#include "hle.h"

#include <string.h>

/* The Old File System on a double-density ADF, read straight from the image. Block layout per
 * docs/formats/adf.md, "OFS files and directories". */

#define OFS_BSIZE 512u
#define OFS_ROOT 880u
#define OFS_DATA 488u
#define OFS_TABLE 72u
#define OFS_T_HEADER 2u
#define OFS_T_DATA 8u
#define OFS_T_LIST 16u

static uint32_t ofs_l(const uint8_t *adf, uint32_t block, uint32_t off)
{
	const uint8_t *p = adf + block * OFS_BSIZE + off;
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int ofs_valid(uint32_t block)
{
	return block >= 2 && block < AMIGA_ADF_SIZE / OFS_BSIZE;
}

static int ofs_upper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

uint32_t hle_ofs_hash(const char *name)
{
	uint32_t h = (uint32_t)strlen(name), i;
	for(i = 0; name[i]; i++)
		h = (h * 13 + (uint32_t)ofs_upper((uint8_t)name[i])) & 0x7FF;
	return h % OFS_TABLE;
}

static void ofs_bstr(const uint8_t *adf, uint32_t block, uint32_t off, char *out, uint32_t max)
{
	uint32_t n = adf[block * OFS_BSIZE + off], i;
	if(n > max - 1)
		n = max - 1;
	for(i = 0; i < n; i++)
		out[i] = (char)adf[block * OFS_BSIZE + off + 1 + i];
	out[n] = 0;
}

int hle_ofs_volume(const uint8_t *adf, char name[32])
{
	if(adf[0] != 'D' || adf[1] != 'O' || adf[2] != 'S' || adf[3] != 0)
		return -1;
	if(ofs_l(adf, OFS_ROOT, 0) != OFS_T_HEADER || ofs_l(adf, OFS_ROOT, 508) != 1)
		return -1;
	ofs_bstr(adf, OFS_ROOT, 432, name, 32);
	return 0;
}

/* The root block's last-alteration date of the volume (+472: days, minutes, ticks). */
void hle_ofs_volume_date(const uint8_t *adf, uint32_t *days, uint32_t *mins, uint32_t *ticks)
{
	*days = ofs_l(adf, OFS_ROOT, 472);
	*mins = ofs_l(adf, OFS_ROOT, 476);
	*ticks = ofs_l(adf, OFS_ROOT, 480);
}

static int ofs_same(const char *a, const char *b)
{
	for(; *a && *b; a++, b++)
		if(ofs_upper((uint8_t)*a) != ofs_upper((uint8_t)*b))
			return 0;
	return *a == *b;
}

int32_t hle_ofs_lookup(const uint8_t *adf, uint32_t dir, const char *name)
{
	uint32_t at, guard;
	char here[32];
	if(!ofs_valid(dir) || ofs_l(adf, dir, 0) != OFS_T_HEADER || strlen(name) > 30)
		return -1;
	at = ofs_l(adf, dir, 24 + 4 * hle_ofs_hash(name));
	for(guard = 0; at && guard < AMIGA_ADF_SIZE / OFS_BSIZE; guard++)
	{
		if(!ofs_valid(at) || ofs_l(adf, at, 0) != OFS_T_HEADER)
			return -1;
		ofs_bstr(adf, at, 432, here, sizeof here);
		if(ofs_same(here, name))
			return (int32_t)at;
		at = ofs_l(adf, at, 496);
	}
	return -1;
}

int hle_ofs_entry(const uint8_t *adf, uint32_t block, struct HleOfsEntry *e)
{
	if(!ofs_valid(block) || ofs_l(adf, block, 0) != OFS_T_HEADER)
		return -1;
	memset(e, 0, sizeof *e);
	e->type = (int32_t)ofs_l(adf, block, 508);
	e->size = e->type < 0 ? ofs_l(adf, block, 324) : 0;
	e->protect = ofs_l(adf, block, 320);
	e->days = ofs_l(adf, block, 420);
	e->mins = ofs_l(adf, block, 424);
	e->ticks = ofs_l(adf, block, 428);
	e->parent = ofs_l(adf, block, 500);
	e->blocks = e->type < 0 ? (e->size + OFS_DATA - 1) / OFS_DATA : 0;
	ofs_bstr(adf, block, 432, e->name, sizeof e->name);
	ofs_bstr(adf, block, 328, e->comment, sizeof e->comment);
	return 0;
}

/* The data block holding byte k * 488 of the file: the header's table, then the extension chain. */
static uint32_t ofs_data_block(const uint8_t *adf, uint32_t header, uint32_t k)
{
	uint32_t b = header, n;
	while(k >= OFS_TABLE)
	{
		b = ofs_l(adf, b, 504);
		if(!ofs_valid(b) || ofs_l(adf, b, 0) != OFS_T_LIST)
			return 0;
		k -= OFS_TABLE;
	}
	if(k >= ofs_l(adf, b, 8))
		return 0;
	n = ofs_l(adf, b, 308 - 4 * k);
	if(!ofs_valid(n) || ofs_l(adf, n, 0) != OFS_T_DATA)
		return 0;
	return n;
}

uint32_t hle_ofs_read(const uint8_t *adf, uint32_t header, uint32_t pos, uint8_t *out, uint32_t n)
{
	uint32_t size, done = 0;
	if(!ofs_valid(header) || ofs_l(adf, header, 0) != OFS_T_HEADER || (int32_t)ofs_l(adf, header, 508) >= 0)
		return 0;
	size = ofs_l(adf, header, 324);
	if(pos >= size)
		return 0;
	if(n > size - pos)
		n = size - pos;
	while(done < n)
	{
		uint32_t at = pos + done, off = at % OFS_DATA, blk = ofs_data_block(adf, header, at / OFS_DATA), have, take;
		if(!blk)
			break;
		have = ofs_l(adf, blk, 12);
		if(have > OFS_DATA || off >= have)
			break;
		take = have - off;
		if(take > n - done)
			take = n - done;
		memcpy(out + done, adf + blk * OFS_BSIZE + 24 + off, take);
		done += take;
	}
	return done;
}
