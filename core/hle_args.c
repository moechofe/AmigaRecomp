#include "hle.h"

#include <string.h>

/* dos.library's ReadArgs and FreeArgs (dos.library autodoc, ADCD 2.1; structures from NDK 3.2
 * dos/rdargs.i; error codes from dos/dos.i). Everything ReadArgs makes for the caller (the
 * RDArgs when it was not given one, the strings, numbers and /M arrays) lives in one block,
 * which RDA_DAList points at and FreeArgs frees; the block's first longword is its size. */

#define RDA_SOURCE_BUFFER 0x00
#define RDA_SOURCE_LENGTH 0x04
#define RDA_SOURCE_CURCHR 0x08
#define RDA_DALIST 0x0C
#define RDA_SIZEOF 0x20

#define ERROR_NO_FREE_STORE 103
#define ERROR_BAD_TEMPLATE 114
#define ERROR_BAD_NUMBER 115
#define ERROR_REQUIRED_ARG_MISSING 116
#define ERROR_KEY_NEEDS_ARG 117
#define ERROR_TOO_MANY_ARGS 118
#define ERROR_UNMATCHED_QUOTES 119
#define ERROR_LINE_TOO_LONG 120

#define ARGS_LINE 512
#define ARGS_ITEMS 32
#define ARGS_WORDS 64
#define ARGS_FREE -1
#define ARGS_KEYWORD -2

enum ArgsKind { ArgsString, ArgsSwitch, ArgsToggle, ArgsNumber };

struct ArgsItem
{
	char names[64];  /* the item's keyword and aliases, '=' between them */
	enum ArgsKind kind;
	uint8_t required, keyword, multiple, rest;
	int first;       /* its first word, or -1 */
};

struct ArgsLine
{
	char orig[ARGS_LINE];  /* the line as given, for /F */
	char text[ARGS_LINE];  /* the words, each ended by a 0 */
	int word[ARGS_WORDS];  /* offsets into text */
	int start[ARGS_WORDS]; /* offsets into orig */
	uint8_t quoted[ARGS_WORDS];
	int owner[ARGS_WORDS]; /* the item a word went to, ARGS_FREE or ARGS_KEYWORD */
	int count;
};

static int args_upper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

/* Whether `word` is one of the item's names, case ignored. */
static int args_named(const struct ArgsItem *it, const char *word)
{
	const char *n = it->names;
	size_t len = strlen(word);
	while(*n)
	{
		size_t k = strcspn(n, "="), i;
		if(k == len)
		{
			for(i = 0; i < k && args_upper((unsigned char)n[i]) == args_upper((unsigned char)word[i]); i++)
				;
			if(i == k)
				return 1;
		}
		n += k;
		if(*n == '=')
			n++;
	}
	return 0;
}

static int args_template(const char *t, struct ArgsItem *items, int *count)
{
	int n = 0;
	while(*t)
	{
		struct ArgsItem *it;
		size_t k = strcspn(t, "/,");
		if(n == ARGS_ITEMS || k >= sizeof items[0].names)
			return 0;
		it = &items[n++];
		memset(it, 0, sizeof *it);
		it->first = -1;
		memcpy(it->names, t, k);
		it->names[k] = 0;
		t += k;
		while(*t == '/')
		{
			switch(args_upper((unsigned char)t[1]))
			{
			case 'A': it->required = 1; break;
			case 'K': it->keyword = 1; break;
			case 'S': it->kind = ArgsSwitch; break;
			case 'T': it->kind = ArgsToggle; break;
			case 'N': it->kind = ArgsNumber; break;
			case 'M': it->multiple = 1; break;
			case 'F': it->rest = 1; break;
			default: return 0;
			}
			t += 2;
		}
		if(*t == ',')
			t++;
		else if(*t)
			return 0;
	}
	*count = n;
	return 1;
}

/* Splits the line into words: blanks separate them, `=` ends a keyword, and a quoted word
 * keeps its blanks, with `*` escaping (*N newline, *E escape, *" and ** themselves). The line
 * ends at a newline. */
static uint32_t args_split(struct ArgsLine *l)
{
	const char *p = l->orig;
	char *out = l->text;
	l->count = 0;
	for(;;)
	{
		while(*p == ' ' || *p == '\t')
			p++;
		if(!*p || *p == '\n')
			return 0;
		if(l->count == ARGS_WORDS)
			return ERROR_LINE_TOO_LONG;
		l->start[l->count] = (int)(p - l->orig);
		l->word[l->count] = (int)(out - l->text);
		l->quoted[l->count] = *p == '"';
		l->owner[l->count] = ARGS_FREE;
		l->count++;
		if(*p == '"')
		{
			for(p++; *p != '"'; p++)
			{
				if(!*p || *p == '\n')
					return ERROR_UNMATCHED_QUOTES;
				if(*p == '*' && p[1] && p[1] != '\n')
				{
					p++;
					*out++ = args_upper((unsigned char)*p) == 'N' ? '\n' : args_upper((unsigned char)*p) == 'E' ? 0x1B : *p;
				}
				else
					*out++ = *p;
			}
			p++;
			*out++ = 0;
			continue;
		}
		while(*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '=')
			*out++ = *p++;
		*out++ = 0;
		if(*p == '=')
		{
			p++;
			if(!*p || *p == ' ' || *p == '\t' || *p == '\n')
				return ERROR_KEY_NEEDS_ARG;
		}
	}
}

