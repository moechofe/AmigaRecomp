#define _POSIX_C_SOURCE 200809L

#include "desktop.h"
#include "game.h"
#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32

void sdl_desktop_window_icon(SDL_Window *window) { (void)window; }
int sdl_desktop_offered(void) { return 0; }
int sdl_desktop_installed(void) { return 0; }
int sdl_desktop_install(char *msg, size_t n) { snprintf(msg, n, "Not available on Windows."); return -1; }
int sdl_desktop_remove(char *msg, size_t n) { snprintf(msg, n, "Not available on Windows."); return -1; }

#else

extern const struct SdlAsset sdl_packaging[];

static const struct SdlAsset *desktop_asset(const char *ext)
{
	char name[64];
	const struct SdlAsset *a;
	snprintf(name, sizeof name, "%s.%s", game_def.id, ext);
	for(a = sdl_packaging; a->name; a++)
		if(!strcmp(a->name, name))
			return a;
	return 0;
}

void sdl_desktop_window_icon(SDL_Window *window)
{
	const struct SdlAsset *a = desktop_asset("png");
	SDL_Surface *icon;
	if(!a)
		return;
	icon = SDL_LoadPNG_IO(SDL_IOFromConstMem(a->data, a->size), true);
	if(!icon)
	{
		fprintf(stderr, "window icon: %s\n", SDL_GetError());
		return;
	}
	/* GNOME on Wayland has no window icons and takes the icon from the matching .desktop file;
	 * SDL reports that as a failure, which is not one here. */
	SDL_SetWindowIcon(window, icon);
	SDL_DestroySurface(icon);
}

#ifdef __linux__

/* XDG Base Directory: $XDG_DATA_HOME, or ~/.local/share. */
static int desktop_paths(char *entry, char *icon, size_t n)
{
	const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	const struct SdlAsset *png = desktop_asset("png");
	char base[1024];
	unsigned w, h;
	if(data && data[0] == '/')
		snprintf(base, sizeof base, "%s", data);
	else if(home && *home)
		snprintf(base, sizeof base, "%s/.local/share", home);
	else
		return -1;
	/* The hicolor size directory is the PNG's own: IHDR width and height, big-endian. */
	if(!png || png->size < 24)
		return -1;
	w = (unsigned)png->data[16] << 24 | (unsigned)png->data[17] << 16 | (unsigned)png->data[18] << 8 | png->data[19];
	h = (unsigned)png->data[20] << 24 | (unsigned)png->data[21] << 16 | (unsigned)png->data[22] << 8 | png->data[23];
	if(snprintf(entry, n, "%s/applications/%s.desktop", base, game_def.id) >= (int)n)
		return -1;
	if(snprintf(icon, n, "%s/icons/hicolor/%ux%u/apps/%s.png", base, w, h, game_def.id) >= (int)n)
		return -1;
	return 0;
}

/* mkdir -p of the directory holding `path`. */
static int desktop_mkdirs(const char *path)
{
	char dir[1024];
	char *p;
	snprintf(dir, sizeof dir, "%s", path);
	p = strrchr(dir, '/');
	if(!p)
		return -1;
	*p = 0;
	for(p = dir + 1; ; p++)
	{
		if(*p == '/' || !*p)
		{
			char c = *p;
			*p = 0;
			if(mkdir(dir, 0755) && errno != EEXIST)
				return -1;
			*p = c;
			if(!c)
				return 0;
		}
	}
}

static int desktop_write(const char *path, const void *data, size_t size)
{
	FILE *f = fopen(path, "wb");
	int bad;
	if(!f)
		return -1;
	bad = fwrite(data, 1, size, f) != size;
	bad |= fclose(f) != 0;
	return bad ? -1 : 0;
}

/* Appends `s` to out[*len] (NUL-terminated); -1 if it does not fit. */
static int desktop_cat(char *out, size_t n, size_t *len, const char *s)
{
	size_t k = strlen(s);
	if(*len + k >= n)
		return -1;
	memcpy(out + *len, s, k + 1);
	*len += k;
	return 0;
}

/* Appends `s` escaped as the Desktop Entry Specification asks. Every string value doubles its
 * backslashes. Inside a quoted Exec argument, `"`, `` ` ``, `$` and `\` first take a backslash
 * of their own (so `\` becomes four), and `%` is `%%`. A line break cannot be written. */
