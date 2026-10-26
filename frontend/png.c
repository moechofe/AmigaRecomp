#include "png.h"

#include <stdio.h>
#include <string.h>

/* RGB8 PNG with stored (uncompressed) deflate blocks: PNG spec (ISO 15948) and RFC 1950/1951. */

static uint32_t sdl_crc_table[256];

static void sdl_crc_init(void)
{
	uint32_t n, k;
	for(n = 0; n < 256; n++)
	{
		uint32_t c = n;
		for(k = 0; k < 8; k++)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		sdl_crc_table[n] = c;
	}
}

static uint32_t sdl_crc(uint32_t crc, const uint8_t *p, size_t n)
{
	size_t i;
	crc = ~crc;
	for(i = 0; i < n; i++)
		crc = sdl_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

static void sdl_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void sdl_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
	uint8_t head[8], tail[4];
	uint32_t crc;
	sdl_be32(head, len);
	memcpy(head + 4, type, 4);
	crc = sdl_crc(0, head + 4, 4);
	crc = sdl_crc(crc, data, len);
	sdl_be32(tail, crc);
	fwrite(head, 1, 8, f);
	if(len)
		fwrite(data, 1, len, f);
	fwrite(tail, 1, 4, f);
}

#define SDL_PNG_RAW_MAX (AMIGA_FRAME_ROWS * (1 + AMIGA_FRAME_STRIDE * 3))
#define SDL_PNG_BLOCKS ((SDL_PNG_RAW_MAX + 65534) / 65535)

static uint8_t sdl_png_raw[SDL_PNG_RAW_MAX];
static uint8_t sdl_png_z[2 + SDL_PNG_RAW_MAX + SDL_PNG_BLOCKS * 5 + 4];

int sdl_png_write(const char *path, const struct Amiga *m, const struct SdlView *v)
{
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	const uint32_t raw = (uint32_t)v->h * (1 + (uint32_t)v->w * 3);
	uint8_t ihdr[13];
	uint32_t a = 1, b = 0, i, zn = 0, done = 0;
	int x, y;
	FILE *f;
	sdl_crc_init();
	for(y = 0, i = 0; y < v->h; y++)
	{
		sdl_png_raw[i++] = 0;
		for(x = 0; x < v->w; x++)
		{
			uint32_t c = denise_capture(m, v->x + x, v->y - AMIGA_FRAME_CAPTURE_ROW + y);
			sdl_png_raw[i++] = (uint8_t)(c >> 16);
			sdl_png_raw[i++] = (uint8_t)(c >> 8);
			sdl_png_raw[i++] = (uint8_t)c;
		}
	}
	for(i = 0; i < raw; i++)
	{
		a = (a + sdl_png_raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	sdl_png_z[zn++] = 0x78;
	sdl_png_z[zn++] = 0x01;
	while(done < raw)
	{
		uint32_t n = raw - done > 65535 ? 65535 : raw - done;
		sdl_png_z[zn++] = done + n == raw ? 1 : 0;
		sdl_png_z[zn++] = (uint8_t)n;
		sdl_png_z[zn++] = (uint8_t)(n >> 8);
		sdl_png_z[zn++] = (uint8_t)~n;
		sdl_png_z[zn++] = (uint8_t)(~n >> 8);
		memcpy(sdl_png_z + zn, sdl_png_raw + done, n);
		zn += n;
		done += n;
	}
	sdl_be32(sdl_png_z + zn, (b << 16) | a);
	zn += 4;

	sdl_be32(ihdr, (uint32_t)v->w);
	sdl_be32(ihdr + 4, (uint32_t)v->h);
	ihdr[8] = 8;
	ihdr[9] = 2;
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;

	f = fopen(path, "wb");
	if(!f)
		return -1;
	fwrite(sig, 1, 8, f);
	sdl_chunk(f, "IHDR", ihdr, 13);
	sdl_chunk(f, "IDAT", sdl_png_z, zn);
	sdl_chunk(f, "IEND", 0, 0);
	return fclose(f) ? -1 : 0;
}
