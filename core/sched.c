#include "amiga.h"

/* The beam is derived from the CPU cycle counter: 454 cycles a line, 313 lines a frame. */

int sched_line(const struct Amiga *m, uint64_t t)
{
	(void)m;
	return (int)((t / AMIGA_CYCLES_PER_LINE) % AMIGA_LINES);
}

int sched_hpos(const struct Amiga *m, uint64_t t)
{
	(void)m;
	return (int)((t % AMIGA_CYCLES_PER_LINE) / 2);
}

static void sched_line_start(struct Amiga *m, int line)
{
	m->copper.hpos = 0;
	if(line == 0)
	{
		custom_set_intreq(m, 0x0020);
		copper_restart(m, 1);
		cia_tod_tick(m, &m->cia_a);
		cia_frame(m);
		m->denise.frame_done = 1;
		m->frame_count++;
	}
	denise_begin_line(m, line);
}

static void sched_line_end(struct Amiga *m)
{
	denise_end_line(m);
	cia_tod_tick(m, &m->cia_b);
}

void sched_catch_up(struct Amiga *m, uint64_t target)
{
	while(m->done < target && !m->fatal)
	{
		uint64_t line_start = m->done - m->done % AMIGA_CYCLES_PER_LINE;
		uint64_t line_end = line_start + AMIGA_CYCLES_PER_LINE;
		uint64_t stop = target < line_end ? target : line_end;
		int line = (int)((line_start / AMIGA_CYCLES_PER_LINE) % AMIGA_LINES);
		int h_to = (int)((stop - line_start) / 2);

		if(m->done == line_start)
			sched_line_start(m, line);
		copper_run(m, line, h_to);
		denise_render_to(m, h_to);
		cia_run(m, (uint32_t)(stop - m->done));
		m->paula.audio_cck_frac += stop - m->done;
		paula_audio_run(m, (int)(m->paula.audio_cck_frac / 2));
		m->paula.audio_cck_frac %= 2;
		m->done = stop;
		if(stop == line_end)
			sched_line_end(m);
	}
}
