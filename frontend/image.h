#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

enum SdlDumpStatus { SdlDumpStatusVerified, SdlDumpStatusKnown, SdlDumpStatusRefused };
struct SdlDump { uint32_t crc; const char *role; enum SdlDumpStatus status; const char *name; };
extern const struct SdlDump sdl_dumps[];  /* game_dumps.c, generated from dumps.txt */
extern const uint32_t sdl_dump_count;

#define SDL_IMAGE_CANDIDATES 16
struct SdlCandidate
{
	char name[256];  /* "<file>" or "<file>:<member>" */
	uint8_t *adf;    /* AMIGA_ADF_SIZE bytes from malloc, or NULL when rejected */
	char error[128]; /* why, when adf is NULL */
};

/* Expands the file `name` held in the malloc buffer `data`, which this call owns: a candidate
 * adopts it or it is freed. Returns the number of candidates, or -1 with `error` set. */
int sdl_image_expand(const char *name, uint8_t *data, size_t size,
	struct SdlCandidate out[SDL_IMAGE_CANDIDATES], char *error, size_t error_size);

enum SdlRank { SdlRankNone, SdlRankUnknown, SdlRankKnown, SdlRankVerified };
struct SdlIdentity
{
	int role;          /* index into game_def.disks, -1 when rejected */
	enum SdlRank rank;
	const char *name;  /* dumps.txt name for known/verified, NULL for unknown */
	char error[192];   /* when role is -1 */
};
void sdl_image_identify(const uint8_t *adf, struct SdlIdentity *id);

#endif
