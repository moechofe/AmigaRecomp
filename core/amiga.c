#include "amiga.h"
#include "game.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void amiga_init(struct Amiga *m)
{
	int n;
	memset(m, 0, sizeof *m);
	m->sprites_enabled = 1;
	for(n = 0; n < AMIGA_DRIVES; n++)
		m->drive_role[n] = -1;
}

void amiga_attach(struct Amiga *m, int role, uint8_t *image, int writable)
{
	if(role < 0 || role >= AMIGA_ROLES)
	{
		amiga_fatal(m, "role %d: the runtime holds %d roles", role, AMIGA_ROLES);
		return;
	}
	m->disks[role].image = image;
	m->disks[role].rw = writable ? image : NULL;
	m->disks[role].dirty = 0;
}

/* zlib's CRC-32 (RFC 1952): reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF. */
uint32_t amiga_crc32(const uint8_t *p, uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFu, i;
	int bit;
	for(i = 0; i < len; i++)
	{
		crc ^= p[i];
		for(bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

void amiga_fatal(struct Amiga *m, const char *fmt, ...)
{
	va_list ap;
	if(m->fatal)
		return;
	va_start(ap, fmt);
	vsnprintf(m->fatal_msg, sizeof m->fatal_msg, fmt, ap);
	va_end(ap);
	m->fatal = 1;
}

void amiga_logf(struct Amiga *m, const char *fmt, ...)
{
	char text[AMIGA_FATAL_LEN];
	va_list ap;
	if(!m->log)
		return;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	m->log(text);
}

void amiga_log_once(struct Amiga *m, uint32_t key, const char *fmt, ...)
{
	char text[AMIGA_FATAL_LEN];
	va_list ap;
	int i;
	for(i = 0; i < m->logged_count; i++)
		if(m->logged[i] == key)
			return;
	if(m->logged_count < AMIGA_LOG_ONCE)
		m->logged[m->logged_count++] = key;
	if(!m->log)
		return;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	m->log(text);
}

void amiga_key(struct Amiga *m, uint8_t code, int up)
{
	int next = (m->key_tail + 1) % AMIGA_KEY_QUEUE;
	if(next == m->key_head)
		return;
	m->keys[m->key_tail] = (uint8_t)((code & 0x7F) | (up ? 0x80 : 0));
	m->key_tail = next;
}

/* JOY0DAT's two 8-bit counters follow the mouse; the buttons are CIA-A PRA bit 6 (left) and
 * POTGOR bit 10 (right). */
void amiga_mouse(struct Amiga *m, int dx, int dy, int lmb, int rmb)
{
	m->paula.mouse_x = (int16_t)((m->paula.mouse_x + dx) & 0xFF);
	m->paula.mouse_y = (int16_t)((m->paula.mouse_y + dy) & 0xFF);
	m->paula.lmb = (uint8_t)(lmb != 0);
	m->paula.rmb = (uint8_t)(rmb != 0);
}
