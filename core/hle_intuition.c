#include "hle.h"

/* intuition.library functions, by negative LVO, from NDK 3.2 FD/intuition_lib.fd (bias 30,
 * 6 bytes a slot). Returns 0 to return to the caller, -1 when it is not implemented. */

int hle_int_call(struct Amiga *m, uint32_t lvo)
{
	switch(lvo)
	{
	case 390: /* RethinkDisplay: there are no screens to merge */
		amiga_log_once(m, 0xF0000000u | lvo, "RethinkDisplay: no screens; display left alone");
		return 0;
	default:
		return -1;
	}
}
