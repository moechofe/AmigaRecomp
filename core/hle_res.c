#include "hle.h"

/* Kickstart 3.0's potgo.resource and ciaa/ciab.resource, on the A1200 only. Each function is
 * from its autodoc (potgo.doc, cia.doc, ADCD 2.1); the nodes' negative and positive sizes and
 * versions are those Kickstart 3.0 rev 39.106 lists (docs/runtime.md, "Kickstart handoff"). */

#define D0 m->cpu.d[0]
#define D1 m->cpu.d[1]
#define A1 m->cpu.a[1]
#define A6 m->cpu.a[6]

#if AMIGA_A1200

static void res_node(struct Amiga *m, uint32_t base, uint32_t negsize, uint16_t possize, int lib, uint16_t version, uint32_t name)
{
	hle_jump_table(m, base, negsize, lib);
	hle_w8(m, base + LN_TYPE, NT_RESOURCE);
	hle_w32(m, base + LN_NAME, name);
	hle_w16(m, base + LIB_NEGSIZE, (uint16_t)negsize);
	hle_w16(m, base + LIB_POSSIZE, possize);
	hle_w16(m, base + LIB_VERSION, version);
	hle_add_tail(m, HLE_EXEC_BASE + EB_RESLIST, base);
}

/* An Interrupt the HLE owns, standing for Kickstart's handler of an ICR bit. */
static uint32_t res_owner(struct Amiga *m, uint32_t node, uint32_t name, uint32_t code, uint32_t data)
{
	hle_w8(m, node + LN_TYPE, NT_INTERRUPT);
	hle_w32(m, node + LN_NAME, name);
	hle_w32(m, node + IS_DATA, data);
	hle_w32(m, node + IS_CODE, code);
	return node;
}

void hle_res_build(struct Amiga *m)
{
	uint32_t timer;
	res_node(m, HLE_POTGO_BASE, 0x14, 0x2A, HLE_LIB_POTGO, 37, hle_string(m, HLE_RES_STRINGS, "potgo.resource"));
	res_node(m, HLE_CIAA_BASE, 0x18, 0x88, HLE_LIB_CIAA, 0, hle_string(m, HLE_RES_STRINGS + 0x20, "ciaa.resource"));
	res_node(m, HLE_CIAB_BASE, 0x18, 0x88, HLE_LIB_CIAB, 0, hle_string(m, HLE_RES_STRINGS + 0x40, "ciab.resource"));
	hle_timer_build(m);
	hle_kbd_build(m);

	/* The ICR bits' owners in the Kickstart 3.0 resources at the cracktro (docs/runtime.md,
	 * "Kickstart handoff"): CIA-A timer A, timer B and the alarm timer.device, the serial port
	 * keyboard.device, the flag none; CIA-B the alarm graphics.library, the flag disk.resource,
	 * the others none. */
	timer = res_owner(m, HLE_ICR_NODES + 0x20, hle_r32(m, HLE_TIMER_BASE + LN_NAME), HLE_ICR_NOP, HLE_TIMER_BASE);
	m->hle.icr_vec[0][0] = m->hle.icr_vec[0][1] = m->hle.icr_vec[0][2] = timer;
	m->hle.icr_vec[0][3] = res_owner(m, HLE_ICR_NODES, hle_r32(m, HLE_KBD_BASE + LN_NAME), HLE_KBD_INT, HLE_KBD_BASE);
	m->hle.icr_vec[1][2] = res_owner(m, HLE_ICR_NODES + 0x40, HLE_STRINGS + 0x20, HLE_ICR_NOP, HLE_GFX_BASE);
	m->hle.icr_vec[1][4] = res_owner(m, HLE_ICR_NODES + 0x60, hle_string(m, HLE_RES_STRINGS + 0xA0, "disk.resource"), HLE_ICR_NOP, 0);
}