static int desktop_put(char *out, size_t n, size_t *len, const char *s, int exec_arg)
{
	for(; *s; s++)
	{
		char one[2] = {*s, 0}, esc[4] = {'\\', '\\', *s, 0};
		const char *rep = one;
		if(*s == '\n' || *s == '\r')
			return -1;
		if(*s == '\\')
			rep = exec_arg ? "\\\\\\\\" : "\\\\";
		else if(exec_arg && (*s == '"' || *s == '`' || *s == '$'))
			rep = esc;
		else if(exec_arg && *s == '%')
			rep = "%%";
		if(desktop_cat(out, n, len, rep))
			return -1;
	}
	return 0;
}

int sdl_desktop_offered(void)
{
	const char *app = getenv("APPIMAGE");
	return app && app[0] == '/' && desktop_asset("desktop") && desktop_asset("png");
}

int sdl_desktop_installed(void)
{
	char entry[1100], icon[1100];
	return !desktop_paths(entry, icon, sizeof entry) && !access(entry, F_OK);
}

int sdl_desktop_install(char *msg, size_t n)
{
	const struct SdlAsset *src = desktop_asset("desktop"), *png = desktop_asset("png");
	const char *app = getenv("APPIMAGE");
	char entry[1100], icon[1100], text[8192];
	size_t len = 0, i = 0;
	if(!sdl_desktop_offered() || desktop_paths(entry, icon, sizeof entry))
	{
		snprintf(msg, n, "The applications menu entry needs the game to run from its AppImage.");
		return -1;
	}
	/* The packaged entry, less its Exec, TryExec and Icon, which point at this AppImage and the
	 * icon written beside it. */
	while(i < src->size)
	{
		size_t end = i;
		while(end < src->size && src->data[end] != '\n')
			end++;
		if(strncmp((const char *)src->data + i, "Exec=", 5) && strncmp((const char *)src->data + i, "TryExec=", 8)
			&& strncmp((const char *)src->data + i, "Icon=", 5))
		{
			if(len + (end - i) + 2 >= sizeof text)
				goto too_long;
			memcpy(text + len, src->data + i, end - i);
			len += end - i;
			text[len++] = '\n';
		}
		i = end + 1;
	}
	text[len] = 0;
	if(desktop_cat(text, sizeof text, &len, "Exec=\"") || desktop_put(text, sizeof text, &len, app, 1)
		|| desktop_cat(text, sizeof text, &len, "\" %F\nTryExec=") || desktop_put(text, sizeof text, &len, app, 0)
		|| desktop_cat(text, sizeof text, &len, "\nIcon=") || desktop_put(text, sizeof text, &len, icon, 0)
		|| desktop_cat(text, sizeof text, &len, "\n"))
		goto too_long;
	if(desktop_mkdirs(icon) || desktop_write(icon, png->data, png->size)
		|| desktop_mkdirs(entry) || desktop_write(entry, text, len))
	{
		snprintf(msg, n, "Could not write %s: %s.", entry, strerror(errno));
		return -1;
	}
	snprintf(msg, n, "%s is in your applications menu. It starts this AppImage, so keep it where it is.", game_def.title);
	return 0;
too_long:
	snprintf(msg, n, "The AppImage's path is too long, or holds a line break.");
	return -1;
}

int sdl_desktop_remove(char *msg, size_t n)
{
	char entry[1100], icon[1100];
	if(desktop_paths(entry, icon, sizeof entry))
	{
		snprintf(msg, n, "No applications menu entry to remove.");
		return -1;
	}
	if((unlink(entry) && errno != ENOENT) || (unlink(icon) && errno != ENOENT))
	{
		snprintf(msg, n, "Could not remove %s: %s.", entry, strerror(errno));
		return -1;
	}
	snprintf(msg, n, "%s is no longer in your applications menu.", game_def.title);
	return 0;
}

#else

int sdl_desktop_offered(void) { return 0; }
int sdl_desktop_installed(void) { return 0; }
int sdl_desktop_install(char *msg, size_t n) { snprintf(msg, n, "Only available on Linux."); return -1; }
int sdl_desktop_remove(char *msg, size_t n) { snprintf(msg, n, "Only available on Linux."); return -1; }

#endif
#endif
