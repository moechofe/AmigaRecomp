#include "amiga.h"
#include "game.h"

/* Custom chip register file, offsets from 0xDFF000 per HRM appendix A. */

#define REG_DMACONR 0x002
#define REG_VPOSR 0x004
#define REG_VHPOSR 0x006
#define REG_JOY0DAT 0x00A
#define REG_JOY1DAT 0x00C
#define REG_CLXDAT 0x00E
#define REG_ADKCONR 0x010
#define REG_POT0DAT 0x012
#define REG_POT1DAT 0x014
#define REG_POTGOR 0x016
#define REG_SERDATR 0x018
#define REG_DSKBYTR 0x01A
#define REG_INTENAR 0x01C
#define REG_INTREQR 0x01E
#define REG_DSKLEN 0x024
#define REG_COPCON 0x02E
#define REG_POTGO 0x034
#define REG_BLTCON0 0x040
#define REG_BLTCON1 0x042
#define REG_BLTAFWM 0x044
#define REG_BLTALWM 0x046
#define REG_BLTCPTH 0x048
#define REG_BLTCPTL 0x04A
#define REG_BLTBPTH 0x04C
#define REG_BLTBPTL 0x04E
#define REG_BLTAPTH 0x050
#define REG_BLTAPTL 0x052
#define REG_BLTDPTH 0x054
#define REG_BLTDPTL 0x056
#define REG_BLTSIZE 0x058
#define REG_BLTCMOD 0x060
#define REG_BLTBMOD 0x062
#define REG_BLTAMOD 0x064
#define REG_BLTDMOD 0x066
#define REG_BLTCDAT 0x070
#define REG_BLTBDAT 0x072
#define REG_BLTADAT 0x074
#define REG_DENISEID 0x07C
#define REG_COP1LCH 0x080
#define REG_COP1LCL 0x082
#define REG_COP2LCH 0x084
#define REG_COP2LCL 0x086
#define REG_COPJMP1 0x088
#define REG_COPJMP2 0x08A
#define REG_DIWSTRT 0x08E
#define REG_DMACON 0x096
#define REG_INTENA 0x09A
#define REG_INTREQ 0x09C
#define REG_ADKCON 0x09E
#define REG_AUD0LCH 0x0A0
#define REG_AUD3DAT 0x0DA
#define REG_BPL1PTH 0x0E0

static uint16_t set_clr(uint16_t old, uint16_t v, uint16_t mask)
{
	if(v & 0x8000)
		return (uint16_t)(old | (v & mask));
	return (uint16_t)(old & ~(v & mask));
}

