/* $Id: fcmped.c,v 1.8 1999/05/28 13:41:38 jj Exp $
 * arch/sparc/math-emu/fcmped.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"

int FCMPED(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_D(A); FP_DECL_D(B);
	long ret;
	unsigned long fsr;
	
	FP_UNPACK_RAW_DP(A, rs1);
	FP_UNPACK_RAW_DP(B, rs2);
	FP_CMP_D(ret, B, A, 3);
	if (ret == 3)
		FP_SET_EXCEPTION(FP_EX_INVALID);
	if (!FP_INHIBIT_RESULTS) {
		if (ret == -1) ret = 2;
		fsr = *(long *)rd;
		fsr &= ~0xc00;
		fsr |= (ret << 10);
		*(long *)rd = fsr;
	}
	FP_HANDLE_EXCEPTIONS;
}