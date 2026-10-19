#include "amiga.h"
#include "game.h"
#include "image.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_identify(const uint8_t *adf, uint32_t size)
{
	return size == AMIGA_ADF_SIZE && adf[0] == 4;
}

static const struct GameDisk test_disks[] = {
	{"disk1", "Disk 1", 0, test_identify},
	{"disk2", "Disk 2", 0, 0},
	{"save", "Save", 1, 0},
};

const struct GameDef game_def = {
	.id = "test",
	.title = "Test Game",
	.disks = test_disks,
	.disk_count = 3,
	.drives = {0, 2, -1, -1},
};

static int failures;

static void check(int ok, const char *name, const char *detail)
{
	if(ok)
		printf("ok %s\n", name);
	else
	{
		printf("FAIL %s: %s\n", name, detail);
		failures++;
	}
}

static uint8_t *image_filled(uint8_t fill, size_t size)
{
	uint8_t *p = malloc(size);
	if(!p)
		exit(2);
	memset(p, fill, size);
	return p;
}

static uint8_t *copy_bytes(const uint8_t *p, size_t n)
{
	uint8_t *c = malloc(n);
	if(!c)
		exit(2);
	memcpy(c, p, n);
	return c;
}

struct ZipEntry
{
	const char *name;
	const uint8_t *data;
	size_t size;
};

