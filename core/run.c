#include "amiga.h"
#include "cpu_ops.h"
#include "game.h"

/* One frame: interrupts first, then the HLE, then the CPU, with the chipset caught up to the
 * CPU after every step. Returns nonzero on a fatal error; the message is in fatal_msg.
 *
 * An interrupt is taken at an instruction boundary only if it was already pending when the
 * last instruction began (Cpu.ipl_prev): the 68000 latches the IPL before an instruction's
 * final prefetch (docs/runtime.md, "Timing"). A CPU parked in the HLE region or on STOP is between instructions and takes
 * it at once. */
int amiga_run_frame(struct Amiga *m)
{
	uint32_t target = m->frame_count + 1;
	while(m->frame_count < target && !m->fatal)
	{
		uint64_t line_end = (m->cycles / AMIGA_CYCLES_PER_LINE + 1) * AMIGA_CYCLES_PER_LINE;
		uint32_t pc;
		int ipl, mask, parked;
		sched_catch_up(m, m->cycles);
		ipl = custom_ipl(m);
		mask = (m->cpu.sr >> 8) & 7;
		pc = m->cpu.pc & 0xFFFFFFu;
		parked = (pc >= HLE_BASE && pc < HLE_END) || m->cpu.stopped;
		if(ipl > mask && (parked || m->cpu.ipl_prev > mask))
		{
			cpu_interrupt(m, ipl);
			m->cpu.ipl_prev = (uint8_t)ipl;
			pc = m->cpu.pc & 0xFFFFFFu;
		}
		if(pc >= HLE_BASE && pc < HLE_END)
		{
			m->cpu.ipl_prev = (uint8_t)ipl;
			if(hle_dispatch(m))
				m->cycles = line_end;
			continue;
		}
		if(m->cpu.stopped)
		{
			m->cycles = line_end;
			continue;
		}
		if(game_hook_at(m, pc) && game_hook_run(m, pc))
		{
			m->cpu.ipl_prev = (uint8_t)ipl;
			continue;
		}
		interp_run(m, 1000, line_end);
	}
	sched_catch_up(m, m->cycles);
	if(!m->fatal)
		game_hacks_frame(m);
	return m->fatal;
}
