#include "image.h"

#include "amiga.h"
#include "game.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_NOT_ADF "not an 880 KB double-density ADF"
#define IMAGE_NOT_GZIP "not a readable gzip stream"

static int image_ends(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext), i;
	if(n < e)
		return 0;
	for(i = 0; i < e; i++)
	{
		char c = name[n - e + i];
		if(c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if(c != ext[i])
			return 0;
	}
	return 1;
}

static int image_is_gzip_name(const char *name)
{
	return image_ends(name, ".adz") || image_ends(name, ".gz");
}

static uint32_t image_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* RFC 1952. Returns a malloc'd AMIGA_ADF_SIZE buffer, or NULL with `error` set. */
static uint8_t *image_gunzip(const uint8_t *src, size_t n, char *error, size_t error_size)
{
	size_t p = 10, got;
	uint8_t flags;
	uint8_t *buf;
	if(n < 18 || src[0] != 0x1F || src[1] != 0x8B || src[2] != 0x08)
	{
		snprintf(error, error_size, "%s", IMAGE_NOT_GZIP);
		return 0;
	}
	flags = src[3];
	if(flags & 0x04)
	{
		if(p + 2 > n - 8)
			goto bad;
		p += 2 + ((size_t)src[p] | (size_t)src[p + 1] << 8);
		if(p > n - 8)
			goto bad;
	}
	if(flags & 0x08)
	{
		while(p < n - 8 && src[p])
			p++;
		if(p >= n - 8)
			goto bad;
		p++;
	}
	if(flags & 0x10)
	{
		while(p < n - 8 && src[p])
			p++;
		if(p >= n - 8)
			goto bad;
		p++;
	}
	if(flags & 0x02)
		p += 2;
	if(p >= n - 8)
		goto bad;
	if(image_le32(src + n - 4) != AMIGA_ADF_SIZE)
	{
		snprintf(error, error_size, "%s", IMAGE_NOT_ADF);
		return 0;
	}
	buf = malloc(AMIGA_ADF_SIZE);
	if(!buf)
	{
		snprintf(error, error_size, "%s", "out of memory");
		return 0;
	}
	got = tinfl_decompress_mem_to_mem(buf, AMIGA_ADF_SIZE, src + p, n - 8 - p, 0);
	if(got != AMIGA_ADF_SIZE || (uint32_t)mz_crc32(MZ_CRC32_INIT, buf, AMIGA_ADF_SIZE) != image_le32(src + n - 8))
	{
		free(buf);
		goto bad;
	}
	return buf;
bad:
	snprintf(error, error_size, "%s", IMAGE_NOT_GZIP);
	return 0;
}

static void image_set_name(struct SdlCandidate *c, const char *file, const char *member)
{
	if(member)
		snprintf(c->name, sizeof c->name, "%s:%.200s", file, member);
	else
		snprintf(c->name, sizeof c->name, "%s", file);
}

static void image_free_all(struct SdlCandidate *out, int n)
{
	int i;
	for(i = 0; i < n; i++)
		free(out[i].adf);
	memset(out, 0, sizeof out[0] * SDL_IMAGE_CANDIDATES);
}