uint16_t custom_read(struct Amiga *m, uint32_t reg)
{
	uint64_t now = amiga_now(m);
	int line, hpos;
	switch(reg)
	{
	case REG_DMACONR:
		return (uint16_t)((m->paula.dmacon & 0x07FF) | (m->blit.zero ? 0x2000 : 0));
	case REG_VPOSR:
		line = sched_line(m, now);
		/* LOF, then the Agnus ID. */
		return (uint16_t)((m->denise.short_frame ? 0 : 0x8000) | (AMIGA_AGNUS_ID << 8) | ((line >> 8) & 1));
	case REG_VHPOSR:
		line = sched_line(m, now);
		hpos = sched_hpos(m, now);
		return (uint16_t)(((line & 0xFF) << 8) | (hpos & 0xFF));
	case REG_JOY0DAT:
		if(game_def.joystick < 2)
			return (uint16_t)(((m->paula.mouse_y & 0xFF) << 8) | (m->paula.mouse_x & 0xFF));
		/* fall through */
	case REG_JOY1DAT:
	{
		/* HRM, "Reading the joystick": right is bit 1, left bit 9; down is bit 1 XOR bit 0, up
		 * bit 9 XOR bit 8. */
		const uint8_t j = m->paula.joy[reg == REG_JOY1DAT];
		const int right = (j & AmigaJoyRight) != 0, left = (j & AmigaJoyLeft) != 0;
		const int down = (j & AmigaJoyDown) != 0, up = (j & AmigaJoyUp) != 0;
		return (uint16_t)((left << 9) | ((left ^ up) << 8) | (right << 1) | (right ^ down));
	}
	case REG_CLXDAT:
		amiga_log_once(m, 0x50000000u | reg, "collision detection is not emulated (CLXDAT read)");
		return 0x8000;
	case REG_ADKCONR:
		return m->paula.adkcon;
	case REG_POT0DAT:
	case REG_POT1DAT:
		return 0;
	case REG_POTGOR:
		/* Pin 9 of each port, active low (HRM): port 1's (DATLY, bit 10) is the right mouse button
		 * or the port-1 joystick's second button, port 2's (DATRY, bit 14) the port-2 joystick's. */
		return (uint16_t)(0x5500 & ~(m->paula.rmb || (m->paula.joy[0] & AmigaJoyFire2) ? 0x0400 : 0)
			& ~(m->paula.joy[1] & AmigaJoyFire2 ? 0x4000 : 0));
	case REG_SERDATR:
		return 0x3000;
	case REG_DSKBYTR:
		return 0;
	case REG_INTENAR:
		return m->paula.intena;
	case REG_INTREQR:
		return m->paula.intreq;
	case REG_DENISEID:
		/* The A1200's Lisa reads 0x00F8 (FS-UAE oracle, docs/runtime.md, "Kickstart handoff"). */
		return AMIGA_A1200 ? 0x00F8 : 0xFFFF;
	case REG_COPJMP1:
	case REG_COPJMP2:
		/* OCS writes the chip-bus latch into a write-only register on a read; strobes ignore the data. WinUAE custom.cpp. */
		copper_restart(m, reg == REG_COPJMP1 ? 1 : 2);
		return 0;
	default:
		amiga_log_once(m, 0x50000000u | reg, "read of write-only custom register 0x%03X", (unsigned)reg);
		return 0;
	}
}

int custom_ipl(const struct Amiga *m)
{
	uint16_t p = m->paula.intena & m->paula.intreq & 0x3FFF;
	if(!(m->paula.intena & 0x4000) || !p)
		return 0;
	/* HRM, Interrupts: level per INTREQ bit. */
	if(p & 0x2000)
		return 6;
	if(p & 0x1800)
		return 5;
	if(p & 0x0780)
		return 4;
	if(p & 0x0070)
		return 3;
	if(p & 0x0008)
		return 2;
	return 1;
}

void custom_set_intreq(struct Amiga *m, uint16_t bits)
{
	m->paula.intreq |= bits & 0x7FFF;
}

static void custom_display(struct Amiga *m, uint32_t reg, uint16_t v, int from_copper)
{
	int hpos = from_copper ? m->copper.hpos : sched_hpos(m, amiga_now(m));
	denise_render_to(m, hpos);
	denise_write(m, reg, v);
}

