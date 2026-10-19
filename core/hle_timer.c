#include "hle.h"

/* timer.device, on the A1200 only (timer.doc and devices/timer.i, ADCD 2.1). Time is the
 * machine's: one E clock tick per 10 time-base cycles (docs/runtime.md, "Timing"), so 709 379
 * a second, and a microsecond is 7.09379 cycles. A TR_ADDREQUEST is replied at the first
 * vertical blank at or after its time [INFERENCE: Kickstart's timer.device replies a
 * UNIT_MICROHZ request from a CIA timer interrupt at its time; a vertical blank is up to 20 ms
 * late]. The system time is dos's clock (hle_dos_time). */

#if AMIGA_A1200

#define D0 m->cpu.d[0]
#define A0 m->cpu.a[0]
#define A1 m->cpu.a[1]

#define TR_ADDREQUEST 9
#define TR_GETSYSTIME 10
#define TR_SECS 0x20
#define TR_MICRO 0x24
#define UNIT_MICROHZ 0
#define UNIT_VBLANK 1
#define UNIT_ECLOCK 2
#define UNIT_WAITUNTIL 3
#define UNIT_WAITECLOCK 4
#define IOERR_ABORTED -2
#define TIMER_ECLOCK_HZ 709379u

static uint64_t timer_cycles_of_micros(uint64_t micros)
{
	return micros * 709379u / 100000u;
}

void hle_timer_build(struct Amiga *m)
{
	hle_jump_table(m, HLE_TIMER_BASE, 0x44, HLE_LIB_TIMER);
	hle_w8(m, HLE_TIMER_BASE + LN_TYPE, NT_DEVICE);
	hle_w32(m, HLE_TIMER_BASE + LN_NAME, hle_string(m, HLE_RES_STRINGS + 0x60, "timer.device"));
	hle_w16(m, HLE_TIMER_BASE + LIB_NEGSIZE, 0x44);
	hle_w16(m, HLE_TIMER_BASE + LIB_POSSIZE, LIB_SIZE);
	hle_w16(m, HLE_TIMER_BASE + LIB_VERSION, 39);
	hle_w16(m, HLE_TIMER_BASE + LIB_REVISION, 4);
	hle_add_tail(m, HLE_EXEC_BASE + EB_DEVICELIST, HLE_TIMER_BASE);
}

int hle_timer_open(struct Amiga *m, uint32_t unit)
{
	(void)m;
	return unit <= UNIT_WAITECLOCK;
}

static void timer_systime(struct Amiga *m, uint32_t tv)
{
	uint32_t secs, micros;
	hle_dos_time(m, &secs, &micros);
	hle_w32(m, tv, secs);
	hle_w32(m, tv + 4, micros);
}

/* The request's due time in time-base cycles, from its unit and its timeval or EClockVal. */
static uint64_t timer_due(struct Amiga *m, uint32_t io)
{
	uint32_t unit = hle_r32(m, io + IO_UNIT), hi = hle_r32(m, io + TR_SECS), lo = hle_r32(m, io + TR_MICRO);
	uint64_t now = amiga_now(m);
	switch(unit)
	{
	case UNIT_MICROHZ:
	case UNIT_VBLANK:
		return now + timer_cycles_of_micros((uint64_t)hi * 1000000u + lo);
	case UNIT_ECLOCK:
		return now + (((uint64_t)hi << 32) | lo) * 10u;
	case UNIT_WAITECLOCK:
	{
		uint64_t at = (((uint64_t)hi << 32) | lo) * 10u;
		return at > now ? at : now;
	}
	default:
	{
		uint32_t secs, micros;
		uint64_t want = (uint64_t)hi * 1000000u + lo, have;
		hle_dos_time(m, &secs, &micros);
		have = (uint64_t)secs * 1000000u + micros;
		return now + (want > have ? timer_cycles_of_micros(want - have) : 0);
	}
	}
}

