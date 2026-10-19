#include "hle.h"
#include "cpu_ops.h"

#include <stdio.h>
#include <string.h>

/* dos.library and the Initial CLI, over the OFS/FFS reader (hle_ofs.c) and the hunk loader
 * (hle_hunk.c). Functions by negative LVO from NDK 3.2 FD/dos_lib.fd, each from its autodoc;
 * structures from NDK 3.2 dos/dos.i and dos/dosextens.i. Every function returns its result in D0
 * and in D1, as the overlay manager reads it (docs/runtime.md, "dos.library"). */

#define D0 m->cpu.d[0]
#define D1 m->cpu.d[1]
#define D2 m->cpu.d[2]
#define D3 m->cpu.d[3]

#define RT_MATCHWORD 0x00
#define RT_MATCHTAG 0x02
#define RT_ENDSKIP 0x06
#define RT_TYPE 0x0C
#define RT_NAME 0x0E
#define RT_INIT 0x16
#define DL_ROOT 0x22
#define DL_SIZEOF 0x36
#define RN_TASKARRAY 0x00
#define RN_INFO 0x18
#define DI_DEVINFO 0x04
#define DOL_NEXT 0x00
#define DOL_TYPE 0x04
#define DOL_TASK 0x08
#define DOL_LOCK 0x0C
#define DOL_NAME 0x28
#define DOL_SIZEOF 0x2C
#define DLT_DEVICE 0
#define DLT_DIRECTORY 1
#define DLT_VOLUME 2
#define FH_INTERACTIVE 0x04
#define FH_ARG1 0x24
#define FH_SIZEOF 0x2C
#define FL_KEY 0x04
#define FL_ACCESS 0x08
#define FL_VOLUME 0x10
#define FL_SIZEOF 0x14
/* Lock's access modes, dos/dos.i: ACCESS_READ is SHARED_LOCK. */
#define SHARED_LOCK -2
/* dos/dosasl.i: an AnchorPath and an AChain; dos/dos.i: ERROR_NO_MORE_ENTRIES. */
#define AP_BASE 0x00
#define AP_LAST 0x04
#define AP_STRLEN 0x12
#define AP_INFO 0x14
#define AP_BUF 0x118
#define AN_LOCK 0x08
#define AN_INFO 0x0C
#define AN_SIZEOF 0x114
#define HLE_DOS_NO_MORE_ENTRIES 232
#define FIB_DISKKEY 0x00
#define FIB_DIRENTRYTYPE 0x04
#define FIB_FILENAME 0x08
#define FIB_PROTECTION 0x74
#define FIB_ENTRYTYPE 0x78
#define FIB_SIZE 0x7C
#define FIB_NUMBLOCKS 0x80
#define FIB_DATE 0x84
#define FIB_COMMENT 0x90
#define CLI_RETURNCODE 0x0C
#define CLI_COMMANDNAME 0x10
#define CLI_STANDARDINPUT 0x1C
#define CLI_CURRENTINPUT 0x20
#define CLI_CURRENTOUTPUT 0x30
#define CLI_DEFAULTSTACK 0x34
#define CLI_STANDARDOUTPUT 0x38
#define CLI_MODULE 0x3C
#define CLI_SIZEOF 0x40
#define MODE_READWRITE 1004
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT 0
#define OFFSET_END 1
#define DOSTRUE 0xFFFFFFFFu
#define SIGB_DOS 8

/* The AmigaDOS Manual, STACK: "the default stack size is 4000 bytes". */
#define CLI_STACK_LONGS 1000
#define DOS_PATH 256
#define DOS_LINE 256
/* HleDosFile.drive of a NIL: handle and, on the A1200, of the CON: window; the console's is -1. */
#define DOS_NIL -2
#define DOS_CON -3

static const char *const dos_system_assigns[][2] = {
	{"SYS", ""}, {"C", "c"}, {"S", "s"}, {"L", "l"}, {"LIBS", "libs"}, {"DEVS", "devs"}, {"FONTS", "fonts"},
};

/* --- small helpers ------------------------------------------------------------------------ */

static int dos_upper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static int dos_same(const char *a, const char *b)
{
	for(; *a && *b; a++, b++)
		if(dos_upper((uint8_t)*a) != dos_upper((uint8_t)*b))
			return 0;
	return *a == *b;
}

static int dos_cstr(struct Amiga *m, uint32_t at, char *out, uint32_t max)
{
	uint32_t i;
	for(i = 0; i + 1 < max; i++)
		if(!(out[i] = (char)hle_r8(m, at + i)))
			return 0;
	out[i] = 0;
	amiga_fatal(m, "dos: a name longer than %u characters", max - 1);
	return -1;
}

static void dos_bstr(struct Amiga *m, uint32_t at, const char *s)
{
	uint32_t n = (uint32_t)strlen(s), i;
	hle_w8(m, at, (uint8_t)n);
	for(i = 0; i < n; i++)
		hle_w8(m, at + 1 + i, (uint8_t)s[i]);
}

static uint32_t dos_process(struct Amiga *m)
{
	return hle_r32(m, HLE_EXEC_BASE + EB_THISTASK);
}

static void dos_error(struct Amiga *m, uint32_t code)
{
	hle_w32(m, dos_process(m) + PR_RESULT2, code);
}

static const uint8_t *dos_image(struct Amiga *m, int drive)
{
	int role = drive >= 0 && drive < AMIGA_DRIVES ? m->drive_role[drive] : -1;
	return role >= 0 ? m->disks[role].image : 0;
}

/* A guest buffer as host bytes, or 0 (after a fatal) when it is not wholly inside one RAM. */
static uint8_t *dos_buffer(struct Amiga *m, uint32_t at, uint32_t n)
{
	uint8_t *p = mem_ram(m, at);
	if(!n)
		return p;
	if(!p || mem_ram(m, at + n - 1) != p + n - 1)
	{
		amiga_fatal(m, "dos: a buffer of %u bytes at 0x%06X is not wholly in RAM", n, at);
		return 0;
	}
	return p;
}

/* --- volumes, assigns and the device list -------------------------------------------------- */

static uint32_t dos_node(struct Amiga *m, int32_t type, const char *name, uint32_t lock)
{
	uint32_t n = hle_alloc(m, DOL_SIZEOF + 32, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!n)
	{
		amiga_fatal(m, "dos: no memory for the device list");
		return 0;
	}
	hle_w32(m, n + DOL_TYPE, (uint32_t)type);
	hle_w32(m, n + DOL_LOCK, lock);
	dos_bstr(m, n + DOL_SIZEOF, name);
	hle_w32(m, n + DOL_NAME, (n + DOL_SIZEOF) >> 2);
	hle_w32(m, n + DOL_NEXT, hle_r32(m, HLE_DOS_INFO + DI_DEVINFO));
	hle_w32(m, HLE_DOS_INFO + DI_DEVINFO, n >> 2);
	return n;
}

static uint32_t dos_volume_node(struct Amiga *m, const char *name)
{
	struct HleDos *d = &m->hle.dos;
	int i;
	for(i = 0; i < d->volume_count; i++)
		if(!strcmp(d->volumes[i].name, name))
			return d->volumes[i].node;
	if(d->volume_count == HLE_DOS_VOLUMES)
	{
		amiga_fatal(m, "dos: more than %d volumes", HLE_DOS_VOLUMES);
		return 0;
	}
	strcpy(d->volumes[d->volume_count].name, name);
	return d->volumes[d->volume_count++].node = dos_node(m, DLT_VOLUME, name, 0);
}

/* Name every drive's volume, and give each new one its DLT_VOLUME node. */
void hle_dos_mount(struct Amiga *m)
{
	struct HleDos *d = &m->hle.dos;
	int n;
	for(n = 0; n < AMIGA_DRIVES; n++)
	{
		const uint8_t *adf = dos_image(m, n);
		d->volume[n][0] = 0;
		if(!adf)
			continue;
		if(adf[0] == 'D' && adf[1] == 'O' && adf[2] == 'S' && adf[3] > 1)
		{
			amiga_fatal(m, "dos: DF%d holds a DOS\\%d volume; only DOS\\0 (OFS) and DOS\\1 (FFS) are implemented",
				n, adf[3]);
			return;
		}
		if(hle_ofs_volume(adf, d->volume[n]) == 0 && !dos_volume_node(m, d->volume[n]))
			return;
	}
}