static int image_expand_zip(const char *name, uint8_t *data, size_t size,
	struct SdlCandidate out[SDL_IMAGE_CANDIDATES], char *error, size_t error_size)
{
	mz_zip_archive zip;
	mz_uint i, count;
	int n = 0;
	memset(&zip, 0, sizeof zip);
	if(!mz_zip_reader_init_mem(&zip, data, size, 0))
	{
		snprintf(error, error_size, "not a readable ZIP (%s)", mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
		free(data);
		return -1;
	}
	count = mz_zip_reader_get_num_files(&zip);
	for(i = 0; i < count; i++)
	{
		mz_zip_archive_file_stat st;
		struct SdlCandidate *c;
		int gz;
		if(!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
			continue;
		gz = image_is_gzip_name(st.m_filename);
		if(!gz && !image_ends(st.m_filename, ".adf"))
			continue;
		if(n == SDL_IMAGE_CANDIDATES)
		{
			image_free_all(out, n);
			snprintf(error, error_size, "more than %d images", SDL_IMAGE_CANDIDATES);
			mz_zip_reader_end(&zip);
			free(data);
			return -1;
		}
		c = &out[n++];
		image_set_name(c, name, st.m_filename);
		if(!gz)
		{
			if(st.m_uncomp_size != AMIGA_ADF_SIZE)
			{
				snprintf(c->error, sizeof c->error, "%s", IMAGE_NOT_ADF);
				continue;
			}
			c->adf = malloc(AMIGA_ADF_SIZE);
			if(!c->adf)
				snprintf(c->error, sizeof c->error, "out of memory");
			else if(!mz_zip_reader_extract_to_mem(&zip, i, c->adf, AMIGA_ADF_SIZE, 0))
			{
				snprintf(c->error, sizeof c->error, "not a readable ZIP member (%s)",
					mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
				free(c->adf);
				c->adf = 0;
			}
		}
		else
		{
			size_t len = 0;
			void *raw = mz_zip_reader_extract_to_heap(&zip, i, &len, 0);
			if(!raw)
				snprintf(c->error, sizeof c->error, "not a readable ZIP member (%s)",
					mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
			else
			{
				c->adf = image_gunzip(raw, len, c->error, sizeof c->error);
				mz_free(raw);
			}
		}
	}
	mz_zip_reader_end(&zip);
	free(data);
	if(!n)
	{
		snprintf(error, error_size, "holds no .adf, .adz or .gz");
		return -1;
	}
	return n;
}

int sdl_image_expand(const char *name, uint8_t *data, size_t size,
	struct SdlCandidate out[SDL_IMAGE_CANDIDATES], char *error, size_t error_size)
{
	memset(out, 0, sizeof out[0] * SDL_IMAGE_CANDIDATES);
	error[0] = 0;
	if(image_ends(name, ".adf"))
	{
		image_set_name(&out[0], name, 0);
		if(size == AMIGA_ADF_SIZE)
			out[0].adf = data;
		else
		{
			snprintf(out[0].error, sizeof out[0].error, "%s", IMAGE_NOT_ADF);
			free(data);
		}
		return 1;
	}
	if(image_is_gzip_name(name))
	{
		image_set_name(&out[0], name, 0);
		out[0].adf = image_gunzip(data, size, out[0].error, sizeof out[0].error);
		free(data);
		return 1;
	}
	if(image_ends(name, ".zip"))
		return image_expand_zip(name, data, size, out, error, error_size);
	snprintf(error, error_size, "not an ADF, ADZ, GZ or ZIP");
	free(data);
	return -1;
}

void sdl_image_identify(const uint8_t *adf, struct SdlIdentity *id)
{
	const struct SdlDump *dump = 0;
	uint32_t crc = amiga_crc32(adf, AMIGA_ADF_SIZE), i;
	int r;
	id->role = -1;
	id->rank = SdlRankNone;
	id->name = 0;
	id->error[0] = 0;
	for(i = 0; i < sdl_dump_count; i++)
		if(sdl_dumps[i].crc == crc)
		{
			dump = &sdl_dumps[i];
			break;
		}
	if(dump && dump->status == SdlDumpStatusRefused)
	{
		snprintf(id->error, sizeof id->error, "%s", dump->name);
		return;
	}
	if(dump)
	{
		for(i = 0; i < game_def.disk_count; i++)
			if(!strcmp(game_def.disks[i].id, dump->role) && !game_def.disks[i].writable)
				break;
		if(i == game_def.disk_count)
		{
			snprintf(id->error, sizeof id->error, "dumps.txt names %s, which is not a read-only role of %s",
				dump->role, game_def.title);
			return;
		}
		id->role = (int)i;
		id->rank = dump->status == SdlDumpStatusVerified ? SdlRankVerified : SdlRankKnown;
		id->name = dump->name;
		return;
	}
	for(r = 0; r < (int)game_def.disk_count; r++)
	{
		const struct GameDisk *d = &game_def.disks[r];
		if(!d->writable && d->identify && d->identify(adf, AMIGA_ADF_SIZE))
		{
			id->role = r;
			id->rank = SdlRankUnknown;
			return;
		}
	}
	snprintf(id->error, sizeof id->error, "not a disk of %s", game_def.title);
}