/* BeginIO: 1 when the request is done now, 0 when it is queued to be replied later. */
int hle_timer_io(struct Amiga *m, uint32_t io)
{
	struct Hle *h = &m->hle;
	hle_w8(m, io + IO_ERROR, 0);
	switch(hle_r16(m, io + IO_COMMAND))
	{
	case TR_ADDREQUEST:
		if(h->timer_count == HLE_TIMER_REQUESTS)
		{
			amiga_fatal(m, "timer.device: more than %d requests are queued", HLE_TIMER_REQUESTS);
			return 1;
		}
		h->timer_io[h->timer_count] = io;
		h->timer_due[h->timer_count] = timer_due(m, io);
		h->timer_count++;
		hle_w8(m, io + IO_FLAGS, (uint8_t)(hle_r8(m, io + IO_FLAGS) & ~IOF_QUICK));
		hle_w8(m, io + LN_TYPE, NT_MESSAGE);
		return 0;
	case TR_GETSYSTIME:
		timer_systime(m, io + TR_SECS);
		return 1;
	default:
		amiga_fatal(m, "timer.device command %u is not implemented", hle_r16(m, io + IO_COMMAND));
		return 1;
	}
}

static void timer_drop(struct Amiga *m, int i)
{
	struct Hle *h = &m->hle;
	h->timer_count--;
	h->timer_io[i] = h->timer_io[h->timer_count];
	h->timer_due[i] = h->timer_due[h->timer_count];
}

void hle_timer_abort(struct Amiga *m, uint32_t io)
{
	int i;
	for(i = 0; i < m->hle.timer_count; i++)
		if(m->hle.timer_io[i] == io)
		{
			timer_drop(m, i);
			hle_w8(m, io + IO_ERROR, (uint8_t)IOERR_ABORTED);
			hle_reply_msg(m, io);
			return;
		}
}

/* At each vertical blank: reply every request whose time has come. */
void hle_timer_vblank(struct Amiga *m)
{
	uint64_t now = amiga_now(m);
	int i = 0;
	while(i < m->hle.timer_count)
	{
		if(m->hle.timer_due[i] <= now)
		{
			uint32_t io = m->hle.timer_io[i];
			timer_drop(m, i);
			hle_reply_msg(m, io);
			continue;
		}
		i++;
	}
}

static int32_t timer_cmp(struct Amiga *m, uint32_t a, uint32_t b)
{
	uint32_t as = hle_r32(m, a), bs = hle_r32(m, b), am = hle_r32(m, a + 4), bm = hle_r32(m, b + 4);
	if(as != bs)
		return as > bs ? -1 : 1;
	if(am != bm)
		return am > bm ? -1 : 1;
	return 0;
}

/* The device's own functions, by negative LVO from devices/timer.i. */
int hle_timer_call(struct Amiga *m, uint32_t lvo)
{
	switch(lvo)
	{
	case 42: /* AddTime(a0 dest, a1 src) */
	case 48: /* SubTime(a0 dest, a1 src) */
	{
		int64_t d = (int64_t)hle_r32(m, A0) * 1000000 + hle_r32(m, A0 + 4);
		int64_t s = (int64_t)hle_r32(m, A1) * 1000000 + hle_r32(m, A1 + 4);
		d = lvo == 42 ? d + s : d - s;
		hle_w32(m, A0, (uint32_t)(d / 1000000));
		hle_w32(m, A0 + 4, (uint32_t)(d % 1000000));
		return 0;
	}
	case 54: /* CmpTime(a0 dest, a1 src): 0 equal, -1 dest more, 1 dest less */
		D0 = (uint32_t)timer_cmp(m, A0, A1);
		return 0;
	case 60: /* ReadEClock(a0 dest): the E clock's count, and its frequency in D0 */
	{
		uint64_t e = amiga_now(m) / 10u;
		hle_w32(m, A0, (uint32_t)(e >> 32));
		hle_w32(m, A0 + 4, (uint32_t)e);
		D0 = TIMER_ECLOCK_HZ;
		return 0;
	}
	case 66: /* GetSysTime(a0 dest) */
		timer_systime(m, A0);
		return 0;
	default:
		return -1;
	}
}

#else

void hle_timer_vblank(struct Amiga *m)
{
	(void)m;
}

#endif
