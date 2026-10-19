#include "disks.h"

#include "game.h"
#include "settings.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SdlRole sdl_roles[AMIGA_ROLES];

static const char *disks_rank_word(enum SdlRank rank)
{
	return rank == SdlRankVerified ? "verified" : rank == SdlRankKnown ? "known" : "unknown";
}

/* Read an ADF into `buf`. 0, or -1 with the message printed. */
static int disks_read_adf(const char *path, uint8_t *buf)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if(!f)
	{
		fprintf(stderr, "cannot read %s\n", path);
		return -1;
	}
	n = fread(buf, 1, AMIGA_ADF_SIZE, f);
	if(n != AMIGA_ADF_SIZE || fgetc(f) != EOF)
	{
		fprintf(stderr, "%s: not an 880 KB double-density ADF\n", path);
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

int sdl_disks_init(const char *pref)
{
	uint32_t r;
	int n;
	if(game_def.drive_count < 1 || game_def.drive_count > AMIGA_DRIVES)
	{
		fprintf(stderr, "game_def.drive_count %d is not 1-%d\n", game_def.drive_count, AMIGA_DRIVES);
		return -1;
	}
	for(n = game_def.drive_count; n < AMIGA_DRIVES; n++)
		if(game_def.drives[n] >= 0 || (game_def.drives_optional && game_def.drives_optional[n] >= 0))
		{
			fprintf(stderr, "game_def.drives%s[%d] is beyond drive_count %d\n", game_def.drives[n] >= 0 ? "" : "_optional", n, game_def.drive_count);
			return -1;
		}
	for(r = 0; r < game_def.disk_count; r++)
	{
		struct SdlRole *role = &sdl_roles[r];
		FILE *f;
		if(!game_def.disks[r].writable)
			continue;
		role->adf = malloc(AMIGA_ADF_SIZE);
		if(!role->adf)
		{
			fprintf(stderr, "out of memory\n");
			return -1;
		}
		snprintf(role->source, sizeof role->source, "%s%s.adf", pref, game_def.disks[r].id);
		f = fopen(role->source, "rb");
		if(f)
		{
			fclose(f);
			if(disks_read_adf(role->source, role->adf))
				return -1;
		}
		else
			memset(role->adf, 0, AMIGA_ADF_SIZE);
	}
	return 0;
}

static int disks_is_absolute(const char *path)
{
	if(path[0] == '/' || path[0] == '\\')
		return 1;
	return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
}

static uint8_t *disks_read_file(const char *path, size_t *size)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "rb");
	Sint64 n;
	uint8_t *data;
	if(!io)
		return 0;
	n = SDL_GetIOSize(io);
	if(n < 0 || (uint64_t)n > (uint64_t)SIZE_MAX / 2)
	{
		SDL_CloseIO(io);
		return 0;
	}
	data = malloc(n ? (size_t)n : 1);
	if(!data || SDL_ReadIO(io, data, (size_t)n) != (size_t)n)
	{
		free(data);
		SDL_CloseIO(io);
		return 0;
	}
	SDL_CloseIO(io);
	*size = (size_t)n;
	return data;
}

/* A role other than `role` holding a dump of another set than `set`, or -1. */
static int disks_other_set(int role, const char *set)
{
	uint32_t r;
	for(r = 0; set && r < game_def.disk_count; r++)
		if((int)r != role && sdl_roles[r].set && strcmp(sdl_roles[r].set, set))
			return (int)r;
	return -1;
}

/* Empties role `r` and forgets its settings.ini path, first taking it from `m` and its drives
 * when the machine holds it. */
static void disks_release(struct Amiga *m, int r)
{
	struct SdlRole *role = &sdl_roles[r];
	const char *value;
	char key[80];
	int n;
	snprintf(key, sizeof key, "disk.%s", game_def.disks[r].id);
	value = sdl_settings_get(key);
	if(value && *value && sdl_settings_set(key, ""))
		sdl_settings_save();
	if(role->attached)
	{
		for(n = 0; n < AMIGA_DRIVES; n++)
			if(m->drive_role[n] == r)
				amiga_insert(m, n, -1);
		amiga_attach(m, r, NULL, 0);
		role->attached = 0;
	}
	free(role->adf);
	role->adf = NULL;
	role->rank = SdlRankNone;
	role->set = NULL;
	role->name[0] = 0;
	role->source[0] = 0;
}