/* potgo.resource: which of POTGO's bits are allocated, as one word. */
static int res_potgo(struct Amiga *m, uint32_t lvo)
{
	switch(lvo)
	{
	case 6: /* AllocPotBits(d0 bits): the bits granted, those no one holds */
		D0 &= ~(uint32_t)m->hle.potgo_bits & 0xFFFF;
		m->hle.potgo_bits |= (uint16_t)D0;
		return 0;
	case 12: /* FreePotBits(d0 bits) */
		m->hle.potgo_bits &= (uint16_t)~D0;
		return 0;
	case 18: /* WritePotgo(d0 word, d1 mask): the masked bits of POTGO, the others as they were */
		m->hle.potgo = (uint16_t)((m->hle.potgo & ~D1) | (D0 & D1));
		hle_w16(m, 0xDFF034, m->hle.potgo);
		return 0;
	default:
		return -1;
	}
}

/* ciaa.resource (`which` 0) and ciab.resource (1). */
static int res_cia(struct Amiga *m, int which, uint32_t lvo)
{
	struct Cia *c = which ? &m->cia_b : &m->cia_a;
	uint32_t bit = D0 & 7;
	switch(lvo)
	{
	case 6: /* AddICRVector(d0 iCRBit, a1 interrupt): 0, the bit enabled; or the owner */
		if(bit > 4)
			break;
		if(m->hle.icr_vec[which][bit])
		{
			D0 = m->hle.icr_vec[which][bit];
			return 0;
		}
		m->hle.icr_vec[which][bit] = A1;
		cia_write(m, c, 0xD, (uint8_t)(0x80 | (1u << bit)));
		D0 = 0;
		return 0;
	case 12: /* RemICRVector(d0 iCRBit, a1 interrupt): the bit disabled and freed */
		if(bit > 4)
			break;
		if(m->hle.icr_vec[which][bit] == A1)
		{
			cia_write(m, c, 0xD, (uint8_t)(1u << bit));
			m->hle.icr_vec[which][bit] = 0;
		}
		return 0;
	case 18: /* AbleICR(d0 mask): bit 7 set enables, clear disables the other bits; the old mask */
	{
		uint8_t old = c->icr_mask;
		if(D0 & 0x7F)
			cia_write(m, c, 0xD, (uint8_t)D0);
		D0 = old;
		return 0;
	}
	case 24: /* SetICR(d0 mask): bit 7 set requests, clear cancels the other bits; the old requests */
	{
		uint8_t old = c->icr & 0x1F;
		if(D0 & 0x1F)
		{
			if(D0 & 0x80)
				c->icr |= (uint8_t)(D0 & 0x1F);
			else
				c->icr &= (uint8_t)~(D0 & 0x1F);
			cia_reassert(m);
		}
		D0 = old;
		return 0;
	}
	default:
		return -1;
	}
	amiga_fatal(m, "%s: ICR bit %u does not exist", which ? "ciab.resource" : "ciaa.resource", bit);
	return 0;
}

/* The next ICR bit of `pending` (an ICR the CIA's server read) with an owner whose code is
 * guest code: its Interrupt, with the bit cleared from `pending`; the HLE's own owners are done
 * here. 0 when no bit is left. */
uint32_t hle_res_icr_next(struct Amiga *m, int which, uint32_t *pending)
{
	while(*pending & 0x1F)
	{
		uint32_t bit = 0, node, code;
		while(!(*pending & (1u << bit)))
			bit++;
		*pending &= ~(1u << bit);
		node = m->hle.icr_vec[which][bit];
		if(!node)
			continue;
		code = hle_r32(m, node + IS_CODE);
		if(code == HLE_KBD_INT)
			hle_input_key(m, m->cia_a.sdr);
		else if(code != HLE_ICR_NOP)
			return node;
	}
	return 0;
}

int hle_res_call(struct Amiga *m, int lib, uint32_t lvo)
{
	switch(lib)
	{
	case HLE_LIB_POTGO: return res_potgo(m, lvo);
	case HLE_LIB_CIAA: return res_cia(m, 0, lvo);
	case HLE_LIB_CIAB: return res_cia(m, 1, lvo);
	case HLE_LIB_TIMER: return hle_timer_call(m, lvo);
	default: return -1;
	}
}

#else

void hle_res_build(struct Amiga *m)
{
	(void)m;
}

int hle_res_call(struct Amiga *m, int lib, uint32_t lvo)
{
	(void)m;
	(void)lib;
	(void)lvo;
	return -1;
}

#endif
