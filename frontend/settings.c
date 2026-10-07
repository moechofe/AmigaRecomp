#include "settings.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_MAX 64

struct SettingsEntry
{
	char key[64];
	char value[1024];
};

static struct SettingsEntry settings_table[SETTINGS_MAX];
static int settings_count;
static char settings_path[1280];

static struct SettingsEntry *settings_find(const char *key)
{
	int i;
	for(i = 0; i < settings_count; i++)
		if(!strcmp(settings_table[i].key, key))
			return &settings_table[i];
	return 0;
}

const char *sdl_settings_get(const char *key)
{
	const struct SettingsEntry *e = settings_find(key);
	return e ? e->value : 0;
}

int sdl_settings_set(const char *key, const char *value)
{
	struct SettingsEntry *e = settings_find(key);
	if(!e)
	{
		if(settings_count == SETTINGS_MAX)
		{
			fprintf(stderr, "settings.ini: more than %d keys\n", SETTINGS_MAX);
			return 0;
		}
		e = &settings_table[settings_count++];
		snprintf(e->key, sizeof e->key, "%s", key);
	}
	else if(!strcmp(e->value, value))
		return 0;
	snprintf(e->value, sizeof e->value, "%s", value);
	return 1;
}

int sdl_settings_load(const char *path)
{
	SDL_PathInfo info;
	char *text, *line, *end;
	size_t size;
	settings_count = 0;
	snprintf(settings_path, sizeof settings_path, "%s", path);
	if(!SDL_GetPathInfo(path, &info))
		return 0;
	text = SDL_LoadFile(path, &size);
	if(!text)
	{
		fprintf(stderr, "cannot read %s: %s\n", path, SDL_GetError());
		return -1;
	}
	for(line = text; line < text + size; line = end + 1)
	{
		char *eq, *stop = line;
		while(stop < text + size && *stop != '\n')
			stop++;
		end = stop;
		if(stop > line && stop[-1] == '\r')
			stop--;
		if(line == stop || *line == '#')
			continue;
		for(eq = line; eq < stop && *eq != '='; eq++)
			;
		if(eq == stop || eq == line)
			continue;
		{
			char key[64], value[1024];
			snprintf(key, sizeof key, "%.*s", (int)(eq - line), line);
			snprintf(value, sizeof value, "%.*s", (int)(stop - eq - 1), eq + 1);
			sdl_settings_set(key, value);
		}
	}
	SDL_free(text);
	return 0;
}

int sdl_settings_save(void)
{
	char text[SETTINGS_MAX * (64 + 1024 + 2)];
	size_t n = 0;
	int i;
	for(i = 0; i < settings_count; i++)
		n += (size_t)snprintf(text + n, sizeof text - n, "%s=%s\n", settings_table[i].key, settings_table[i].value);
	if(!SDL_SaveFile(settings_path, text, n))
	{
		fprintf(stderr, "cannot write %s\n", settings_path);
		return -1;
	}
	return 0;
}