static int dos_drive_of_volume(struct Amiga *m, uint32_t node)
{
	int n, i;
	for(i = 0; i < m->hle.dos.volume_count; i++)
		if(m->hle.dos.volumes[i].node == node)
			for(n = 0; n < AMIGA_DRIVES; n++)
				if(!strcmp(m->hle.dos.volume[n], m->hle.dos.volumes[i].name))
					return n;
	return -1;
}

/* --- the handlers' packets ---------------------------------------------------------------- */

#define DP_PORT 0x04
#define DP_TYPE 0x08
#define DP_RES1 0x0C
#define DP_RES2 0x10
#define DP_ARG1 0x14
#define ACTION_INHIBIT 31
#define MP_SIZEOF 0x22
#define PA_IGNORE 2

/* A message PutMsg sends to DFn's handler port holds a DosPacket at its ln_Name (dos/dosextens.i,
 * StandardPacket). The packet is done at once and returned as a handler returns it: dp_Port set
 * to the handler's port, then PutMsg to the port dp_Port named (AmigaDOS Technical Reference
 * Manual, "Packets"). ACTION_INHIBIT (dos/dosextens.i) is the only type: inhibiting does nothing,
 * since this filesystem holds no state between calls, and ending it reads the disks again, as a
 * filesystem does once it is no longer inhibited. Returns 0 when `port` is no handler's. */
int hle_dos_packet(struct Amiga *m, uint32_t port, uint32_t msg)
{
	struct HleDos *d = &m->hle.dos;
	uint32_t pkt, reply;
	int n;
	for(n = 0; n < AMIGA_DRIVES && (!d->handler[n] || d->handler[n] != port); n++)
		;
	if(n == AMIGA_DRIVES)
		return 0;
	pkt = hle_r32(m, msg + LN_NAME);
	switch(hle_r32(m, pkt + DP_TYPE))
	{
	case ACTION_INHIBIT:
		amiga_logf(m, "dos: DF%d %s", n, hle_r32(m, pkt + DP_ARG1) ? "inhibited" : "no longer inhibited");
		if(!hle_r32(m, pkt + DP_ARG1))
			hle_dos_mount(m);
		hle_w32(m, pkt + DP_RES1, DOSTRUE);
		hle_w32(m, pkt + DP_RES2, 0);
		break;
	default:
		amiga_fatal(m, "dos: packet type %d to DF%d's handler is not implemented", (int32_t)hle_r32(m, pkt + DP_TYPE), n);
		return 1;
	}
	reply = hle_r32(m, pkt + DP_PORT);
	hle_w32(m, pkt + DP_PORT, port);
	hle_w8(m, msg + LN_TYPE, NT_MESSAGE);
	hle_put_msg(m, reply, msg);
	return 1;
}

/* --- locks -------------------------------------------------------------------------------- */

static uint32_t dos_lock(struct Amiga *m, int drive, uint32_t block, int32_t access)
{
	uint32_t l = hle_alloc(m, FL_SIZEOF, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!l)
	{
		dos_error(m, HLE_DOS_NO_FREE_STORE);
		return 0;
	}
	hle_w32(m, l + FL_KEY, block);
	hle_w32(m, l + FL_ACCESS, (uint32_t)access);
	hle_w32(m, l + FL_VOLUME, dos_volume_node(m, m->hle.dos.volume[drive]) >> 2);
	return l >> 2;
}

/* The drive and header block of a lock BPTR; 0 is the boot volume's root (AmigaDOS Technical
 * Reference Manual, "Locks"). Returns 0, or -1 after a fatal or with pr_Result2 set. */
static int dos_lock_place(struct Amiga *m, uint32_t lock, int *drive, uint32_t *block)
{
	int n;
	if(!lock)
	{
		*drive = 0;
		*block = 880;
		return 0;
	}
	n = dos_drive_of_volume(m, hle_r32(m, (lock << 2) + FL_VOLUME) << 2);
	if(n < 0)
	{
		amiga_fatal(m, "dos: a lock's volume is in no drive");
		return -1;
	}
	*drive = n;
	*block = hle_r32(m, (lock << 2) + FL_KEY);
	return 0;
}

/* --- paths -------------------------------------------------------------------------------- */

/* A volume no drive holds. With pr_WindowPtr -1 the call fails with
 * ERROR_DEVICE_NOT_MOUNTED. Otherwise Kickstart 1.3 opens a requester in its ROM font, which is
 * fatal here; Kickstart 3.0's dos asks through intuition's EasyRequestArgs, whatever patches
 * it, once the call has failed (hle_dos_call). */
static int dos_volume_missing(struct Amiga *m, const char *name)
{
	if(hle_r32(m, dos_process(m) + PR_WINDOWPTR) != DOSTRUE)
	{
#if AMIGA_A1200
		if(!m->hle.dos.request[0])
			snprintf(m->hle.dos.request, sizeof m->hle.dos.request, "%s", name);
#else
		amiga_fatal(m, "dos: volume \"%s\" is in no drive", name);
		return -1;
#endif
	}
	dos_error(m, HLE_DOS_DEVICE_NOT_MOUNTED);
	return -1;
}

/* NAME: is an assign, a volume or DF0:-DF3:; a leading ':' is the current volume's root; '/'
 * is the parent; anything else is relative to `lock`, or to pr_CurrentDir when it is 0
 * (AmigaDOS Manual, "Filenames"). Returns 0 with the place, or -1 with pr_Result2 set or a fatal. */
static int dos_resolve(struct Amiga *m, uint32_t lock, const char *path, int *drive, uint32_t *block)
{
	struct HleDos *d = &m->hle.dos;
	const char *colon = strchr(path, ':'), *p;
	const uint8_t *adf;
	if(colon)
	{
		char dev[32];
		size_t n = (size_t)(colon - path);
		int i, found = 0;
		if(n >= sizeof dev)
		{
			dos_error(m, HLE_DOS_OBJECT_NOT_FOUND);
			return -1;
		}
		memcpy(dev, path, n);
		dev[n] = 0;
		p = colon + 1;
		if(!n)
		{
			if(dos_lock_place(m, lock ? lock : hle_r32(m, dos_process(m) + PR_CURRENTDIR), drive, block) < 0)
				return -1;
			*block = 880;
			found = 1;
		}
		for(i = 0; !found && i < AMIGA_DRIVES; i++)
		{
			char df[4] = {'D', 'F', (char)('0' + i), 0};
			if(dos_same(dev, df))
			{
				if(!d->volume[i][0])
					return dos_volume_missing(m, dev);
				*drive = i;
				*block = 880;
				found = 1;
			}
		}
		for(i = 0; !found && i < d->assign_count; i++)
		{
			if(dos_same(dev, d->assigns[i].name))
			{
				*drive = d->assigns[i].drive;
				*block = d->assigns[i].block;
				found = 1;
			}
		}
		for(i = 0; !found && i < d->volume_count; i++)
		{
			if(dos_same(dev, d->volumes[i].name))
			{
				int k;
				for(k = 0; k < AMIGA_DRIVES && strcmp(d->volume[k], d->volumes[i].name); k++)
					;
				if(k == AMIGA_DRIVES)
					return dos_volume_missing(m, d->volumes[i].name);
				*drive = k;
				*block = 880;
				found = 1;
			}
		}
		/* A name that is no device and no assign is a volume's, which no drive holds (AmigaDOS
		 * Manual, "Filenames"). */
		if(!found)
			return dos_volume_missing(m, dev);
	}
	else
	{
		p = path;
		if(dos_lock_place(m, lock ? lock : hle_r32(m, dos_process(m) + PR_CURRENTDIR), drive, block) < 0)
			return -1;
	}
	adf = dos_image(m, *drive);
	if(!adf || !d->volume[*drive][0])
		return dos_volume_missing(m, path);
	while(*p)
	{
		char part[32];
		const char *slash = strchr(p, '/');
		size_t n = slash ? (size_t)(slash - p) : strlen(p);
		int32_t at;
		if(!n)
		{
			struct HleOfsEntry e;
			if(hle_ofs_entry(adf, *block, &e) < 0 || !e.parent)
			{
				dos_error(m, HLE_DOS_OBJECT_NOT_FOUND);
				return -1;
			}
			*block = e.parent;
			p++;
			continue;
		}
		if(n >= sizeof part)
		{
			dos_error(m, HLE_DOS_OBJECT_NOT_FOUND);
			return -1;
		}
		memcpy(part, p, n);
		part[n] = 0;
		at = hle_ofs_lookup(adf, *block, part);
		if(at < 0)
		{
			dos_error(m, HLE_DOS_OBJECT_NOT_FOUND);
			return -1;
		}
		*block = (uint32_t)at;
		p += n;
		if(*p == '/')
			p++;
	}
	return 0;
}

