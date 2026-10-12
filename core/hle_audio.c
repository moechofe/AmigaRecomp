#include "hle.h"
#include "cpu_ops.h"

/* audio.device: channel allocation and CMD_WRITE on Paula's four DMA channels, each command
 * from its autodoc (audio.doc) and RKM Devices, "Audio Device"; IOAudio from NDK 3.2
 * devices/audio.i. A write is programmed through the AUDx registers as a CPU write would, and
 * the channel's audio interrupt (one each time Paula reloads the channel's location) counts the
 * cycles it has played. */

#define IOA_ALLOCKEY 0x20
#define IOA_DATA 0x22
#define IOA_LENGTH 0x26
#define IOA_PERIOD 0x2A
#define IOA_VOLUME 0x2C
#define IOA_CYCLES 0x2E
#define IOA_WRITEMSG 0x30

#define CMD_RESET 1
#define CMD_WRITE 3
#define CMD_FLUSH 8
#define ADCMD_FREE 9
#define ADCMD_FINISH 11
#define ADCMD_PERVOL 12
#define ADCMD_ALLOCATE 32

#define ADIOF_PERVOL 0x10
#define ADIOF_SYNCCYCLE 0x20
#define ADIOF_NOWAIT 0x40
#define ADIOF_WRITEMESSAGE 0x80
#define IOERR_ABORTED -2
#define ADIOERR_NOALLOCATION -10
#define ADIOERR_ALLOCFAILED -11

static void aud_reg(struct Amiga *m, int ch, uint32_t reg, uint16_t v)
{
	hle_w16(m, 0xDFF0A0 + 16 * (uint32_t)ch + reg, v);
}

static void aud_done(struct Amiga *m, uint32_t io, int8_t error)
{
	hle_w8(m, io + IO_ERROR, (uint8_t)error);
	hle_reply_msg(m, io);
}

/* The write at the head of the channel's queue starts: its location and length, and its period
 * and volume with ADIOF_PERVOL. Turning the channel's DMA on reloads it at once, which raises
 * the interrupt that counts the first cycle. */
static void aud_start(struct Amiga *m, int ch)
{
	struct HleAudioChannel *c = &m->hle.audio.ch[ch];
	uint32_t io = c->queue[0], data = hle_r32(m, io + IOA_DATA), msg = io + IOA_WRITEMSG;
	uint8_t flags = hle_r8(m, io + IO_FLAGS);
	if(flags & ADIOF_PERVOL)
	{
		aud_reg(m, ch, 0x6, hle_r16(m, io + IOA_PERIOD));
		aud_reg(m, ch, 0x8, hle_r16(m, io + IOA_VOLUME));
	}
	aud_reg(m, ch, 0x0, (uint16_t)(data >> 16));
	aud_reg(m, ch, 0x2, (uint16_t)data);
	aud_reg(m, ch, 0x4, (uint16_t)(hle_r32(m, io + IOA_LENGTH) / 2));
	c->plays = 0;
	c->preloaded = 0;
	c->finishing = 0;
	hle_w16(m, 0xDFF09A, (uint16_t)(0x8000 | (0x80 << ch)));
	hle_w16(m, 0xDFF096, (uint16_t)(0x8200 | (1 << ch)));
	if((flags & ADIOF_WRITEMESSAGE) && hle_r32(m, msg + MN_REPLYPORT))
		hle_reply_msg(m, msg);
}

static void aud_stop(struct Amiga *m, int ch)
{
	hle_w16(m, 0xDFF096, (uint16_t)(1 << ch));
	hle_w16(m, 0xDFF09A, (uint16_t)(0x80 << ch));
	hle_w16(m, 0xDFF09C, (uint16_t)(0x80 << ch));
}

static void aud_pop(struct Amiga *m, int ch, int8_t error)
{
	struct HleAudioChannel *c = &m->hle.audio.ch[ch];
	uint32_t io = c->queue[0];
	int i;
	for(i = 1; i < c->count; i++)
		c->queue[i - 1] = c->queue[i];
	c->count--;
	aud_done(m, io, error);
}

/* Every write of the channel ends with IOERR_ABORTED and the channel stops (CMD_FLUSH). */
static void aud_flush(struct Amiga *m, int ch)
{
	aud_stop(m, ch);
	while(m->hle.audio.ch[ch].count)
		aud_pop(m, ch, IOERR_ABORTED);
}

/* The channel's audio interrupt: Paula has just reloaded its location, so play number `plays`
 * begins. When the last cycle of the current write begins, the next write's location is
 * loaded, to play straight after it; when the cycle after the last begins, the write is done. */
