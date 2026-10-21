#ifndef SCRIPT_H
#define SCRIPT_H

#include "amiga.h"

#include <stddef.h>

/* Headless input scripts: one directive per line, `#` comments. The frontend owns the text;
 * the core walks it a frame at a time. docs/runtime.md, "Input scripts". */

enum ScriptResult
{
	ScriptRunFrame,
	ScriptShot,
	ScriptStateSave,
	ScriptStateLoad,
	ScriptQuit,
	ScriptTimeout,
	ScriptError
};

struct Script
{
	const char *text;
	size_t pos;
	int lineno;
	char line[128];
	int active;
	int phase;
	int count;
	int frames;
	int arg[3];
	int joy_on;  /* 1 once a `joy` directive has run: the port-2 joystick is held at `joy` */
	uint8_t joy;
	char name[64];
	char error[256];
};

void script_init(struct Script *s, const char *text);
enum ScriptResult script_tick(struct Script *s, struct Amiga *m);
int script_colours(const struct Amiga *m);

#endif