static int args_number(const char *s, int32_t *v)
{
	int neg = 0;
	int64_t n = 0;
	if(*s == '-' || *s == '+')
		neg = *s++ == '-';
	if(*s < '0' || *s > '9')
		return 0;
	while(*s >= '0' && *s <= '9')
		n = n * 10 + (*s++ - '0');
	if(*s)
		return 0;
	*v = (int32_t)(neg ? -n : n);
	return 1;
}

static void args_fail(struct Amiga *m, uint32_t code)
{
	hle_w32(m, hle_r32(m, HLE_EXEC_BASE + EB_THISTASK) + PR_RESULT2, code);
	m->cpu.d[0] = 0;
}

/* Gives item k the free words from `w` on: one, or for /M every free word but those the
 * required positional items after it still need, or for /F the rest of the line. */
static void args_take(struct ArgsLine *l, struct ArgsItem *items, int count, int k, int w)
{
	struct ArgsItem *it = &items[k];
	int j, need = 0, free = 0;
	it->first = w;
	if(it->rest)
	{
		for(; w < l->count; w++)
			l->owner[w] = k;
		return;
	}
	l->owner[w] = k;
	if(!it->multiple)
		return;
	for(j = k + 1; j < count; j++)
		need += !items[j].keyword && items[j].kind != ArgsSwitch && items[j].required && items[j].first < 0;
	for(j = w + 1; j < l->count; j++)
		free += l->owner[j] == ARGS_FREE;
	free -= need < free ? need : free;
	for(j = w + 1; j < l->count && free; j++)
		if(l->owner[j] == ARGS_FREE)
		{
			l->owner[j] = k;
			free--;
		}
}

/* ReadArgs(d1 template, d2 array, d3 rdargs): the RDArgs, or 0 with IoErr() set. The line is
 * rdargs' CSource when it has a buffer, else `args_line`, the running command's arguments. */