/* `open` NULL: sdl_disks_offer's rules. Else sdl_disks_open's, on that machine. */
static int disks_offer(const char *path, enum SdlOffer how, struct Amiga *open, int *swapped,
	void (*report)(int error, const char *text))
{
	char absolute[1024], line[640], error[128];
	struct SdlCandidate cands[SDL_IMAGE_CANDIDATES];
	const char *base, *slash;
	size_t size = 0;
	uint8_t *data;
	int n, i, filled = 0;

	if(disks_is_absolute(path))
		snprintf(absolute, sizeof absolute, "%s", path);
	else
	{
		char *cwd = SDL_GetCurrentDirectory();
		size_t len = cwd ? strlen(cwd) : 0;
		snprintf(absolute, sizeof absolute, "%s%s%s", cwd ? cwd : "",
			len && cwd[len - 1] != '/' && cwd[len - 1] != '\\' ? "/" : "", path);
		SDL_free(cwd);
	}
	if(strlen(absolute) >= sizeof absolute - 1 || strlen(path) >= sizeof absolute - 1)
	{
		snprintf(line, sizeof line, "%.200s: path too long", path);
		report(1, line);
		return 0;
	}
	base = absolute;
	for(slash = absolute; *slash; slash++)
		if(*slash == '/' || *slash == '\\')
			base = slash + 1;
	data = disks_read_file(absolute, &size);
	if(!data)
	{
		snprintf(line, sizeof line, "cannot read %s", absolute);
		report(1, line);
		return 0;
	}
	n = sdl_image_expand(base, data, size, cands, error, sizeof error);
	if(n < 0)
	{
		snprintf(line, sizeof line, "%s: %s", base, error);
		report(1, line);
		return 0;
	}
	for(i = 0; i < n; i++)
	{
		struct SdlCandidate *c = &cands[i];
		struct SdlIdentity id;
		struct SdlRole *held;
		const char *label;
		int other;
		if(!c->adf)
		{
			snprintf(line, sizeof line, "%.255s: %s", c->name, c->error);
			report(1, line);
			continue;
		}
		sdl_image_identify(c->adf, &id);
		if(id.role < 0)
		{
			snprintf(line, sizeof line, "%.255s: %s", c->name, id.error);
			report(1, line);
			free(c->adf);
			continue;
		}
		held = &sdl_roles[id.role];
		label = game_def.disks[id.role].label;
		if(open)
		{
			while((other = disks_other_set(id.role, id.set)) >= 0)
			{
				snprintf(line, sizeof line, "%.200s: %s taken out, it does not go with %.200s", sdl_roles[other].name,
					game_def.disks[other].label, c->name);
				report(0, line);
				*swapped |= sdl_roles[other].attached;
				disks_release(open, other);
			}
			if(held->attached)
			{
				amiga_attach(open, id.role, c->adf, 0);
				*swapped = 1;
			}
		}
		else if(held->adf && how == SdlOfferRemembered)
		{
			free(c->adf);
			continue;
		}
		else if((other = disks_other_set(id.role, id.set)) >= 0)
		{
			snprintf(line, sizeof line, "%.200s: %s, it does not go with %s, %.200s", c->name,
				how == SdlOfferRemembered ? "not used" : "refused", game_def.disks[other].label, sdl_roles[other].name);
			report(how != SdlOfferRemembered, line);
			free(c->adf);
			continue;
		}
		else if(held->adf && id.rank <= held->rank)
		{
			snprintf(line, sizeof line, "%.255s: ignored, %s already has a %s dump", c->name, label, disks_rank_word(held->rank));
			report(0, line);
			free(c->adf);
			continue;
		}
		free(held->adf);
		held->adf = c->adf;
		held->rank = id.rank;
		held->set = id.set;
		snprintf(held->source, sizeof held->source, "%s", absolute);
		snprintf(held->name, sizeof held->name, "%.255s", id.name ? id.name : c->name);
		snprintf(line, sizeof line, "%.255s: %s, %s dump", c->name, label, disks_rank_word(id.rank));
		report(0, line);
		filled++;
	}
	return filled;
}

int sdl_disks_offer(const char *path, enum SdlOffer how, void (*report)(int error, const char *text))
{
	return disks_offer(path, how, NULL, NULL, report);
}

int sdl_disks_open(struct Amiga *m, const char *path, int *swapped, void (*report)(int error, const char *text))
{
	return disks_offer(path, SdlOfferGiven, m, swapped, report);
}

int sdl_disks_import(int role, const char *path, void (*report)(int error, const char *text))
{
	char line[1400];
	struct SdlIdentity id;
	const char *base = path, *p;
	size_t size = 0;
	uint8_t *data;
	FILE *f;
	int ok;

	if(role < 0 || role >= (int)game_def.disk_count || !game_def.disks[role].writable)
		return -1;
	for(p = path; *p; p++)
		if(*p == '/' || *p == '\\')
			base = p + 1;
	data = disks_read_file(path, &size);
	if(!data)
	{
		snprintf(line, sizeof line, "cannot read %.1000s", path);
		report(1, line);
		return -1;
	}
	if(size != AMIGA_ADF_SIZE)
	{
		snprintf(line, sizeof line, "%.255s: not an 880 KB double-density ADF", base);
		report(1, line);
		free(data);
		return -1;
	}
	sdl_image_identify(data, &id);
	if(id.role >= 0)
	{
		snprintf(line, sizeof line, "%.255s: refused, it is %s", base, game_def.disks[id.role].label);
		report(1, line);
		free(data);
		return -1;
	}
	f = fopen(sdl_roles[role].source, "wb");
	ok = f && fwrite(data, 1, AMIGA_ADF_SIZE, f) == AMIGA_ADF_SIZE;
	if(f && fclose(f))
		ok = 0;
	if(!ok)
	{
		snprintf(line, sizeof line, "cannot write %.1000s", sdl_roles[role].source);
		report(1, line);
		free(data);
		return -1;
	}
	memcpy(sdl_roles[role].adf, data, AMIGA_ADF_SIZE);
	free(data);
	snprintf(line, sizeof line, "%.255s: copied into %s (%.800s)", base, game_def.disks[role].label, sdl_roles[role].source);
	report(0, line);
	return 0;
}