void hle_audio_interrupt(struct Amiga *m, int ch)
{
	struct HleAudioChannel *c = &m->hle.audio.ch[ch];
	uint32_t io, cycles;
	hle_w16(m, 0xDFF09C, (uint16_t)(0x80 << ch));
	if(!c->count)
	{
		aud_stop(m, ch);
		return;
	}
	io = c->queue[0];
	cycles = hle_r16(m, io + IOA_CYCLES);
	c->plays++;
	if(c->finishing)
		cycles = c->plays - 1;
	if(cycles && c->plays == cycles && c->count > 1 && !(hle_r8(m, c->queue[1] + IO_FLAGS) & ADIOF_PERVOL))
	{
		uint32_t next = hle_r32(m, c->queue[1] + IOA_DATA);
		aud_reg(m, ch, 0x0, (uint16_t)(next >> 16));
		aud_reg(m, ch, 0x2, (uint16_t)next);
		aud_reg(m, ch, 0x4, (uint16_t)(hle_r32(m, c->queue[1] + IOA_LENGTH) / 2));
		c->preloaded = 1;
	}
	if(!cycles || c->plays <= cycles)
		return;
	if(c->preloaded)
	{
		aud_pop(m, ch, 0);
		c->plays = 1;
		c->preloaded = 0;
		c->finishing = 0;
		return;
	}
	aud_stop(m, ch);
	aud_pop(m, ch, 0);
	if(c->count)
		aud_start(m, ch);
}

static int aud_key_ok(struct Amiga *m, int ch, uint32_t io)
{
	struct HleAudioChannel *c = &m->hle.audio.ch[ch];
	return c->allocated && c->key == hle_r16(m, io + IOA_ALLOCKEY);
}

/* ADCMD_ALLOCATE: the first combination whose channels are all free. Stealing a channel and
 * waiting for one are not implemented. */
static int8_t aud_allocate(struct Amiga *m, uint32_t io)
{
	struct HleAudio *a = &m->hle.audio;
	uint32_t data = hle_r32(m, io + IOA_DATA), len = hle_r32(m, io + IOA_LENGTH), i;
	uint16_t key = hle_r16(m, io + IOA_ALLOCKEY);
	int ch;
	if(!key)
	{
		key = ++a->next_key;
		hle_w16(m, io + IOA_ALLOCKEY, key);
	}
	if(!len)
	{
		hle_w32(m, io + IO_UNIT, 0);
		return 0;
	}
	for(i = 0; i < len; i++)
	{
		uint8_t want = hle_r8(m, data + i) & 15;
		int busy = 0;
		for(ch = 0; ch < 4; ch++)
			if((want & (1 << ch)) && a->ch[ch].allocated)
				busy = 1;
		if(busy)
			continue;
		for(ch = 0; ch < 4; ch++)
		{
			if(!(want & (1 << ch)))
				continue;
			aud_flush(m, ch);
			a->ch[ch].allocated = 1;
			a->ch[ch].key = key;
			a->ch[ch].prec = (int8_t)hle_r8(m, io + LN_PRI);
			hle_w32(m, HLE_EXEC_BASE + EB_INTVECTS + (uint32_t)(7 + ch) * IV_SIZE + IV_DATA, (uint32_t)ch);
			hle_w32(m, HLE_EXEC_BASE + EB_INTVECTS + (uint32_t)(7 + ch) * IV_SIZE + IV_CODE, HLE_AUDIO_INT);
		}
		hle_w32(m, io + IO_UNIT, want);
		return 0;
	}
	if(!(hle_r8(m, io + IO_FLAGS) & ADIOF_NOWAIT))
	{
		amiga_fatal(m, "audio.device: ADCMD_ALLOCATE would wait or steal a channel; not implemented");
		return 0;
	}
	hle_w32(m, io + IO_UNIT, 0);
	return ADIOERR_ALLOCFAILED;
}

/* OpenDevice("audio.device"): with a non-zero ioa_Length it allocates as ADCMD_ALLOCATE does. */
int8_t hle_audio_open(struct Amiga *m, uint32_t io)
{
	int8_t error;
	hle_w32(m, io + IO_DEVICE, HLE_AUDIO_BASE);
	error = aud_allocate(m, io);
	if(error)
		hle_w32(m, io + IO_DEVICE, 0xFFFFFFFFu);
	else
		hle_w16(m, HLE_AUDIO_BASE + LIB_OPENCNT, (uint16_t)(hle_r16(m, HLE_AUDIO_BASE + LIB_OPENCNT) + 1));
	hle_w8(m, io + IO_ERROR, (uint8_t)error);
	return error;
}

