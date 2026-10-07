#ifndef DISKS_H
#define DISKS_H

#include "amiga.h"
#include "image.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SdlRole
{
	uint8_t *adf;       /* AMIGA_ADF_SIZE bytes, or NULL */
	enum SdlRank rank;  /* SdlRankNone for writable roles */
	char name[256];     /* dumps.txt name, or the candidate name of an unknown dump */
	char source[1024];  /* absolute path given (the container for a ZIP member); <pref><id>.adf for writable */
	int attached;       /* handed to the machine with amiga_attach */
};
extern struct SdlRole sdl_roles[AMIGA_ROLES];

enum SdlOffer { SdlOfferGiven, SdlOfferRemembered };

int sdl_disks_init(const char *pref);  /* 0, or -1 with the message printed */
/* Reads, expands and identifies `path`. `report` gets one line per candidate: error 1 when it is
 * rejected, 0 when accepted or outranked. Returns the number of roles filled. */
int sdl_disks_offer(const char *path, enum SdlOffer how, void (*report)(int error, const char *text));
void sdl_disks_offer_remembered(void (*report)(int error, const char *text));
int sdl_disks_ready(void);  /* every role that drives[] names has an image */
void sdl_disks_missing(char *buf, size_t size);
void sdl_disks_attach(struct Amiga *m, void (*banner)(const char *text));
void sdl_disks_remember(void);

#ifdef __cplusplus
}
#endif

#endif
