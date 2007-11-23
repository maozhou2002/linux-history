/* $Id: fmuld.c,v 1.4 1999/05/28 13:44:11 jj Exp $
 * arch/sparc64/math-emu/fmuld.c
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include "sfp-util.h"
#include "soft-fp.h"
#include "double.h"

int FMULD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_EX;
	FP_DECL_D(A); FP_DECL_D(B); FP_DECL_D(R);

	FP_UNPACK_DP(A, rs1);
	FP_UNPACK_DP(B, rs2);
	FP_MUL_D(R, A, B);
	FP_PACK_DP(rd, R);
	FP_HANDLE_EXCEPTIONS;
}