/* --- files -------------------------------------------------------------------------------- */

static struct HleDosFile *dos_file(struct Amiga *m, uint32_t fh)
{
	uint32_t i;
	if(!fh)
		return 0;
	i = hle_r32(m, (fh << 2) + FH_ARG1);
	if(i < 1 || i > HLE_DOS_FILES || !m->hle.dos.files[i - 1].used || m->hle.dos.files[i - 1].fh != fh)
	{
		amiga_fatal(m, "dos: 0x%06X is not a FileHandle this runtime opened", fh << 2);
		return 0;
	}
	return &m->hle.dos.files[i - 1];
}

/* A FileHandle in a free slot; 0 with pr_Result2 set, or after a fatal. */
static uint32_t dos_handle(struct Amiga *m, int drive, uint32_t header, uint32_t size)
{
	struct HleDos *d = &m->hle.dos;
	uint32_t fh;
	int i;
	for(i = 1; i < HLE_DOS_FILES && d->files[i].used; i++)
		;
	if(i == HLE_DOS_FILES)
	{
		amiga_fatal(m, "dos: more than %d files open", HLE_DOS_FILES - 1);
		return 0;
	}
	fh = hle_alloc(m, FH_SIZEOF, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!fh)
	{
		dos_error(m, HLE_DOS_NO_FREE_STORE);
		return 0;
	}
	hle_w32(m, fh + FH_ARG1, (uint32_t)i + 1);
	d->files[i].used = 1;
	d->files[i].drive = (int8_t)drive;
	d->files[i].header = header;
	d->files[i].size = size;
	d->files[i].pos = 0;
	d->files[i].fh = fh >> 2;
	return fh >> 2;
}

/* Open a file for reading, or NIL: ("NIL: is a dummy device: output written to it is
 * discarded, and input read from it is empty", AmigaDOS Manual 1.3, "Devices"); returns the
 * FileHandle BPTR, or 0 with pr_Result2 set. */
static uint32_t dos_open(struct Amiga *m, const char *path, const char *why)
{
	struct HleOfsEntry e;
	uint32_t block, fh;
	int drive;
	if(dos_same(path, "NIL:"))
		return dos_handle(m, DOS_NIL, 0, 0);
	if(dos_resolve(m, 0, path, &drive, &block) < 0)
		return 0;
	if(hle_ofs_entry(dos_image(m, drive), block, &e) < 0 || e.type >= 0)
	{
		dos_error(m, HLE_DOS_OBJECT_WRONG_TYPE);
		return 0;
	}
	fh = dos_handle(m, drive, block, e.size);
	if(fh)
		amiga_logf(m, "dos: %s(\"%s\") DF%d %u bytes", why, path, drive, e.size);
	return fh;
}

static void dos_close(struct Amiga *m, uint32_t fh)
{
	struct HleDosFile *f = dos_file(m, fh);
	if(!f || f == &m->hle.dos.files[0])
		return;
#if AMIGA_A1200
	if(f->drive == DOS_CON)
		hle_con_close(m);
#endif
	f->used = 0;
	hle_free(m, fh << 2, FH_SIZEOF);
}

static uint32_t dos_read(struct Amiga *m, struct HleDosFile *f, uint8_t *out, uint32_t n)
{
	const uint8_t *adf;
	uint32_t got;
	if(f->drive == DOS_NIL)
		return 0;
	adf = dos_image(m, f->drive);
	if(!adf)
	{
		amiga_fatal(m, "dos: reading a file whose disk has left DF%d", f->drive);
		return 0;
	}
	got = hle_ofs_read(adf, f->header, f->pos, out, n);
	m->disk_accesses++;
	f->pos += got;
	return got;
}

static uint32_t dos_hunk_read(struct Amiga *m, void *ctx, uint8_t *out, uint32_t n)
{
	return dos_read(m, ctx, out, n);
}

/* --- LoadSeg ------------------------------------------------------------------------------ */

static uint32_t dos_loadseg_fh(struct Amiga *m, uint32_t fh)
{
	struct HleDos *d = &m->hle.dos;
	struct HleDosFile *f = dos_file(m, fh);
	struct HleHunkSource src = {dos_hunk_read, f};
	struct HleHunkOverlay ov;
	uint32_t seg;
	int32_t err;
	int i;
	if(!f)
		return 0;
	seg = hle_hunk_load(m, &src, fh, &err, &ov);
	if(!seg || !ov.table)
	{
		dos_close(m, fh);
		if(!seg)
			dos_error(m, (uint32_t)err);
		return seg;
	}
	for(i = 0; i < HLE_DOS_OVERLAYS && d->overlays[i].seglist; i++)
		;
	if(i == HLE_DOS_OVERLAYS)
	{
		amiga_fatal(m, "dos: more than %d overlaid programs loaded", HLE_DOS_OVERLAYS);
		return 0;
	}
	d->overlays[i].seglist = seg;
	d->overlays[i].table = ov.table;
	d->overlays[i].size = ov.size;
	d->overlays[i].hunks = ov.hunks;
	d->overlays[i].hunks_size = ov.hunks_size;
	d->overlays[i].stream = fh;
	return seg;
}

uint32_t hle_dos_loadseg(struct Amiga *m, const char *path)
{
	uint32_t fh = dos_open(m, path, "LoadSeg");
	return fh ? dos_loadseg_fh(m, fh) : 0;
}

/* UnLoadSeg: "Overlaid segments will have all appropriate files closed" (dos autodoc). */
static void dos_unloadseg(struct Amiga *m, uint32_t seg)
{
	struct HleDos *d = &m->hle.dos;
	int i;
	hle_hunk_unload(m, seg);
	for(i = 0; seg && i < HLE_DOS_OVERLAYS; i++)
	{
		struct HleDosOverlay *o = &d->overlays[i];
		if(o->seglist != seg)
			continue;
		hle_free(m, o->table, o->size);
		hle_free(m, o->hunks, o->hunks_size);
		dos_close(m, o->stream);
		memset(o, 0, sizeof *o);
	}
}

/* --- the CLI ------------------------------------------------------------------------------ */

static void dos_cli_next(struct Amiga *m);

/* The script's next line, without its end; 0 at the end of the script. */
static int dos_script_line(struct Amiga *m, char *line)
{
	struct HleDos *d = &m->hle.dos;
	const uint8_t *adf = dos_image(m, d->script_drive);
	uint32_t n = 0;
	uint8_t c;
	if(!adf)
	{
		amiga_fatal(m, "CLI: the disk holding S:Startup-Sequence has left DF%d", d->script_drive);
		return 0;
	}
	m->disk_accesses++;
	if(!hle_ofs_read(adf, d->script_header, d->script_pos, &c, 1))
		return 0;
	while(hle_ofs_read(adf, d->script_header, d->script_pos, &c, 1) == 1)
	{
		d->script_pos++;
		if(c == '\n')
			break;
		if(n + 1 < DOS_LINE)
			line[n++] = (char)c;
	}
	line[n] = 0;
	return 1;
}