void custom_write(struct Amiga *m, uint32_t reg, uint16_t v, int from_copper)
{
	uint16_t old;
	switch(reg)
	{
	case 0x020:
	case 0x022:
	case 0x024:
	case 0x07E:
		disk_write_reg(m, reg, v);
		return;
	case 0x02A:
	case 0x02C:
		amiga_log_once(m, 0x60000000u | reg, "beam position write 0x%03X ignored", (unsigned)reg);
		return;
	case REG_COPCON:
		m->copper.danger = (v & 2) != 0;
		return;
	case 0x030:
	case 0x032:
		amiga_log_once(m, 0x60000000u | reg, "serial port write 0x%03X ignored", (unsigned)reg);
		return;
	case REG_POTGO:
		m->paula.potgo = v;
		return;
	case 0x036:
		amiga_log_once(m, 0x60000000u | reg, "JOYTEST write ignored");
		return;
	case REG_BLTCON0: m->blit.con0 = v; return;
	case REG_BLTCON1: m->blit.con1 = v; return;
	case REG_BLTAFWM: m->blit.afwm = v; return;
	case REG_BLTALWM: m->blit.alwm = v; return;
	case REG_BLTCPTH: m->blit.cpt = (m->blit.cpt & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); return;
	case REG_BLTCPTL: m->blit.cpt = (m->blit.cpt & 0xFFFF0000u) | (v & 0xFFFE); return;
	case REG_BLTBPTH: m->blit.bpt = (m->blit.bpt & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); return;
	case REG_BLTBPTL: m->blit.bpt = (m->blit.bpt & 0xFFFF0000u) | (v & 0xFFFE); return;
	case REG_BLTAPTH: m->blit.apt = (m->blit.apt & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); return;
	case REG_BLTAPTL: m->blit.apt = (m->blit.apt & 0xFFFF0000u) | (v & 0xFFFE); return;
	case REG_BLTDPTH: m->blit.dpt = (m->blit.dpt & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); return;
	case REG_BLTDPTL: m->blit.dpt = (m->blit.dpt & 0xFFFF0000u) | (v & 0xFFFE); return;
	case REG_BLTSIZE: blit_start(m, v); return;
	case REG_BLTCMOD: m->blit.cmod = (int16_t)(v & 0xFFFE); return;
	case REG_BLTBMOD: m->blit.bmod = (int16_t)(v & 0xFFFE); return;
	case REG_BLTAMOD: m->blit.amod = (int16_t)(v & 0xFFFE); return;
	case REG_BLTDMOD: m->blit.dmod = (int16_t)(v & 0xFFFE); return;
	case REG_BLTCDAT: m->blit.cdat = v; return;
	case REG_BLTBDAT: m->blit.bdat = v; return;
	case REG_BLTADAT: m->blit.adat = v; return;
	case REG_COP1LCH: m->copper.lc1 = (m->copper.lc1 & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); return;
	case REG_COP1LCL: m->copper.lc1 = (m->copper.lc1 & 0xFFFF0000u) | (v & 0xFFFE); return;
	case REG_COP2LCH: m->copper.lc2 = (m->copper.lc2 & 0xFFFF) | ((uint32_t)(v & 0x1F) << 16); return;
	case REG_COP2LCL: m->copper.lc2 = (m->copper.lc2 & 0xFFFF0000u) | (v & 0xFFFE); return;
	case REG_COPJMP1: copper_restart(m, 1); return;
	case REG_COPJMP2: copper_restart(m, 2); return;
	case 0x08C:
		return;
	case REG_DMACON:
		old = m->paula.dmacon;
		m->paula.dmacon = set_clr(old, v, 0x07FF);
		paula_dmacon_changed(m, old);
		return;
	case 0x098:
		return;
	case REG_INTENA:
		m->paula.intena = set_clr(m->paula.intena, v, 0x7FFF);
		return;
	case REG_INTREQ:
		m->paula.intreq = set_clr(m->paula.intreq, v, 0x7FFF);
		if(!(v & 0x8000))
			cia_reassert(m);
		return;
	case REG_ADKCON:
		m->paula.adkcon = set_clr(m->paula.adkcon, v, 0x7FFF);
		return;
	default:
		break;
	}
	if(reg >= REG_AUD0LCH && reg <= REG_AUD3DAT)
	{
		paula_audio_write(m, (int)((reg - REG_AUD0LCH) >> 4), (int)((reg - REG_AUD0LCH) & 0xF), v);
		return;
	}
	if((reg >= REG_DIWSTRT && reg <= 0x094) || (reg >= REG_BPL1PTH && reg <= 0x1BE) || (AMIGA_A1200 && (reg == 0x1E4 || reg == 0x1FC)))
	{
		custom_display(m, reg, v, from_copper);
		return;
	}
	amiga_log_once(m, 0x60000000u | reg, "write to custom register 0x%03X ignored", (unsigned)reg);
}