/* BeginIO. Returns 1 when the request is complete, 0 when it is queued and replied later. */
int hle_audio_io(struct Amiga *m, uint32_t io)
{
	uint16_t cmd = hle_r16(m, io + IO_COMMAND);
	uint32_t unit = hle_r32(m, io + IO_UNIT) & 15, done = 0;
	int ch;
	hle_w8(m, io + IO_ERROR, 0);
	switch(cmd)
	{
	case ADCMD_ALLOCATE:
		hle_w8(m, io + IO_ERROR, (uint8_t)aud_allocate(m, io));
		return 1;
	case CMD_WRITE:
		for(ch = 0; ch < 4 && !(unit & (1u << ch)); ch++)
			;
		if(ch == 4 || !aud_key_ok(m, ch, io))
		{
			hle_w8(m, io + IO_ERROR, (uint8_t)ADIOERR_NOALLOCATION);
			return 1;
		}
		if(m->hle.audio.ch[ch].count == HLE_AUDIO_QUEUE)
		{
			amiga_fatal(m, "audio.device: more than %d writes queued on channel %d", HLE_AUDIO_QUEUE, ch);
			return 1;
		}
		hle_w32(m, io + IO_UNIT, 1u << ch);
		hle_w8(m, io + IO_FLAGS, (uint8_t)(hle_r8(m, io + IO_FLAGS) & ~IOF_QUICK));
		hle_w8(m, io + LN_TYPE, NT_MESSAGE);
		m->hle.audio.ch[ch].queue[m->hle.audio.ch[ch].count++] = io;
		if(m->hle.audio.ch[ch].count == 1)
			aud_start(m, ch);
		return 0;
	case CMD_RESET:
	case CMD_FLUSH:
	case ADCMD_FREE:
	case ADCMD_FINISH:
	case ADCMD_PERVOL:
		for(ch = 0; ch < 4; ch++)
		{
			struct HleAudioChannel *c = &m->hle.audio.ch[ch];
			if(!(unit & (1u << ch)))
				continue;
			if(!aud_key_ok(m, ch, io))
			{
				hle_w8(m, io + IO_ERROR, (uint8_t)ADIOERR_NOALLOCATION);
				continue;
			}
			done |= 1u << ch;
			if(cmd == ADCMD_PERVOL)
			{
				if(hle_r8(m, io + IO_FLAGS) & ADIOF_SYNCCYCLE)
				{
					amiga_fatal(m, "audio.device: ADCMD_PERVOL with ADIOF_SYNCCYCLE is not implemented");
					return 1;
				}
				if(c->count)
				{
					aud_reg(m, ch, 0x6, hle_r16(m, io + IOA_PERIOD));
					aud_reg(m, ch, 0x8, hle_r16(m, io + IOA_VOLUME));
				}
			}
			else if(cmd == ADCMD_FINISH)
			{
				if(!c->count)
					continue;
				if(hle_r8(m, io + IO_FLAGS) & ADIOF_SYNCCYCLE)
					c->finishing = 1;
				else
				{
					aud_stop(m, ch);
					aud_pop(m, ch, IOERR_ABORTED);
					if(c->count)
						aud_start(m, ch);
				}
			}
			else
			{
				aud_flush(m, ch);
				if(cmd != CMD_FLUSH)
				{
					aud_reg(m, ch, 0x6, 0);
					aud_reg(m, ch, 0x8, 0);
				}
				if(cmd == ADCMD_FREE)
				{
					c->allocated = 0;
					c->key = 0;
				}
			}
		}
		hle_w32(m, io + IO_UNIT, done);
		return 1;
	default:
		amiga_fatal(m, "audio.device command %u is not implemented", cmd);
		return 1;
	}
}

/* AbortIO: a queued or playing write ends with IOERR_ABORTED. */
void hle_audio_abort(struct Amiga *m, uint32_t io)
{
	int ch, i;
	for(ch = 0; ch < 4; ch++)
	{
		struct HleAudioChannel *c = &m->hle.audio.ch[ch];
		for(i = 0; i < c->count; i++)
		{
			if(c->queue[i] != io)
				continue;
			if(i == 0)
			{
				aud_stop(m, ch);
				aud_pop(m, ch, IOERR_ABORTED);
				if(c->count)
					aud_start(m, ch);
				return;
			}
			for(; i + 1 < c->count; i++)
				c->queue[i] = c->queue[i + 1];
			c->count--;
			aud_done(m, io, IOERR_ABORTED);
			return;
		}
	}
}

void hle_audio_build(struct Amiga *m)
{
	hle_jump_table(m, HLE_AUDIO_BASE, HLE_AUDIO_NEGSIZE, HLE_LIB_AUDIO);
	hle_w8(m, HLE_AUDIO_BASE + LN_TYPE, NT_DEVICE);
	hle_w32(m, HLE_AUDIO_BASE + LN_NAME, hle_string(m, HLE_STRINGS2, "audio.device"));
	hle_w16(m, HLE_AUDIO_BASE + LIB_NEGSIZE, HLE_AUDIO_NEGSIZE);
	hle_w16(m, HLE_AUDIO_BASE + LIB_POSSIZE, LIB_SIZE);
	hle_w16(m, HLE_AUDIO_BASE + LIB_VERSION, 34);
	hle_add_tail(m, HLE_EXEC_BASE + EB_DEVICELIST, HLE_AUDIO_BASE);
}

/* The device's own vectors: BeginIO -30, AbortIO -36 (exec/devices.i). */
int hle_audio_call(struct Amiga *m, uint32_t lvo)
{
	uint32_t io = m->cpu.a[1];
	switch(lvo)
	{
	case 30:
		if(hle_audio_io(m, io) && !(hle_r8(m, io + IO_FLAGS) & IOF_QUICK))
			hle_reply_msg(m, io);
		return 0;
	case 36:
		hle_audio_abort(m, io);
		m->cpu.d[0] = 0;
		return 0;
	default:
		return -1;
	}
}