void sdl_disks_offer_remembered(void (*report)(int error, const char *text))
{
	const char *offered[AMIGA_ROLES];
	int count = 0;
	uint32_t r;
	for(r = 0; r < game_def.disk_count; r++)
	{
		char key[80];
		const char *value;
		int i, seen = 0;
		if(game_def.disks[r].writable || sdl_roles[r].adf)
			continue;
		snprintf(key, sizeof key, "disk.%s", game_def.disks[r].id);
		value = sdl_settings_get(key);
		if(!value || !*value)
			continue;
		for(i = 0; i < count; i++)
			seen = seen || !strcmp(offered[i], value);
		if(seen)
			continue;
		offered[count++] = value;
		sdl_disks_offer(value, SdlOfferRemembered, report);
	}
}

int sdl_disks_ready(void)
{
	int n;
	for(n = 0; n < 4; n++)
		if(game_def.drives[n] >= 0 && !sdl_roles[game_def.drives[n]].adf)
			return 0;
	return 1;
}

const int *sdl_disks_boot_drives(void)
{
	int n;
	if(!game_def.drives_optional)
		return game_def.drives;
	for(n = 0; n < 4; n++)
		if(game_def.drives_optional[n] >= 0 && !sdl_roles[game_def.drives_optional[n]].adf)
			return game_def.drives;
	return game_def.drives_optional;
}

void sdl_disks_missing(char *buf, size_t size)
{
	uint32_t r;
	int n;
	size_t used = 0;
	if(size)
		buf[0] = 0;
	for(r = 0; r < game_def.disk_count; r++)
	{
		int named = 0;
		for(n = 0; n < 4; n++)
			named = named || game_def.drives[n] == (int)r;
		if(!named || sdl_roles[r].adf || used >= size)
			continue;
		used += (size_t)snprintf(buf + used, size - used, "%s%s", used ? ", " : "", game_def.disks[r].label);
	}
}

void sdl_disks_attach(struct Amiga *m, void (*banner)(const char *text))
{
	uint32_t r;
	for(r = 0; r < game_def.disk_count; r++)
	{
		struct SdlRole *role = &sdl_roles[r];
		if(!role->adf || role->attached)
			continue;
		amiga_attach(m, (int)r, role->adf, game_def.disks[r].writable);
		role->attached = 1;
		if(!game_def.disks[r].writable && role->rank == SdlRankUnknown)
		{
			char text[160];
			snprintf(text, sizeof text, "Unknown dump of %s: running anyway; some hacks may stay inactive", game_def.disks[r].label);
			banner(text);
		}
	}
}

static void disks_report_drive(const struct Amiga *m, int drive, void (*report)(const char *text))
{
	char line[300];
	int role = m->drive_role[drive];
	snprintf(line, sizeof line, "DF%d: %.255s", drive, role < 0 ? "empty" : game_def.disks[role].label);
	report(line);
}

void sdl_disks_insert(struct Amiga *m, int drive, int role, void (*report)(const char *text))
{
	int old, n, from = -1;
	if(!m->booted || drive < 0 || drive >= game_def.drive_count || role < -1
		|| role >= (int)game_def.disk_count || (role >= 0 && !m->disks[role].image))
		return;
	old = m->drive_role[drive];
	if(role == old)
		return;
	for(n = 0; n < game_def.drive_count && role >= 0 && from < 0; n++)
		if(n != drive && m->drive_role[n] == role)
			from = n;
	if(from >= 0)
	{
		amiga_insert(m, from, old);
		disks_report_drive(m, from, report);
	}
	amiga_insert(m, drive, role);
	disks_report_drive(m, drive, report);
}

void sdl_disks_remember(void)
{
	uint32_t r;
	int changed = 0;
	for(r = 0; r < game_def.disk_count; r++)
	{
		char key[80];
		if(game_def.disks[r].writable || !sdl_roles[r].adf)
			continue;
		snprintf(key, sizeof key, "disk.%s", game_def.disks[r].id);
		changed |= sdl_settings_set(key, sdl_roles[r].source);
	}
	if(changed)
		sdl_settings_save();
}