/* The line without its comment and outer blanks (AmigaDOS Manual 1.3, "The CLI"). */
static char *dos_cli_trim(char *line)
{
	int quoted = 0;
	char *p, *e;
	for(p = line; *p; p++)
	{
		if(*p == '"')
			quoted = !quoted;
		else if(*p == ';' && !quoted)
		{
			*p = 0;
			break;
		}
	}
	for(p = line; *p == ' ' || *p == '\t'; p++)
		;
	for(e = p + strlen(p); e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'); e--)
		;
	*e = 0;
	return p;
}

static void dos_cli_run(struct Amiga *m, const char *cmd, const char *args, int nil_in, int nil_out);

/* The command word at *p into cmd, quoted or not; *p moves past it. -1 after a fatal. */
static int dos_cli_word(struct Amiga *m, char **p, char *cmd)
{
	char *s = *p, *e;
	if(*s == '"')
	{
		e = strchr(s + 1, '"');
		if(!e)
		{
			amiga_fatal(m, "CLI: unmatched quote: %s", s);
			return -1;
		}
		memcpy(cmd, s + 1, (size_t)(e - s - 1));
		cmd[e - s - 1] = 0;
		*p = e + 1;
		return 0;
	}
	for(e = s; *e && *e != ' ' && *e != '\t'; e++)
		;
	memcpy(cmd, s, (size_t)(e - s));
	cmd[e - s] = 0;
	*p = e;
	return 0;
}

/* ENDCLI: the Initial CLI's console window closes and the CLI ends (AmigaDOS Manual 1.3, ENDCLI). */
static void dos_cli_end(struct Amiga *m)
{
	m->hle.dos.cli_ended = 1;
	amiga_logf(m, "CLI: ENDCLI; the Initial CLI's window closes");
}

/* RUN's command starts as its own process while the CLI goes on with the script (AmigaDOS
 * Manual 1.3, RUN). Here it runs in the CLI, so only a next line that is ENDCLI goes first, as
 * the oracle shows it (docs/runtime.md, "dos.library"). */
static void dos_cli_endcli_next(struct Amiga *m)
{
	struct HleDos *d = &m->hle.dos;
	char line[DOS_LINE], cmd[DOS_LINE], *p;
	uint32_t pos = d->script_pos;
	do
	{
		if(!dos_script_line(m, line))
		{
			d->script_pos = pos;
			return;
		}
		p = dos_cli_trim(line);
	}
	while(!*p);
	if(dos_cli_word(m, &p, cmd) == 0 && dos_same(cmd, "endcli"))
		dos_cli_end(m);
	else
		d->script_pos = pos;
}

/* One command line: the command word, then the redirections `<name` and `>name` right after it,
 * then the argument string (AmigaDOS Manual 1.3, "The CLI"); only NIL: is implemented. RUN and
 * ENDCLI are the CLI's own: Workbench 1.3's C:Run and C:EndCLI are BCPL programs, which need the
 * BCPL global vector this runtime does not have (docs/formats/hunk.md). A redirection of RUN
 * itself passes to its command. */
static void dos_cli_command(struct Amiga *m, char *p, int nil_in, int nil_out)
{
	char cmd[DOS_LINE], args[DOS_LINE + 1], *e;
	if(dos_cli_word(m, &p, cmd) < 0)
		return;
	for(;;)
	{
		char name[DOS_LINE];
		int to;
		while(*p == ' ' || *p == '\t')
			p++;
		if(*p != '<' && *p != '>')
			break;
		to = *p++ == '>';
		for(e = p; *e && *e != ' ' && *e != '\t'; e++)
			;
		memcpy(name, p, (size_t)(e - p));
		name[e - p] = 0;
		p = e;
		if(!dos_same(name, "NIL:"))
		{
			amiga_fatal(m, "CLI: redirection %s%s is not implemented; only NIL: is", to ? ">" : "<", name);
			return;
		}
		if(to)
			nil_out = 1;
		else
			nil_in = 1;
	}
	if(dos_same(cmd, "run"))
	{
		if(!*p)
		{
			amiga_fatal(m, "CLI: RUN with no command");
			return;
		}
		amiga_logf(m, "CLI: RUN %s", p);
		dos_cli_endcli_next(m);
		dos_cli_command(m, p, nil_in, nil_out);
		return;
	}
	if(dos_same(cmd, "endcli"))
	{
		dos_cli_end(m);
		amiga_fatal(m, "CLI: ENDCLI ended the Initial CLI; no other process runs");
		return;
	}
	strcpy(args, p);
	strcat(args, "\n");
	dos_cli_run(m, cmd, args, nil_in, nil_out);
}

/* The command's Input() and Output(): the process's and the CLI's current streams. */
static void dos_cli_io(struct Amiga *m, uint32_t in, uint32_t out)
{
	struct HleDos *d = &m->hle.dos;
	hle_w32(m, d->process + PR_CIS, in);
	hle_w32(m, d->process + PR_COS, out);
	hle_w32(m, d->cli + CLI_CURRENTINPUT, in);
	hle_w32(m, d->cli + CLI_CURRENTOUTPUT, out);
}

static void dos_cli_run(struct Amiga *m, const char *cmd, const char *args, int nil_in, int nil_out)
{
	struct HleDos *d = &m->hle.dos;
	char path[DOS_LINE + 2];
	uint32_t seg = 0, len = (uint32_t)strlen(args), top, i, name;
	int drive;
	uint32_t block;
	if(strchr(cmd, ':') || strchr(cmd, '/'))
		snprintf(path, sizeof path, "%s", cmd);
	else if(dos_resolve(m, 0, cmd, &drive, &block) == 0)
		snprintf(path, sizeof path, "%s", cmd);
	else
		snprintf(path, sizeof path, "C:%s", cmd);
	if(m->fatal)
		return;
	seg = hle_dos_loadseg(m, path);
	if(!seg)
	{
		if(!m->fatal)
			amiga_fatal(m, "CLI: unknown command %s", cmd);
		return;
	}
#if AMIGA_A1200
	/* pr_HomeDir: a lock on the directory the command was loaded from (dos autodoc,
	 * GetProgramDir), unlocked when it returns. */
	{
		struct HleOfsEntry e;
		if(dos_resolve(m, 0, path, &drive, &block) == 0 && hle_ofs_entry(dos_image(m, drive), block, &e) == 0)
			hle_w32(m, d->process + PR_HOMEDIR, dos_lock(m, drive, e.parent, SHARED_LOCK));
	}
#endif
	d->cmd_seglist = seg;
	d->cmd_stack_size = hle_r32(m, d->cli + CLI_DEFAULTSTACK) * 4;
	d->cmd_stack = hle_alloc(m, d->cmd_stack_size, MEMF_PUBLIC, 0);
	d->cmd_args_size = len + 1;
	d->cmd_args = hle_alloc(m, d->cmd_args_size, MEMF_PUBLIC, 0);
	if(!d->cmd_stack || !d->cmd_args)
	{
		amiga_fatal(m, "CLI: no memory to run %s", cmd);
		return;
	}
	for(i = 0; i <= len; i++)
		hle_w8(m, d->cmd_args + i, (uint8_t)args[i]);
	d->cmd_in = nil_in ? dos_open(m, "NIL:", "CLI") : 0;
	d->cmd_out = nil_out ? dos_open(m, "NIL:", "CLI") : 0;
	if(m->fatal)
		return;
	dos_cli_io(m, d->cmd_in ? d->cmd_in : d->files[0].fh, d->cmd_out ? d->cmd_out : d->files[0].fh);
	name = hle_r32(m, d->cli + CLI_COMMANDNAME) << 2;
	dos_bstr(m, name, cmd);
	hle_w32(m, d->cli + CLI_MODULE, seg);
	top = d->cmd_stack + d->cmd_stack_size;
	hle_w32(m, d->process + TC_SPLOWER, d->cmd_stack);
	hle_w32(m, d->process + TC_SPUPPER, top);
	hle_w32(m, d->process + PR_STACKSIZE, d->cmd_stack_size);

	/* Program entry (AmigaDOS Technical Reference Manual, "Program entry"), with the registers
	 * docs/runtime.md, "dos.library", identified at the oracle's entry; the others are 0. */
	memset(m->cpu.d, 0, sizeof m->cpu.d);
	memset(m->cpu.a, 0, sizeof m->cpu.a);
	m->cpu.a[7] = top - 8;
	hle_w32(m, top - 8, HLE_DOS_CLI_RETURN);
	hle_w32(m, top - 4, d->cmd_stack_size);
	D0 = len;
	D1 = d->cmd_args;
	D2 = d->cmd_stack_size;
	m->cpu.a[0] = d->cmd_args;
	m->cpu.a[3] = seg << 2;
	m->cpu.a[4] = top - 4;
	m->cpu.pc = (seg << 2) + 4;
}

static void dos_cli_next(struct Amiga *m)
{
	char line[DOS_LINE], *p;
	do
	{
		if(!dos_script_line(m, line))
		{
			if(!m->fatal)
				amiga_fatal(m, "CLI: S:Startup-Sequence ended; the CLI has no console window");
			return;
		}
		p = dos_cli_trim(line);
	}
	while(!*p);
	dos_cli_command(m, p, 0, 0);
}

/* A command returned into HLE_DOS_CLI_RETURN with its return code in D0. */
void hle_dos_cli_return(struct Amiga *m)
{
	struct HleDos *d = &m->hle.dos;
	hle_w32(m, d->cli + CLI_RETURNCODE, D0);
	hle_w32(m, d->cli + CLI_MODULE, 0);
	amiga_logf(m, "CLI: command returned %d", (int32_t)D0);
	dos_unloadseg(m, d->cmd_seglist);
	hle_free(m, d->cmd_stack, d->cmd_stack_size);
	hle_free(m, d->cmd_args, d->cmd_args_size);
	d->cmd_seglist = 0;
#if AMIGA_A1200
	if(hle_r32(m, d->process + PR_HOMEDIR))
		hle_free(m, hle_r32(m, d->process + PR_HOMEDIR) << 2, FL_SIZEOF);
	hle_w32(m, d->process + PR_HOMEDIR, 0);
#endif
	dos_cli_io(m, d->files[0].fh, d->files[0].fh);
	if(d->cmd_in)
		dos_close(m, d->cmd_in);
	if(d->cmd_out)
		dos_close(m, d->cmd_out);
	d->cmd_in = d->cmd_out = 0;
	if(d->cli_ended)
	{
		amiga_fatal(m, "CLI: the command RUN started has returned; ENDCLI had ended the Initial CLI");
		return;
	}
	dos_cli_next(m);
}

/* --- boot --------------------------------------------------------------------------------- */

/* The Resident, the library base, the RootNode and the DosInfo, at fixed addresses
 * (docs/runtime.md, "HLE"). */
void hle_dos_build(struct Amiga *m)
{
	uint32_t s_dos = hle_string(m, HLE_STRINGS + 0xC0, "dos.library");
	hle_w16(m, HLE_DOS_RESIDENT + RT_MATCHWORD, 0x4AFC);
	hle_w32(m, HLE_DOS_RESIDENT + RT_MATCHTAG, HLE_DOS_RESIDENT);
	hle_w32(m, HLE_DOS_RESIDENT + RT_ENDSKIP, HLE_DOS_RESIDENT + 0x1A);
	hle_w8(m, HLE_DOS_RESIDENT + RT_TYPE, NT_LIBRARY);
	hle_w32(m, HLE_DOS_RESIDENT + RT_NAME, s_dos);
	hle_w32(m, HLE_DOS_RESIDENT + RT_INIT, HLE_DOS_INIT);

	hle_jump_table(m, HLE_DOS_BASE, HLE_DOS_NEGSIZE, HLE_LIB_DOS);
	hle_w8(m, HLE_DOS_BASE + LN_TYPE, NT_LIBRARY);
	hle_w32(m, HLE_DOS_BASE + LN_NAME, s_dos);
	hle_w16(m, HLE_DOS_BASE + LIB_NEGSIZE, HLE_DOS_NEGSIZE);
	hle_w16(m, HLE_DOS_BASE + LIB_POSSIZE, DL_SIZEOF);
	hle_w16(m, HLE_DOS_BASE + LIB_VERSION, HLE_DOS_VERSION);
	hle_w16(m, HLE_DOS_BASE + LIB_REVISION, HLE_DOS_REVISION);
	hle_w32(m, HLE_DOS_BASE + DL_ROOT, HLE_DOS_ROOT);
	hle_w32(m, HLE_DOS_ROOT + RN_INFO, HLE_DOS_INFO >> 2);
}

/* The system clock: seconds since 1978-01-01 and microseconds, advancing one tick (1/50 s) a
 * frame from the date dos booted with. */
void hle_dos_time(struct Amiga *m, uint32_t *secs, uint32_t *micros)
{
	uint64_t t = m->hle.dos.clock_ticks + (m->frame_count - m->hle.dos.clock_frame);
	*secs = (uint32_t)(t / 50);
	*micros = (uint32_t)(t % 50) * 20000;
}

/* RT_INIT of the Resident, reached from the bootblock's return: mount the drives, make the
 * system assigns and the Initial CLI, and run S:Startup-Sequence. */
void hle_dos_boot(struct Amiga *m)
{
	struct HleDos *d = &m->hle.dos;
	uint32_t p, cli, con, ta, root;
	int i;
	int32_t s;
	hle_add_tail(m, HLE_EXEC_BASE + EB_LIBLIST, HLE_DOS_BASE);
	for(i = 0; i < AMIGA_DRIVES; i++)
	{
		char df[4] = {'D', 'F', (char)('0' + i), 0};
		uint32_t node = dos_node(m, DLT_DEVICE, df, 0), port = hle_alloc(m, MP_SIZEOF, MEMF_PUBLIC | MEMF_CLEAR, 0);
		if(!node || !port)
		{
			if(!m->fatal)
				amiga_fatal(m, "dos: no memory for DF%d's handler", i);
			return;
		}
		hle_w8(m, port + LN_TYPE, NT_MSGPORT);
		hle_w8(m, port + MP_FLAGS, PA_IGNORE);
		hle_new_list(m, port + MP_MSGLIST, NT_MESSAGE);
		hle_w32(m, node + DOL_TASK, port);
		d->handler[i] = port;
	}
	hle_dos_mount(m);
	if(m->fatal)
		return;
	if(!d->volume[0][0])
	{
		amiga_fatal(m, "dos: the disk in DF0 holds no OFS volume to boot from");
		return;
	}
	d->booted = 1;
	/* Kickstart 1.3 opens the Workbench screen for the Initial CLI's console window (the oracle's
	 * shots show its title bar once a game's own screen has gone). */
	d->workbench = 1;
	{
		/* No battery clock: the system clock starts at the boot volume's last-alteration date
		 * (docs/runtime.md, "dos.library"). */
		uint32_t days, mins, ticks;
		hle_ofs_volume_date(dos_image(m, 0), &days, &mins, &ticks);
		d->clock_ticks = ((uint64_t)days * 1440 + mins) * 3000 + ticks;
		d->clock_frame = m->frame_count;
	}
	for(i = 0; i < (int)(sizeof dos_system_assigns / sizeof dos_system_assigns[0]); i++)
	{
		const uint8_t *adf = dos_image(m, 0);
		int32_t at = dos_system_assigns[i][1][0] ? hle_ofs_lookup(adf, 880, dos_system_assigns[i][1]) : 880;
		struct HleOfsEntry e;
		if(at < 0 || hle_ofs_entry(adf, (uint32_t)at, &e) < 0 || e.type < 0)
			continue;
		strcpy(d->assigns[d->assign_count].name, dos_system_assigns[i][0]);
		d->assigns[d->assign_count].drive = 0;
		d->assigns[d->assign_count].block = (uint32_t)at;
		d->assign_count++;
		if(!dos_node(m, DLT_DIRECTORY, dos_system_assigns[i][0], dos_lock(m, 0, (uint32_t)at, -2)))
			return;
	}

	p = hle_alloc(m, PR_SIZEOF, MEMF_PUBLIC | MEMF_CLEAR, 0);
	cli = hle_alloc(m, CLI_SIZEOF + 0x40, MEMF_PUBLIC | MEMF_CLEAR, 0);
	con = hle_alloc(m, FH_SIZEOF, MEMF_PUBLIC | MEMF_CLEAR, 0);
	ta = hle_alloc(m, 8, MEMF_PUBLIC | MEMF_CLEAR, 0);
	if(!p || !cli || !con || !ta)
	{
		amiga_fatal(m, "dos: no memory for the Initial CLI");
		return;
	}
	d->process = p;
	d->cli = cli;
	hle_w8(m, p + LN_TYPE, NT_PROCESS);
	hle_w32(m, p + TC_SIGALLOC, 0x0000FFFFu);
	hle_w8(m, p + PR_MSGPORT + LN_TYPE, NT_MSGPORT);
	hle_w8(m, p + PR_MSGPORT + MP_SIGBIT, SIGB_DOS);
	hle_w32(m, p + PR_MSGPORT + MP_SIGTASK, p);
	hle_new_list(m, p + PR_MSGPORT + MP_MSGLIST, NT_MESSAGE);
	hle_w32(m, p + PR_TASKNUM, 1);
	root = dos_lock(m, 0, 880, -2);
	hle_w32(m, p + PR_CURRENTDIR, root);
	hle_w32(m, con + FH_INTERACTIVE, DOSTRUE);
	hle_w32(m, con + FH_ARG1, 1);
	d->files[0].used = 1;
	d->files[0].drive = -1;
	d->files[0].fh = con >> 2;
	hle_w32(m, p + PR_CIS, con >> 2);
	hle_w32(m, p + PR_COS, con >> 2);
	hle_w32(m, p + PR_CLI, cli >> 2);
	hle_w32(m, cli + CLI_DEFAULTSTACK, CLI_STACK_LONGS);
	hle_w32(m, cli + CLI_COMMANDNAME, (cli + CLI_SIZEOF) >> 2);
	hle_w32(m, cli + CLI_STANDARDINPUT, con >> 2);
	hle_w32(m, cli + CLI_CURRENTINPUT, con >> 2);
	hle_w32(m, cli + CLI_STANDARDOUTPUT, con >> 2);
	hle_w32(m, cli + CLI_CURRENTOUTPUT, con >> 2);
	hle_w32(m, ta, 1);
	hle_w32(m, ta + 4, p + PR_MSGPORT);
	hle_w32(m, HLE_DOS_ROOT + RN_TASKARRAY, ta >> 2);
	hle_w32(m, HLE_EXEC_BASE + EB_THISTASK, p);

	if(dos_resolve(m, 0, "S:Startup-Sequence", &i, (uint32_t *)&s) < 0)
	{
		if(!m->fatal)
			amiga_fatal(m, "CLI: DF0 has no S:Startup-Sequence");
		return;
	}
	d->script_drive = (int8_t)i;
	d->script_header = (uint32_t)s;
	d->script_pos = 0;
	amiga_logf(m, "dos: booted from DF0 (%s); running S:Startup-Sequence", d->volume[0]);
	dos_cli_next(m);
}

/* --- the library -------------------------------------------------------------------------- */

static void dos_examine(struct Amiga *m, int drive, uint32_t block, uint32_t fib)
{
	struct HleOfsEntry e;
	uint32_t i;
	if(hle_ofs_entry(dos_image(m, drive), block, &e) < 0)
	{
		amiga_fatal(m, "dos: Examine of block %u, which is not a header", block);
		return;
	}
	for(i = 0; i < 260; i += 4)
		hle_w32(m, fib + i, 0);
	hle_w32(m, fib + FIB_DISKKEY, block);
	hle_w32(m, fib + FIB_DIRENTRYTYPE, (uint32_t)e.type);
	hle_w32(m, fib + FIB_ENTRYTYPE, (uint32_t)e.type);
	hle_string(m, fib + FIB_FILENAME, e.name);
	hle_w32(m, fib + FIB_PROTECTION, e.protect);
	hle_w32(m, fib + FIB_SIZE, e.size);
	hle_w32(m, fib + FIB_NUMBLOCKS, e.blocks);
	hle_w32(m, fib + FIB_DATE, e.days);
	hle_w32(m, fib + FIB_DATE + 4, e.mins);
	hle_w32(m, fib + FIB_DATE + 8, e.ticks);
	hle_string(m, fib + FIB_COMMENT, e.comment);
}

static int dos_call(struct Amiga *m, uint32_t lvo)
{
	struct HleDos *d = &m->hle.dos;
	char path[DOS_PATH];
	switch(lvo)
	{
	case 30: /* Open(d1 name, d2 mode) */
		if(dos_cstr(m, D1, path, sizeof path) < 0)
			return 0;
		if(D2 == MODE_NEWFILE)
		{
			amiga_fatal(m, "dos: writing files is not implemented (Open(\"%s\", MODE_NEWFILE))", path);
			return 0;
		}
		if(D2 != MODE_OLDFILE && D2 != MODE_READWRITE)
		{
			amiga_fatal(m, "dos: Open(\"%s\") in mode %d is not implemented", path, (int32_t)D2);
			return 0;
		}
		if(!strcmp(path, "*"))
		{
			D0 = d->files[0].fh;
			return 0;
		}
#if AMIGA_A1200
		if(dos_upper(path[0]) == 'C' && dos_upper(path[1]) == 'O' && dos_upper(path[2]) == 'N' && path[3] == ':')
		{
			D0 = hle_con_open(m, path) ? dos_handle(m, DOS_CON, 0, 0) : 0;
			return 0;
		}
#endif
		D0 = dos_open(m, path, "Open");
		return 0;
	case 36: /* Close(d1 file) */
		dos_close(m, D1);
		D0 = DOSTRUE;
		return 0;
	case 42: /* Read(d1 file, d2 buffer, d3 length) */
	{
		struct HleDosFile *f = dos_file(m, D1);
		uint8_t *p;
		if(!f)
			return 0;
		if(f == &d->files[0])
		{
			amiga_fatal(m, "dos: reading the console is not implemented");
			return 0;
		}
#if AMIGA_A1200
		if(f->drive == DOS_CON)
		{
			int32_t n = hle_con_read(m, D2, D3);
			if(n >= 0)
			{
				D0 = (uint32_t)n;
				return 0;
			}
			m->cpu.pc = HLE_CON_PARK;
			return 1;
		}
#endif
		p = dos_buffer(m, D2, D3);
		if(!p && D3)
			return 0;
		D0 = dos_read(m, f, p, D3);
		return 0;
	}
	case 48: /* Write(d1 file, d2 buffer, d3 length): the console and NIL: only */
	{
		struct HleDosFile *f = dos_file(m, D1);
		char text[DOS_LINE];
		uint32_t i, n = D3 < sizeof text - 1 ? D3 : (uint32_t)sizeof text - 1;
		if(!f)
			return 0;
		if(f->drive == DOS_NIL)
		{
			D0 = D3;
			return 0;
		}
		if(f != &d->files[0] && f->drive != DOS_CON)
		{
			amiga_fatal(m, "dos: writing files is not implemented (Write)");
			return 0;
		}
		for(i = 0; i < n; i++)
			text[i] = (char)hle_r8(m, D2 + i);
		text[n] = 0;
		amiga_logf(m, "dos: console: %s", text);
#if AMIGA_A1200
		if(f->drive == DOS_CON)
			hle_con_write(m, D2, D3);
#endif
		D0 = D3;
		return 0;
	}
	case 54: /* Input */
		D0 = hle_r32(m, dos_process(m) + PR_CIS);
		return 0;
	case 60: /* Output */
		D0 = hle_r32(m, dos_process(m) + PR_COS);
		return 0;
	case 66: /* Seek(d1 file, d2 position, d3 mode): the old position */
	{
		struct HleDosFile *f = dos_file(m, D1);
		int64_t at;
		if(!f)
			return 0;
		if(f == &d->files[0] || f->drive == DOS_CON)
		{
			amiga_fatal(m, "dos: Seek on the console is not implemented");
			return 0;
		}
		at = (int32_t)D3 == OFFSET_BEGINNING ? 0 : (int32_t)D3 == OFFSET_CURRENT ? (int64_t)f->pos :
			(int32_t)D3 == OFFSET_END ? (int64_t)f->size : -1;
		at += (int32_t)D2;
		if((int32_t)D3 < OFFSET_BEGINNING || (int32_t)D3 > OFFSET_END || at < 0 || at > (int64_t)f->size)
		{
			dos_error(m, HLE_DOS_SEEK_ERROR);
			D0 = DOSTRUE;
			return 0;
		}
		D0 = f->pos;
		f->pos = (uint32_t)at;
		return 0;
	}
	case 84: /* Lock(d1 name, d2 mode) */
	{
		int drive;
		uint32_t block;
		if(dos_cstr(m, D1, path, sizeof path) < 0)
			return 0;
		D0 = dos_resolve(m, 0, path, &drive, &block) < 0 ? 0 : dos_lock(m, drive, block, (int32_t)D2);
		return 0;
	}
	case 90: /* UnLock(d1 lock) */
		if(D1)
			hle_free(m, D1 << 2, FL_SIZEOF);
		return 0;
	case 96: /* DupLock(d1 lock) */
	{
		int drive;
		uint32_t block;
		if(!D1)
		{
			D0 = 0;
			return 0;
		}
		if(dos_lock_place(m, D1, &drive, &block) < 0)
			return 0;
		D0 = dos_lock(m, drive, block, (int32_t)hle_r32(m, (D1 << 2) + FL_ACCESS));
		return 0;
	}
	case 102: /* Examine(d1 lock, d2 fileInfoBlock) */
	{
		int drive;
		uint32_t block;
		if(dos_lock_place(m, D1, &drive, &block) < 0)
			return 0;
		dos_examine(m, drive, block, D2);
		D0 = DOSTRUE;
		return 0;
	}
	case 126: /* CurrentDir(d1 lock): the old one */
		D0 = hle_r32(m, dos_process(m) + PR_CURRENTDIR);
		hle_w32(m, dos_process(m) + PR_CURRENTDIR, D1);
		return 0;
	case 132: /* IoErr */
		D0 = hle_r32(m, dos_process(m) + PR_RESULT2);
		return 0;
	case 150: /* LoadSeg(d1 name); with d1 = 0, an overlay node: d2 hunk table, d3 stream */
		if(!D1)
		{
			struct HleDosFile *f = dos_file(m, D3);
			struct HleHunkSource src = {dos_hunk_read, f};
			int32_t err;
			if(!f)
				return 0;
			D0 = hle_hunk_node(m, &src, D2 << 2, &err);
			if(!D0)
				dos_error(m, (uint32_t)err);
			return 0;
		}
		if(dos_cstr(m, D1, path, sizeof path) < 0)
			return 0;
		D0 = hle_dos_loadseg(m, path);
		return 0;
	case 156: /* UnLoadSeg(d1 seglist) */
		dos_unloadseg(m, D1);
		D0 = DOSTRUE;
		return 0;
	case 174: /* DeviceProc(d1 name): the MsgPort of the handler of DF0:-DF3:, or of the drive holding a volume */
	{
		char *colon;
		int drive = -1, i;
		uint32_t block;
		if(dos_cstr(m, D1, path, sizeof path) < 0)
			return 0;
		colon = strchr(path, ':');
		if(!colon)
		{
			amiga_fatal(m, "dos: DeviceProc(\"%s\") without a device name is not implemented", path);
			return 0;
		}
		*colon = 0;
		for(i = 0; i < d->assign_count; i++)
			if(dos_same(path, d->assigns[i].name))
			{
				amiga_fatal(m, "dos: DeviceProc(\"%s:\") of an assign is not implemented", path);
				return 0;
			}
		for(i = 0; i < AMIGA_DRIVES; i++)
		{
			char df[4] = {'D', 'F', (char)('0' + i), 0};
			if(dos_same(path, df))
				drive = i;
		}
		colon[0] = ':';
		colon[1] = 0;
		if(drive < 0 && dos_resolve(m, 0, path, &drive, &block) < 0)
		{
			D0 = 0;
			return 0;
		}
		D0 = d->handler[drive];
		return 0;
	}
	case 192: /* DateStamp(d1 ds): days since 1978-01-01, minutes past midnight, ticks past the minute */
	{
		uint64_t t = d->clock_ticks + (m->frame_count - d->clock_frame);
		hle_w32(m, D1, (uint32_t)(t / (3000 * 1440)));
		hle_w32(m, D1 + 4, (uint32_t)(t / 3000 % 1440));
		hle_w32(m, D1 + 8, (uint32_t)(t % 3000));
		D0 = D1;
		return 0;
	}
	case 198: /* Delay(d1 ticks): a tick is 1/50 s, one PAL frame */
		if((int32_t)D1 <= 0)
			return 0;
		d->delay_frame = m->frame_count + D1;
		m->cpu.pc = HLE_DOS_DELAY;
		return 1;
	case 216: /* IsInteractive(d1 file): the console and the CON: window */
	{
		struct HleDosFile *f = D1 == d->files[0].fh ? 0 : dos_file(m, D1);
		D0 = D1 == d->files[0].fh || (f && f->drive == DOS_CON) ? DOSTRUE : 0;
		return 0;
	}
	case 798: /* ReadArgs(d1 template, d2 array, d3 rdargs) */
		hle_dos_readargs(m, d->cmd_seglist ? d->cmd_args : 0);
		return 0;
	case 858: /* FreeArgs(d1 rdargs) */
		hle_dos_freeargs(m);
		return 0;
#if AMIGA_A1200
	case 600: /* GetProgramDir: pr_HomeDir, which the caller must not unlock */
		D0 = hle_r32(m, dos_process(m) + PR_HOMEDIR);
		return 0;
	/* Buffered I/O (dos autodoc, V36): no buffer is kept, each call reads or writes at once
	 * through Read and Write. */
	case 306: /* FGetC(d1 fh): the byte, or -1 at the end */
	case 336: /* FGets(d1 fh, d2 buf, d3 len): up to len - 1 bytes through the first newline, or 0 at the end */
	{
		struct HleDosFile *f = dos_file(m, D1);
		uint32_t n = 0, max = lvo == 306 ? 1 : (D3 ? D3 - 1 : 0);
		uint8_t c;
		if(!f)
			return 0;
		if(f == &d->files[0] || f->drive == DOS_CON)
		{
			amiga_fatal(m, "dos: %s on the console is not implemented", lvo == 306 ? "FGetC" : "FGets");
			return 0;
		}
		while(n < max && dos_read(m, f, &c, 1) == 1)
		{
			if(lvo == 336)
				hle_w8(m, D2 + n, c);
			n++;
			if(lvo == 306 || c == '\n')
				break;
		}
		if(lvo == 306)
			D0 = n ? c : 0xFFFFFFFFu;
		else
		{
			hle_w8(m, D2 + n, 0);
			D0 = n ? D2 : 0;
		}
		return 0;
	}
	case 318: /* UnGetC(d1 fh, d2 character): the last byte read is read again */
	{
		struct HleDosFile *f = dos_file(m, D1);
		if(!f)
			return 0;
		if(f->pos > 0)
			f->pos--;
		D0 = D2;
		return 0;
	}
	case 312: /* FPutC(d1 fh, d2 char): the character */
	case 342: /* FPuts(d1 fh, d2 str): 0 */
	case 330: /* FWrite(d1 fh, d2 block, d3 blocklen, d4 number): the blocks written */
	{
		uint32_t ch = D2, len = 1, buf = 0, number = m->cpu.d[4];
		int r;
		if(lvo == 312)
		{
			buf = hle_alloc(m, 1, MEMF_PUBLIC, 0);
			hle_w8(m, buf, (uint8_t)ch);
		}
		else if(lvo == 342)
			for(buf = D2, len = 0; hle_r8(m, D2 + len); len++)
				;
		else
		{
			buf = D2;
			len = D3 * number;
		}
		D2 = buf;
		D3 = len;
		r = dos_call(m, 48);
		if(lvo == 312)
			hle_free(m, buf, 1);
		D0 = lvo == 312 ? ch : lvo == 342 ? 0 : number;
		return r;
	}
	case 360: /* Flush(d1 fh): nothing is buffered */
		D0 = DOSTRUE;
		return 0;
	case 942: /* WriteChars(d1 buf, d2 buflen): to Output(), the count */
	case 948: /* PutStr(d1 str): to Output(), 0 */
	case 954: /* VPrintf(d1 format, d2 argarray): RawDoFmt's formatting to Output(), the count */
	{
		uint32_t len = D2, buf = D1, next, size = 0;
		int r;
		if(lvo == 948)
			for(len = 0; hle_r8(m, D1 + len); len++)
				;
		if(lvo == 954)
		{
			buf = hle_fmt(m, D1, D2, &size, &next);
			if(!buf)
				return 0;
			len = size - 1;
		}
		D2 = buf;
		D3 = len;
		D1 = hle_r32(m, dos_process(m) + PR_COS);
		r = dos_call(m, 48);
		if(size)
			hle_free(m, buf, size);
		D0 = lvo == 948 ? 0 : len;
		return r;
	}
	case 792: /* CheckSignal(d1 mask): those of the task's received signals, which are cleared */
	{
		uint32_t task = dos_process(m), got = hle_r32(m, task + TC_SIGRECVD) & D1;
		hle_w32(m, task + TC_SIGRECVD, hle_r32(m, task + TC_SIGRECVD) & ~got);
		D0 = got;
		return 0;
	}
#endif
#if AMIGA_A1200
	/* Pattern matching (dos autodoc, MatchFirst; dos/dosasl.i): a name without wildcards
	 * matches itself once. The AnchorPath gets one AChain, ap_Base and ap_Last, holding a lock
	 * on the directory the object is in and its FileInfoBlock, ap_Info the same, and with
	 * ap_Strlen the path in ap_Buf. A pattern with wildcards is fatal. */
	case 822: /* MatchFirst(d1 pattern, d2 anchorPath): 0, or the error */
	{
		struct HleOfsEntry e;
		uint32_t ap = D2, chain, i, n;
		int drive;
		uint32_t block;
		if(dos_cstr(m, D1, path, sizeof path) < 0)
			return 0;
		if(strpbrk(path, "#?*[]()|~%"))
		{
			amiga_fatal(m, "dos: MatchFirst(\"%s\"): wildcards are not implemented", path);
			return 0;
		}
		if(dos_resolve(m, 0, path, &drive, &block) < 0)
		{
			D0 = hle_r32(m, dos_process(m) + PR_RESULT2);
			return 0;
		}
		hle_ofs_entry(dos_image(m, drive), block, &e);
		chain = hle_alloc(m, AN_SIZEOF, MEMF_PUBLIC | MEMF_CLEAR, 0);
		if(!chain)
		{
			D0 = HLE_DOS_NO_FREE_STORE;
			return 0;
		}
		hle_w32(m, chain + AN_LOCK, dos_lock(m, drive, e.parent ? e.parent : block, SHARED_LOCK));
		dos_examine(m, drive, block, chain + AN_INFO);
		hle_w32(m, ap + AP_BASE, chain);
		hle_w32(m, ap + AP_LAST, chain);
		dos_examine(m, drive, block, ap + AP_INFO);
		n = hle_r16(m, ap + AP_STRLEN);
		for(i = 0; n && i < n - 1 && path[i]; i++)
			hle_w8(m, ap + AP_BUF + i, (uint8_t)path[i]);
		if(n)
			hle_w8(m, ap + AP_BUF + i, 0);
		D0 = 0;
		return 0;
	}
	case 828: /* MatchNext(d1 anchorPath): no other match */
		dos_error(m, HLE_DOS_NO_MORE_ENTRIES);
		D0 = HLE_DOS_NO_MORE_ENTRIES;
		return 0;
	case 834: /* MatchEnd(d1 anchorPath) */
	{
		uint32_t chain = hle_r32(m, D1 + AP_BASE);
		if(chain)
		{
			if(hle_r32(m, chain + AN_LOCK))
				hle_free(m, hle_r32(m, chain + AN_LOCK) << 2, FL_SIZEOF);
			hle_free(m, chain, AN_SIZEOF);
		}
		hle_w32(m, D1 + AP_BASE, 0);
		hle_w32(m, D1 + AP_LAST, 0);
		return 0;
	}
#endif
	default:
		return -1;
	}
}

#if AMIGA_A1200
#define DOS_REQUEST_SIZE 0x8C

/* The requester Kickstart 3.0's dos puts up for a volume no drive holds, through intuition's
 * EasyRequestArgs vector (LVO -588), after call `lvo` failed: Retry (1), or a disk inserted
 * (-1, for the IDCMP_DISKINSERTED it passes, as AROS's ErrorReport does), runs the call again;
 * Cancel (0) leaves it failed. The EasyStruct (intuition/intuition.i) and its strings are ours;
 * Kickstart's wording is not measured. The caller's A2 and A3, which a dos call preserves and
 * the call's arguments take, wait in the block until it returns. */
static int dos_request(struct Amiga *m, uint32_t lvo, const uint32_t *args)
{
	struct HleDos *d = &m->hle.dos;
	struct HleFrame f;
	uint32_t block = hle_alloc(m, DOS_REQUEST_SIZE, MEMF_PUBLIC | MEMF_CLEAR, 0), es = block;
	if(!block)
	{
		amiga_fatal(m, "dos: no memory for the requester for volume \"%s\"", d->request);
		return 0;
	}
	hle_w32(m, es, 20);
	hle_w32(m, es + 8, hle_string(m, block + 0x14, "System Request"));
	hle_w32(m, es + 12, hle_string(m, block + 0x24, "Please insert volume\n%s\nin any drive"));
	hle_w32(m, es + 16, hle_string(m, block + 0x4C, "Retry|Cancel"));
	hle_w32(m, block + 0x5C, hle_string(m, block + 0x60, d->request));
	hle_w32(m, block + 0x80, IDCMP_DISKINSERTED);
	hle_w32(m, block + 0x84, m->cpu.a[2]);
	hle_w32(m, block + 0x88, m->cpu.a[3]);
	amiga_logf(m, "dos: LVO -%u asks for volume \"%s\" through EasyRequestArgs", lvo, d->request);
	d->request[0] = 0;
	memset(&f, 0, sizeof f);
	f.kind = HleFrameDosRequest;
	f.node = lvo;
	memcpy(f.saved, args, 4 * sizeof args[0]);
	f.saved[4] = block;
	f.saved[5] = m->cpu.a[6];
	hle_call(m, &f, HLE_INT_BASE - 588);
	m->cpu.a[0] = hle_r32(m, dos_process(m) + PR_WINDOWPTR);
	m->cpu.a[1] = es;
	m->cpu.a[2] = block + 0x80;
	m->cpu.a[3] = block + 0x5C;
	m->cpu.a[6] = HLE_INT_BASE;
	return 1;
}

void hle_dos_request_done(struct Amiga *m, const struct HleFrame *f)
{
	m->cpu.a[2] = hle_r32(m, f->saved[4] + 0x84);
	m->cpu.a[3] = hle_r32(m, f->saved[4] + 0x88);
	hle_free(m, f->saved[4], DOS_REQUEST_SIZE);
	m->cpu.a[6] = f->saved[5];
	if(D0)
	{
		int r;
		D1 = f->saved[0];
		D2 = f->saved[1];
		D3 = f->saved[2];
		m->cpu.d[4] = f->saved[3];
		r = hle_dos_call(m, f->node);
		if(r == 0)
			m->cpu.pc = cpu_pop32(m);
		return;
	}
	dos_error(m, HLE_DOS_DEVICE_NOT_MOUNTED);
	D0 = D1 = 0;
	m->cpu.pc = cpu_pop32(m);
}
#endif

int hle_dos_call(struct Amiga *m, uint32_t lvo)
{
	const uint32_t args[4] = {D1, D2, D3, m->cpu.d[4]};
	int r;
#if AMIGA_A1200
	m->hle.dos.request[0] = 0;
#endif
	r = dos_call(m, lvo);
#if AMIGA_A1200
	if(r == 0 && m->hle.dos.request[0] && !m->fatal)
		return dos_request(m, lvo, args);
#else
	(void)args;
#endif
	if(r == 0)
		D1 = D0;
	return r;
}