static uint8_t *make_zip(const struct ZipEntry *entries, int count, size_t *size)
{
	mz_zip_archive zip;
	void *out = 0;
	int i;
	memset(&zip, 0, sizeof zip);
	if(!mz_zip_writer_init_heap(&zip, 0, 0))
		exit(2);
	for(i = 0; i < count; i++)
		if(!mz_zip_writer_add_mem(&zip, entries[i].name, entries[i].data, entries[i].size, MZ_DEFAULT_COMPRESSION))
			exit(2);
	if(!mz_zip_writer_finalize_heap_archive(&zip, &out, size))
		exit(2);
	mz_zip_writer_end(&zip);
	return out;
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint8_t *make_gzip(const uint8_t *src, size_t n, size_t *size)
{
	static const uint8_t header[10] = {0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xFF};
	size_t len = 0;
	void *deflated = tdefl_compress_mem_to_heap(src, n, &len, TDEFL_DEFAULT_MAX_PROBES);
	uint8_t *out;
	if(!deflated)
		exit(2);
	out = malloc(sizeof header + len + 8);
	if(!out)
		exit(2);
	memcpy(out, header, sizeof header);
	memcpy(out + sizeof header, deflated, len);
	put_le32(out + sizeof header + len, (uint32_t)mz_crc32(MZ_CRC32_INIT, src, n));
	put_le32(out + sizeof header + len + 4, (uint32_t)n);
	mz_free(deflated);
	*size = sizeof header + len + 8;
	return out;
}

static void free_all(struct SdlCandidate *c, int n)
{
	int i;
	for(i = 0; i < n; i++)
		free(c[i].adf);
}

static void test_zip(void)
{
	static const char readme[] = "hello";
	uint8_t *d1 = image_filled(1, AMIGA_ADF_SIZE), *d2 = image_filled(2, AMIGA_ADF_SIZE);
	uint8_t *inner, *zip;
	size_t inner_size, zip_size;
	struct ZipEntry inner_entries[] = {{"deep.adf", d1, AMIGA_ADF_SIZE}};
	struct SdlCandidate c[SDL_IMAGE_CANDIDATES];
	char error[128];
	int n;
	inner = make_zip(inner_entries, 1, &inner_size);
	{
		struct ZipEntry entries[] = {
			{"Disk1.ADF", d1, AMIGA_ADF_SIZE},
			{"sub/disk2.adf", d2, AMIGA_ADF_SIZE},
			{"readme.txt", (const uint8_t *)readme, sizeof readme},
			{"inner.zip", inner, inner_size},
		};
		zip = make_zip(entries, 4, &zip_size);
	}
	n = sdl_image_expand("pair.zip", copy_bytes(zip, zip_size), zip_size, c, error, sizeof error);
	check(n == 2, "zip: two candidates", "wrong candidate count");
	if(n == 2)
	{
		check(!strcmp(c[0].name, "pair.zip:Disk1.ADF") && !strcmp(c[1].name, "pair.zip:sub/disk2.adf"), "zip: names", c[0].name);
		check(c[0].adf && c[1].adf && !memcmp(c[0].adf, d1, AMIGA_ADF_SIZE) && !memcmp(c[1].adf, d2, AMIGA_ADF_SIZE), "zip: bytes", "differ");
	}
	if(n > 0)
		free_all(c, n);
	n = sdl_image_expand("cut.zip", copy_bytes(zip, zip_size / 2), zip_size / 2, c, error, sizeof error);
	check(n == -1 && !strncmp(error, "not a readable ZIP", 18), "truncated zip", error);
	free(d1);
	free(d2);
	mz_free(inner);
	mz_free(zip);
}

static void test_gzip(void)
{
	const char *names[] = {"x.adz", "x.gz", "X.GZ"};
	uint8_t *src = image_filled(1, AMIGA_ADF_SIZE);
	size_t gz_size;
	uint8_t *gz = make_gzip(src, AMIGA_ADF_SIZE, &gz_size);
	int i;
	for(i = 0; i < 3; i++)
	{
		struct SdlCandidate c[SDL_IMAGE_CANDIDATES];
		char error[128];
		int n = sdl_image_expand(names[i], copy_bytes(gz, gz_size), gz_size, c, error, sizeof error);
		check(n == 1 && c[0].adf && !memcmp(c[0].adf, src, AMIGA_ADF_SIZE), names[i], n == 1 ? c[0].error : error);
		if(n > 0)
			free_all(c, n);
	}
	{
		struct SdlCandidate c[SDL_IMAGE_CANDIDATES];
		char error[128];
		int n;
		gz[gz_size - 8] ^= 1;
		n = sdl_image_expand("bad.gz", copy_bytes(gz, gz_size), gz_size, c, error, sizeof error);
		check(n == 1 && !c[0].adf && !strcmp(c[0].error, "not a readable gzip stream"), "gzip: wrong CRC", c[0].error);
		gz[gz_size - 8] ^= 1;
		gz[gz_size - 4] ^= 1;
		n = sdl_image_expand("size.gz", copy_bytes(gz, gz_size), gz_size, c, error, sizeof error);
		check(n == 1 && !c[0].adf && !strcmp(c[0].error, "not an 880 KB double-density ADF"), "gzip: wrong size", c[0].error);
	}
	free(src);
	free(gz);
}

static void test_sizes(void)
{
	struct SdlCandidate c[SDL_IMAGE_CANDIDATES];
	char error[128];
	int n = sdl_image_expand("big.adf", image_filled(0, 2 * AMIGA_ADF_SIZE), 2 * AMIGA_ADF_SIZE, c, error, sizeof error);
	check(n == 1 && !c[0].adf && !strcmp(c[0].error, "not an 880 KB double-density ADF"), "double size", c[0].error);
	n = sdl_image_expand("x.txt", image_filled(0, 1), 1, c, error, sizeof error);
	check(n == -1 && !strcmp(error, "not an ADF, ADZ, GZ or ZIP"), "other extension", error);
}

static void test_dumps(void)
{
	static const struct
	{
		uint32_t crc;
		const char *role;
		const char *set;
		enum SdlDumpStatus status;
		const char *name;
	} want[] = {
		{0xAB174FFBu, "disk1", "first", SdlDumpStatusVerified, "Synthetic disk 1"},
		{0x9FAB1EC7u, "disk2", 0, SdlDumpStatusKnown, "Synthetic disk 2"},
		{0x3AEFD3ECu, "disk1", 0, SdlDumpStatusRefused, "This synthetic dump is refused"},
	};
	uint32_t i;
	int ok = sdl_dump_count == 3;
	for(i = 0; ok && i < 3; i++)
		ok = sdl_dumps[i].crc == want[i].crc && !strcmp(sdl_dumps[i].role, want[i].role)
			&& (want[i].set ? sdl_dumps[i].set && !strcmp(sdl_dumps[i].set, want[i].set) : !sdl_dumps[i].set)
			&& sdl_dumps[i].status == want[i].status && !strcmp(sdl_dumps[i].name, want[i].name);
	check(ok, "dumps table", "rows differ from tests/dumps.txt");
}

static void identify(uint8_t fill, const char *name, int role, enum SdlRank rank, const char *error)
{
	uint8_t *adf = image_filled(fill, AMIGA_ADF_SIZE);
	struct SdlIdentity id;
	int ok;
	sdl_image_identify(adf, &id);
	ok = id.role == role && id.rank == rank && (role < 0 ? !strcmp(id.error, error) : 1);
	check(ok, name, id.error);
	if(role >= 0 && rank == SdlRankUnknown)
		check(id.name == 0, "unknown has no name", "name set");
	free(adf);
}

int main(void)
{
	test_zip();
	test_gzip();
	test_sizes();
	test_dumps();
	identify(1, "identify: verified", 0, SdlRankVerified, 0);
	identify(2, "identify: known", 1, SdlRankKnown, 0);
	identify(3, "identify: refused", -1, SdlRankNone, "This synthetic dump is refused");
	identify(4, "identify: unknown", 0, SdlRankUnknown, 0);
	identify(5, "identify: no role", -1, SdlRankNone, "not a disk of Test Game");
	return failures ? 1 : 0;
}