void hle_dos_readargs(struct Amiga *m, uint32_t args_line)
{
	static struct ArgsItem items[ARGS_ITEMS];
	static struct ArgsLine line;
	char tmpl[256];
	uint32_t array = m->cpu.d[2], rda = m->cpu.d[3], src = args_line, len = 0xFFFFFFFFu, i, size, block, at, err;
	int count, w, k;
	for(i = 0; i < sizeof tmpl - 1 && (tmpl[i] = (char)hle_r8(m, m->cpu.d[1] + i)) != 0; i++)
		;
	tmpl[i] = 0;
	if(!args_template(tmpl, items, &count))
	{
		args_fail(m, ERROR_BAD_TEMPLATE);
		return;
	}
	if(rda && hle_r32(m, rda + RDA_SOURCE_BUFFER))
	{
		src = hle_r32(m, rda + RDA_SOURCE_BUFFER) + hle_r32(m, rda + RDA_SOURCE_CURCHR);
		len = hle_r32(m, rda + RDA_SOURCE_LENGTH) - hle_r32(m, rda + RDA_SOURCE_CURCHR);
	}
	for(i = 0; src && i < len && i < sizeof line.orig - 1 && (line.orig[i] = (char)hle_r8(m, src + i)) != 0; i++)
		;
	if(src && i == sizeof line.orig - 1)
	{
		args_fail(m, ERROR_LINE_TOO_LONG);
		return;
	}
	line.orig[src ? i : 0] = 0;
	if((err = args_split(&line)) != 0)
	{
		args_fail(m, err);
		return;
	}
	if(line.count == 1 && !line.quoted[0] && !strcmp(line.text + line.word[0], "?"))
	{
		amiga_fatal(m, "dos: ReadArgs(\"%s\") was asked for help (\"?\"), which prompts on the console", tmpl);
		return;
	}

	/* Keywords first, then the free words in template order. */
	for(w = 0; w < line.count; w++)
	{
		if(line.quoted[w] || line.owner[w] != ARGS_FREE)
			continue;
		for(k = 0; k < count && (items[k].first >= 0 || !args_named(&items[k], line.text + line.word[w])); k++)
			;
		if(k == count)
			continue;
		line.owner[w] = ARGS_KEYWORD;
		if(items[k].kind == ArgsSwitch)
		{
			items[k].first = w;
			continue;
		}
		if(w + 1 >= line.count)
		{
			args_fail(m, ERROR_KEY_NEEDS_ARG);
			return;
		}
		args_take(&line, items, count, k, w + 1);
	}
	for(k = 0; k < count; k++)
	{
		if(items[k].first >= 0 || items[k].keyword || items[k].kind == ArgsSwitch)
			continue;
		for(w = 0; w < line.count && line.owner[w] != ARGS_FREE; w++)
			;
		if(w < line.count)
			args_take(&line, items, count, k, w);
	}
	for(w = 0; w < line.count; w++)
		if(line.owner[w] == ARGS_FREE)
		{
			args_fail(m, ERROR_TOO_MANY_ARGS);
			return;
		}
	for(k = 0; k < count; k++)
		if(items[k].required && items[k].first < 0)
		{
			args_fail(m, ERROR_REQUIRED_ARG_MISSING);
			return;
		}

	/* The block: its size, the RDArgs if the caller gave none, then each item's data. */
	size = 4 + (rda ? 0 : RDA_SIZEOF);
	for(k = 0; k < count; k++)
	{
		const struct ArgsItem *it = &items[k];
		int n = 0;
		if(it->first < 0 || it->kind == ArgsSwitch || it->kind == ArgsToggle)
			continue;
		if(it->rest)
		{
			size += (uint32_t)(strcspn(line.orig + line.start[it->first], "\n") + 4) & ~3u;
			continue;
		}
		for(w = 0; w < line.count; w++)
			if(line.owner[w] == k)
			{
				size += it->kind == ArgsNumber ? 4 : ((uint32_t)strlen(line.text + line.word[w]) + 4) & ~3u;
				n++;
			}
		if(it->multiple)
			size += 4 * (uint32_t)(n + 1);
	}
	block = hle_alloc(m, size, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!block)
	{
		args_fail(m, ERROR_NO_FREE_STORE);
		return;
	}
	hle_w32(m, block, size);
	at = block + 4;
	if(!rda)
	{
		rda = at;
		at += RDA_SIZEOF;
	}
	hle_w32(m, rda + RDA_DALIST, block);
	for(k = 0; k < count; k++)
	{
		const struct ArgsItem *it = &items[k];
		uint32_t slot = array + 4 * (uint32_t)k, vec = 0;
		int n = 0;
		if(it->first < 0)
			continue;
		if(it->kind == ArgsSwitch)
		{
			hle_w32(m, slot, 0xFFFFFFFFu);
			continue;
		}
		if(it->kind == ArgsToggle)
		{
			const char *s = line.text + line.word[it->first];
			int off = (args_upper((unsigned char)s[0]) == 'O' && args_upper((unsigned char)s[1]) == 'F'
				&& args_upper((unsigned char)s[2]) == 'F' && !s[3])
				|| (args_upper((unsigned char)s[0]) == 'N' && args_upper((unsigned char)s[1]) == 'O' && !s[2]);
			hle_w32(m, slot, off ? 0 : 0xFFFFFFFFu);
			continue;
		}
		if(it->rest)
		{
			const char *r = line.orig + line.start[it->first];
			size_t n2 = strcspn(r, "\n");
			hle_w32(m, slot, at);
			for(i = 0; i < n2; i++)
				hle_w8(m, at + i, (uint8_t)r[i]);
			hle_w8(m, at + (uint32_t)n2, 0);
			at += ((uint32_t)n2 + 4) & ~3u;
			continue;
		}
		if(it->multiple)
		{
			for(w = 0; w < line.count; w++)
				n += line.owner[w] == k;
			vec = at;
			at += 4 * (uint32_t)(n + 1);
			hle_w32(m, slot, vec);
			n = 0;
		}
		for(w = 0; w < line.count; w++)
		{
			const char *s = line.text + line.word[w];
			uint32_t value = at;
			if(line.owner[w] != k)
				continue;
			if(it->kind == ArgsNumber)
			{
				int32_t v;
				if(!args_number(s, &v))
				{
					hle_free(m, block, size);
					hle_w32(m, rda + RDA_DALIST, 0);
					args_fail(m, ERROR_BAD_NUMBER);
					return;
				}
				hle_w32(m, at, (uint32_t)v);
				at += 4;
			}
			else
			{
				size_t len2 = strlen(s);
				for(i = 0; i <= len2; i++)
					hle_w8(m, at + i, (uint8_t)s[i]);
				at += ((uint32_t)len2 + 4) & ~3u;
			}
			if(it->multiple)
				hle_w32(m, vec + 4 * (uint32_t)n++, value);
			else
				hle_w32(m, slot, value);
		}
	}
	m->cpu.d[0] = rda;
}

/* FreeArgs(d1 rdargs): frees what ReadArgs made, the RDArgs too when ReadArgs made it. */
void hle_dos_freeargs(struct Amiga *m)
{
	uint32_t rda = m->cpu.d[1], block;
	if(!rda)
		return;
	block = hle_r32(m, rda + RDA_DALIST);
	if(!block)
		return;
	hle_w32(m, rda + RDA_DALIST, 0);
	hle_free(m, block, hle_r32(m, block));
}
