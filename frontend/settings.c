#include "settings.h"

#include "amiga.h"

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

const struct GameSetting sdl_settings[SdlSettingCount] = {
	{"mouse_capture", "Capture the mouse", "Off: the system cursor moves the game's pointer and can leave the window.", GameValueBool, 0, 0},
};

static int sdl_setting_values[SdlSettingCount];
static int sdl_setting_ready;

static void setting_defaults(void)
{
	int i;
	if(sdl_setting_ready)
		return;
	for(i = 0; i < SdlSettingCount; i++)
		sdl_setting_values[i] = sdl_settings[i].def;
	sdl_setting_ready = 1;
}

int sdl_setting(enum SdlSetting s)
{
	setting_defaults();
	return sdl_setting_values[s];
}

/* The text a value has in settings.ini, or NULL when the value is out of range. */
static const char *setting_text(const struct GameSetting *s, int v)
{
	static const char *const bools[] = {"0", "1"};
	int i;
	if(v < 0)
		return 0;
	if(s->type == GameValueBool)
		return v < 2 ? bools[v] : 0;
	for(i = 0; i < v && s->choices && s->choices[i]; i++)
		;
	return s->choices && i == v ? s->choices[v] : 0;
}

static void setting_key(char *key, size_t size, int hack, const struct GameSetting *s)
{
	snprintf(key, size, hack ? "hack.%s" : "%s", s->id);
}

static int setting_read(const struct GameSetting *s, const char *key)
{
	const char *text = sdl_settings_get(key);
	int v;
	if(!text)
		return s->def;
	for(v = 0; setting_text(s, v); v++)
		if(!strcmp(text, setting_text(s, v)))
			return v;
	fprintf(stderr, "settings.ini: ignoring %s=%s\n", key, text);
	return s->def;
}

void sdl_settings_apply(struct Amiga *m, int hacks)
{
	char key[80];
	uint32_t i;
	setting_defaults();
	for(i = 0; i < SdlSettingCount; i++)
	{
		setting_key(key, sizeof key, 0, &sdl_settings[i]);
		sdl_setting_values[i] = setting_read(&sdl_settings[i], key);
	}
	if(!hacks)
		return;
	for(i = 0; i < game_def.hack_count; i++)
	{
		const struct GameSetting *s = &game_def.hacks[i]->setting;
		setting_key(key, sizeof key, 1, s);
		game_hack_set(m, i, setting_read(s, key));
	}
}

void sdl_settings_choose(struct Amiga *m, int hack, uint32_t index, int value)
{
	const struct GameSetting *s;
	const char *text;
	char key[80];
	setting_defaults();
	if(hack)
	{
		if(index >= game_def.hack_count)
			return;
		s = &game_def.hacks[index]->setting;
	}
	else
	{
		if(index >= SdlSettingCount)
			return;
		s = &sdl_settings[index];
	}
	text = setting_text(s, value);
	if(!text)
		return;
	if(hack)
	{
		if(game_hack_set(m, index, value))
			return;
	}
	else
	{
		sdl_setting_values[index] = value;
	}
	setting_key(key, sizeof key, hack, s);
	if(sdl_settings_set(key, text))
		sdl_settings_save();
}

int sdl_hack_value(const struct Amiga *m, uint32_t index)
{
	return m->hacks.value[index];
}
