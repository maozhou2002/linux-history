/************************************************************************
 *	FILE NAME : TMSCSIM.C						*
 *	     BY   : C.L. Huang,  ching@tekram.com.tw			*
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI	*
 *		     Bus Master Host Adapter				*
 * (C)Copyright 1995-1996 Tekram Technology Co., Ltd.			*
 ************************************************************************
 * (C) Copyright: put under GNU GPL in 10/96				*
 *				(see Documentation/scsi/tmscsim.txt)	*
 ************************************************************************
 * $Id: tmscsim.c,v 2.60.2.30 2000/12/20 01:07:12 garloff Exp $		*
 *	Enhancements and bugfixes by					*
 *	Kurt Garloff <kurt@garloff.de>	<garloff@suse.de>		*
 ************************************************************************
 *	HISTORY:							*
 *									*
 *	REV#	DATE	NAME	DESCRIPTION				*
 *	1.00  96/04/24	CLH	First release				*
 *	1.01  96/06/12	CLH	Fixed bug of Media Change for Removable *
 *				Device, scan all LUN. Support Pre2.0.10 *
 *	1.02  96/06/18	CLH	Fixed bug of Command timeout ...	*
 *	1.03  96/09/25	KG	Added tmscsim_proc_info()		*
 *	1.04  96/10/11	CLH	Updating for support KV 2.0.x		*
 *	1.05  96/10/18	KG	Fixed bug in DC390_abort(null ptr deref)*
 *	1.06  96/10/25	KG	Fixed module support			*
 *	1.07  96/11/09	KG	Fixed tmscsim_proc_info()		*
 *	1.08  96/11/18	KG	Fixed null ptr in DC390_Disconnect()	*
 *	1.09  96/11/30	KG	Added register the allocated IO space	*
 *	1.10  96/12/05	CLH	Modified tmscsim_proc_info(), and reset *
 *				pending interrupt in DC390_detect()	*
 *	1.11  97/02/05	KG/CLH	Fixeds problem with partitions greater	*
 *				than 1GB				*
 *	1.12  98/02/15  MJ      Rewritten PCI probing			*
 *	1.13  98/04/08	KG	Support for non DC390, __initfunc decls,*
 *				changed max devs from 10 to 16		*
 *	1.14a 98/05/05	KG	Dynamic DCB allocation, add-single-dev	*
 *				for LUNs if LUN_SCAN (BIOS) not set	*
 *				runtime config using /proc interface	*
 *	1.14b 98/05/06	KG	eliminated cli (); sti (); spinlocks	*
 *	1.14c 98/05/07	KG	2.0.x compatibility			*
 *	1.20a 98/05/07	KG	changed names of funcs to be consistent *
 *				DC390_ (entry points), dc390_ (internal)*
 *				reworked locking			*
 *	1.20b 98/05/12	KG	bugs: version, kfree, _ctmp		*
 *				debug output				*
 *	1.20c 98/05/12	KG	bugs: kfree, parsing, EEpromDefaults	*
 *	1.20d 98/05/14	KG	bugs: list linkage, clear flag after  	*
 *				reset on startup, code cleanup		*
 *	1.20e 98/05/15	KG	spinlock comments, name space cleanup	*
 *				pLastDCB now part of ACB structure	*
 *				added stats, timeout for 2.1, TagQ bug	*
 *				RESET and INQUIRY interface commands	*
 *	1.20f 98/05/18	KG	spinlocks fixes, max_lun fix, free DCBs	*
 *				for missing LUNs, pending int		*
 *	1.20g 98/05/19	KG	Clean up: Avoid short			*
 *	1.20h 98/05/21	KG	Remove AdaptSCSIID, max_lun ...		*
 *	1.20i 98/05/21	KG	Aiiie: Bug with TagQMask       		*
 *	1.20j 98/05/24	KG	Handle STAT_BUSY, handle pACB->pLinkDCB	*
 *				== 0 in remove_dev and DoingSRB_Done	*
 *	1.20k 98/05/25	KG	DMA_INT	(experimental)	       		*
 *	1.20l 98/05/27	KG	remove DMA_INT; DMA_IDLE cmds added;	*
 *	1.20m 98/06/10	KG	glitch configurable; made some global	*
 *				vars part of ACB; use DC390_readX	*
 *	1.20n 98/06/11	KG	startup params				*
 *	1.20o 98/06/15	KG	added TagMaxNum to boot/module params	*
 *				Device Nr -> Idx, TagMaxNum power of 2  *
 *	1.20p 98/06/17	KG	Docu updates. Reset depends on settings *
 *				pci_set_master added; 2.0.xx: pcibios_*	*
 *				used instead of MechNum things ...	*
 *	1.20q 98/06/23	KG	Changed defaults. Added debug code for	*
 *				removable media and fixed it. TagMaxNum	*
 *				fixed for DC390. Locking: ACB, DRV for	*
 *				better IRQ sharing. Spelling: Queueing	*
 *				Parsing and glitch_cfg changes. Display	*
 *				real SyncSpeed value. Made DisConn	*
 *				functional (!)				*
 *	1.20r 98/06/30	KG	Debug macros, allow disabling DsCn, set	*
 *				BIT4 in CtrlR4, EN_PAGE_INT, 2.0 module	*
 *				param -1 fixed.				*
 *	1.20s 98/08/20	KG	Debug info on abort(), try to check PCI,*
 *				phys_to_bus instead of phys_to_virt,	*
 *				fixed sel. process, fixed locking,	*
 *				added MODULE_XXX infos, changed IRQ	*
 *				request flags, disable DMA_INT		*
 *	1.20t 98/09/07	KG	TagQ report fixed; Write Erase DMA Stat;*
 *				initfunc -> __init; better abort;	*
 *				Timeout for XFER_DONE & BLAST_COMPLETE;	*
 *				Allow up to 33 commands being processed *
 *	2.0a  98/10/14	KG	Max Cmnds back to 17. DMA_Stat clearing *
 *				all flags. Clear within while() loops	*
 *				in DataIn_0/Out_0. Null ptr in dumpinfo	*
 *				for pSRB==0. Better locking during init.*
 *				bios_param() now respects part. table.	*
 *	2.0b  98/10/24	KG	Docu fixes. Timeout Msg in DMA Blast.	*
 *				Disallow illegal idx in INQUIRY/REMOVE	*
 *	2.0c  98/11/19	KG	Cleaned up detect/init for SMP boxes, 	*
 *				Write Erase DMA (1.20t) caused problems	*
 *	2.0d  98/12/25	KG	Christmas release ;-) Message handling  *
 *				completely reworked. Handle target ini-	*
 *				tiated SDTR correctly.			*
 *	2.0d1 99/01/25	KG	Try to handle RESTORE_PTR		*
 *	2.0d2 99/02/08	KG	Check for failure of kmalloc, correct 	*
 *				inclusion of scsicam.h, DelayReset	*
 *	2.0d3 99/05/31	KG	DRIVER_OK -> DID_OK, DID_NO_CONNECT,	*
 *				detect Target mode and warn.		*
 *				pcmd->result handling cleaned up.	*
 *	2.0d4 99/06/01	KG	Cleaned selection process. Found bug	*
 *				which prevented more than 16 tags. Now:	*
 *				24. SDTR cleanup. Cleaner multi-LUN	*
 *				handling. Don't modify ControlRegs/FIFO	*
 *				when connected.				*
 *	2.0d5 99/06/01	KG	Clear DevID, Fix INQUIRY after cfg chg.	*
 *	2.0d6 99/06/02	KG	Added ADD special command to allow cfg.	*
 *				before detection. Reset SYNC_NEGO_DONE	*
 *				after a bus reset.			*
 *	2.0d7 99/06/03	KG	Fixed bugs wrt add,remove commands	*
 *	2.0d8 99/06/04	KG	Removed copying of cmnd into CmdBlock.	*
 *				Fixed Oops in _release().		*
 *	2.0d9 99/06/06	KG	Also tag queue INQUIRY, T_U_R, ...	*
 *				Allow arb. no. of Tagged Cmnds. Max 32	*
 *	2.0d1099/06/20	KG	TagMaxNo changes now honoured! Queueing *
 *				clearified (renamed ..) TagMask handling*
 *				cleaned.				*
 *	2.0d1199/06/28	KG	cmd->result now identical to 2.0d2	*
 *	2.0d1299/07/04	KG	Changed order of processing in IRQ	*
 *	2.0d1399/07/05	KG	Don't update DCB fields if removed	*
 *	2.0d1499/07/05	KG	remove_dev: Move kfree() to the end	*
 *	2.0d1599/07/12	KG	use_new_eh_code: 0, ULONG -> UINT where	*
 *				appropriate				*
 *	2.0d1699/07/13	KG	Reenable StartSCSI interrupt, Retry msg	*
 *	2.0d1799/07/15	KG	Remove debug msg. Disable recfg. when	*
 *				there are queued cmnds			*
 *	2.0d1899/07/18	KG	Selection timeout: Don't requeue	*
 *	2.0d1999/07/18	KG	Abort: Only call scsi_done if dequeued	*
 *	2.0d2099/07/19	KG	Rst_Detect: DoingSRB_Done		*
 *	2.0d2199/08/15	KG	dev_id for request/free_irq, cmnd[0] for*
 *				RETRY, SRBdone does DID_ABORT for the 	*
 *				cmd passed by DC390_reset()		*
 *	2.0d2299/08/25	KG	dev_id fixed. can_queue: 42		*
 *	2.0d2399/08/25	KG	Removed some debugging code. dev_id 	*
 *				now is set to pACB. Use u8,u16,u32. 	*
 *	2.0d2499/11/14	KG	Unreg. I/O if failed IRQ alloc. Call	*
 * 				done () w/ DID_BAD_TARGET in case of	*
 *				missing DCB. We	are old EH!!		*
 *	2.0d2500/01/15	KG	2.3.3x compat from Andreas Schultz	*
 *				set unique_id. Disable RETRY message.	*
 *	2.0d2600/01/29	KG	Go to new EH.				*
 *	2.0d2700/01/31	KG	... but maintain 2.0 compat.		*
 *				and fix DCB freeing			*
 *	2.0d2800/02/14	KG	Queue statistics fixed, dump special cmd*
 *				Waiting_Timer for failed StartSCSI	*
 *				New EH: Don't return cmnds to ML on RST *
 *				Use old EH (don't have new EH fns yet)	*
 * 				Reset: Unlock, but refuse to queue	*
 * 				2.3 __setup function			*
 *	2.0e  00/05/22	KG	Return residual for 2.3			*
 *	2.0e1 00/05/25	KG	Compile fixes for 2.3.99		*
 *	2.0e2 00/05/27	KG	Jeff Garzik's pci_enable_device()	*
 *	2.0e3 00/09/29	KG	Some 2.4 changes. Don't try Sync Nego	*
 *				before INQUIRY has reported ability. 	*
 *				Recognise INQUIRY as scanning command.	*
 *	2.0e4 00/10/13	KG	Allow compilation into 2.4 kernel	*
 *	2.0e5 00/11/17	KG	Store Inq.flags in DCB			*
 *	2.0e6 00/11/22  KG	2.4 init function (Thx to O.Schumann)	*
 * 				2.4 PCI device table (Thx to A.Richter)	*
 *	2.0e7 00/11/28	KG	Allow overriding of BIOS settings	*
 *	2.0f  00/12/20	KG	Handle failed INQUIRYs during scan	*
 *	2.1a  03/11/29  GL, KG	Initial fixing for 2.6. Convert to	*
 *				use the current PCI-mapping API, update	*
 *				command-queuing.			*
 *	2.1b  04/04/13  GL	Fix for 64-bit platforms		*
 *	2.1b1 04/01/31	GL	(applied 05.04) Remove internal		*
 *				command-queuing.			*
 *	2.1b2 04/02/01	CH	(applied 05.04) Fix error-handling	*
 *	2.1c  04/05/23  GL	Update to use the new pci_driver API,	*
 *				some scsi EH updates, more cleanup.	*
 *	2.1d  04/05/27	GL	Moved setting of scan_devices to	*
 *				slave_alloc/_configure/_destroy, as	*
 *				suggested by CH.			*
 ***********************************************************************/

/* DEBUG options */
//#define DC390_DEBUG0
//#define DC390_DEBUG1
//#define DC390_DCBDEBUG
//#define DC390_PARSEDEBUG
//#define DC390_REMOVABLEDEBUG
//#define DC390_LOCKDEBUG

//#define NOP do{}while(0)
#define C_NOP

/* Debug definitions */
#ifdef DC390_DEBUG0
# define DEBUG0(x) x
#else
# define DEBUG0(x) C_NOP
#endif
#ifdef DC390_DEBUG1
# define DEBUG1(x) x
#else
# define DEBUG1(x) C_NOP
#endif
#ifdef DC390_DCBDEBUG
# define DCBDEBUG(x) x
#else
# define DCBDEBUG(x) C_NOP
#endif
#ifdef DC390_PARSEDEBUG
# define PARSEDEBUG(x) x
#else
# define PARSEDEBUG(x) C_NOP
#endif
#ifdef DC390_REMOVABLEDEBUG
# define REMOVABLEDEBUG(x) x
#else
# define REMOVABLEDEBUG(x) C_NOP
#endif
#define DCBDEBUG1(x) C_NOP

#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsicam.h>


#define DC390_BANNER "Tekram DC390/AM53C974"
#define DC390_VERSION "2.1d 2004-05-27"

#define PCI_DEVICE_ID_AMD53C974 	PCI_DEVICE_ID_AMD_SCSI

#include "tmscsim.h"

static u8 dc390_StartSCSI( struct dc390_acb* pACB, struct dc390_dcb* pDCB, struct dc390_srb* pSRB );
static void dc390_DataOut_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_DataIn_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_Command_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_Status_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_MsgOut_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_MsgIn_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_DataOutPhase( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_DataInPhase( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_CommandPhase( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_StatusPhase( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_MsgOutPhase( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_MsgInPhase( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_Nop_0( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);
static void dc390_Nop_1( struct dc390_acb* pACB, struct dc390_srb* pSRB, u8 *psstatus);

static void dc390_SetXferRate( struct dc390_acb* pACB, struct dc390_dcb* pDCB );
static void dc390_Disconnect( struct dc390_acb* pACB );
static void dc390_Reselect( struct dc390_acb* pACB );
static void dc390_SRBdone( struct dc390_acb* pACB, struct dc390_dcb* pDCB, struct dc390_srb* pSRB );
static void dc390_DoingSRB_Done( struct dc390_acb* pACB, struct scsi_cmnd * cmd);
static void dc390_ScsiRstDetect( struct dc390_acb* pACB );
static void dc390_ResetSCSIBus( struct dc390_acb* pACB );
static void dc390_EnableMsgOut_Abort(struct dc390_acb*, struct dc390_srb*);
static irqreturn_t do_DC390_Interrupt( int, void *, struct pt_regs *);

static void   dc390_updateDCB (struct dc390_acb* pACB, struct dc390_dcb* pDCB);

static u32	dc390_laststatus = 0;
static u8	dc390_adapterCnt = 0;

/* Startup values, to be overriden on the commandline */
static int tmscsim[] = {-2, -2, -2, -2, -2, -2};
static int tmscsim_paramnum = ARRAY_SIZE(tmscsim);

module_param_array(tmscsim, int, tmscsim_paramnum, 0);
MODULE_PARM_DESC(tmscsim, "Host SCSI ID, Speed (0=10MHz), Device Flags, Adapter Flags, Max Tags (log2(tags)-1), DelayReset (s)");
MODULE_AUTHOR("C.L. Huang / Kurt Garloff");
MODULE_DESCRIPTION("SCSI host adapter driver for Tekram DC390 and other AMD53C974A based PCI SCSI adapters");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("sd,sr,sg,st");

static void *dc390_phase0[]={
       dc390_DataOut_0,
       dc390_DataIn_0,
       dc390_Command_0,
       dc390_Status_0,
       dc390_Nop_0,
       dc390_Nop_0,
       dc390_MsgOut_0,
       dc390_MsgIn_0,
       dc390_Nop_1
       };

static void *dc390_phase1[]={
       dc390_DataOutPhase,
       dc390_DataInPhase,
       dc390_CommandPhase,
       dc390_StatusPhase,
       dc390_Nop_0,
       dc390_Nop_0,
       dc390_MsgOutPhase,
       dc390_MsgInPhase,
       dc390_Nop_1
       };

#ifdef DC390_DEBUG1
static char* dc390_p0_str[] = {
       "dc390_DataOut_0",
       "dc390_DataIn_0",
       "dc390_Command_0",
       "dc390_Status_0",
       "dc390_Nop_0",
       "dc390_Nop_0",
       "dc390_MsgOut_0",
       "dc390_MsgIn_0",
       "dc390_Nop_1"
       };
     
static char* dc390_p1_str[] = {
       "dc390_DataOutPhase",
       "dc390_DataInPhase",
       "dc390_CommandPhase",
       "dc390_StatusPhase",
       "dc390_Nop_0",
       "dc390_Nop_0",
       "dc390_MsgOutPhase",
       "dc390_MsgInPhase",
       "dc390_Nop_1"
       };
#endif   

/* Devices erroneously pretending to be able to do TagQ */
static u8  dc390_baddevname1[2][28] ={
       "SEAGATE ST3390N         9546",
       "HP      C3323-300       4269"};
#define BADDEVCNT	2

static u8  dc390_eepromBuf[MAX_ADAPTER_NUM][EE_LEN];
static u8  dc390_clock_period1[] = {4, 5, 6, 7, 8, 10, 13, 20};
static u8  dc390_clock_speed[] = {100,80,67,57,50, 40, 31, 20};

/***********************************************************************
 * Functions for the management of the internal structures 
 * (DCBs, SRBs, Queueing)
 *
 **********************************************************************/
static struct dc390_dcb __inline__ *dc390_findDCB ( struct dc390_acb* pACB, u8 id, u8 lun)
{
   struct dc390_dcb* pDCB = pACB->pLinkDCB; if (!pDCB) return 0;
   while (pDCB->TargetID != id || pDCB->TargetLUN != lun)
     {
	pDCB = pDCB->pNextDCB;
	if (pDCB == pACB->pLinkDCB)
	     return 0;
     }
   DCBDEBUG1( printk (KERN_DEBUG "DCB %p (%02x,%02x) found.\n",	\
		      pDCB, pDCB->TargetID, pDCB->TargetLUN));
   return pDCB;
}

/* Queueing philosphy:
 * There are a couple of lists:
 * - Query: Contains the Scsi Commands not yet turned into SRBs (per ACB)
 *   (Note: For new EH, it is unnecessary!)
 * - Waiting: Contains a list of SRBs not yet sent (per DCB)
 * - Free: List of free SRB slots
 * 
 * If there are no waiting commands for the DCB, the new one is sent to the bus
 * otherwise the oldest one is taken from the Waiting list and the new one is 
 * queued to the Waiting List
 * 
 * Lists are managed using two pointers and eventually a counter
 */

/* Insert SRB oin top of free list */
static __inline__ void dc390_Free_insert (struct dc390_acb* pACB, struct dc390_srb* pSRB)
{
    DEBUG0(printk ("DC390: Free SRB %p\n", pSRB));
    pSRB->pNextSRB = pACB->pFreeSRB;
    pACB->pFreeSRB = pSRB;
}


/* Inserts a SRB to the top of the Waiting list */
static __inline__ void dc390_Waiting_insert ( struct dc390_dcb* pDCB, struct dc390_srb* pSRB )
{
    DEBUG0(printk ("DC390: Insert pSRB %p cmd %li to Waiting\n", pSRB, pSRB->pcmd->pid));
    pSRB->pNextSRB = pDCB->pWaitingSRB;
    if (!pDCB->pWaitingSRB)
	pDCB->pWaitLast = pSRB;
    pDCB->pWaitingSRB = pSRB;
    pDCB->WaitSRBCnt++;
}


static __inline__ void dc390_Going_append (struct dc390_dcb* pDCB, struct dc390_srb* pSRB)
{
    pDCB->GoingSRBCnt++;
    DEBUG0(printk("DC390: Append SRB %p to Going\n", pSRB));
    /* Append to the list of Going commands */
    if( pDCB->pGoingSRB )
	pDCB->pGoingLast->pNextSRB = pSRB;
    else
	pDCB->pGoingSRB = pSRB;

    pDCB->pGoingLast = pSRB;
    /* No next one in sent list */
    pSRB->pNextSRB = NULL;
}

static __inline__ void dc390_Going_remove (struct dc390_dcb* pDCB, struct dc390_srb* pSRB)
{
	DEBUG0(printk("DC390: Remove SRB %p from Going\n", pSRB));
   if (pSRB == pDCB->pGoingSRB)
	pDCB->pGoingSRB = pSRB->pNextSRB;
   else
     {
	struct dc390_srb* psrb = pDCB->pGoingSRB;
	while (psrb && psrb->pNextSRB != pSRB)
	  psrb = psrb->pNextSRB;
	if (!psrb) 
	  { printk (KERN_ERR "DC390: Remove non-ex. SRB %p from Going!\n", pSRB); return; }
	psrb->pNextSRB = pSRB->pNextSRB;
	if (pSRB == pDCB->pGoingLast)
	  pDCB->pGoingLast = psrb;
     }
   pDCB->GoingSRBCnt--;
}

/* Moves SRB from Going list to the top of Waiting list */
static void dc390_Going_to_Waiting ( struct dc390_dcb* pDCB, struct dc390_srb* pSRB )
{
    DEBUG0(printk(KERN_INFO "DC390: Going_to_Waiting (SRB %p) pid = %li\n", pSRB, pSRB->pcmd->pid));
    /* Remove SRB from Going */
    dc390_Going_remove (pDCB, pSRB);
    /* Insert on top of Waiting */
    dc390_Waiting_insert (pDCB, pSRB);
    /* Tag Mask must be freed elsewhere ! (KG, 99/06/18) */
}

/* Moves first SRB from Waiting list to Going list */
static __inline__ void dc390_Waiting_to_Going ( struct dc390_dcb* pDCB, struct dc390_srb* pSRB )
{	
	/* Remove from waiting list */
	DEBUG0(printk("DC390: Remove SRB %p from head of Waiting\n", pSRB));
	pDCB->pWaitingSRB = pSRB->pNextSRB;
	if( !pDCB->pWaitingSRB ) pDCB->pWaitLast = NULL;
	pDCB->WaitSRBCnt--;
	dc390_Going_append (pDCB, pSRB);
}

static void DC390_waiting_timed_out (unsigned long ptr);
/* Sets the timer to wake us up */
static void dc390_waiting_timer (struct dc390_acb* pACB, unsigned long to)
{
	if (timer_pending (&pACB->Waiting_Timer)) return;
	init_timer (&pACB->Waiting_Timer);
	pACB->Waiting_Timer.function = DC390_waiting_timed_out;
	pACB->Waiting_Timer.data = (unsigned long)pACB;
	if (time_before (jiffies + to, pACB->pScsiHost->last_reset))
		pACB->Waiting_Timer.expires = pACB->pScsiHost->last_reset + 1;
	else
		pACB->Waiting_Timer.expires = jiffies + to + 1;
	add_timer (&pACB->Waiting_Timer);
}


/* Send the next command from the waiting list to the bus */
static void dc390_Waiting_process ( struct dc390_acb* pACB )
{
    struct dc390_dcb *ptr, *ptr1;
    struct dc390_srb *pSRB;

    if( (pACB->pActiveDCB) || (pACB->ACBFlag & (RESET_DETECT+RESET_DONE+RESET_DEV) ) )
	return;
    if (timer_pending (&pACB->Waiting_Timer)) del_timer (&pACB->Waiting_Timer);
    ptr = pACB->pDCBRunRobin;
    if( !ptr )
      {
	ptr = pACB->pLinkDCB;
	pACB->pDCBRunRobin = ptr;
      }
    ptr1 = ptr;
    if (!ptr1) return;
    do 
      {
	pACB->pDCBRunRobin = ptr1->pNextDCB;
	if( !( pSRB = ptr1->pWaitingSRB ) ||
	    ( ptr1->MaxCommand <= ptr1->GoingSRBCnt ))
	  ptr1 = ptr1->pNextDCB;
	else
	  {
	    /* Try to send to the bus */
	    if( !dc390_StartSCSI(pACB, ptr1, pSRB) )
	      dc390_Waiting_to_Going (ptr1, pSRB);
	    else
	      dc390_waiting_timer (pACB, HZ/5);
	    break;
	  }
      } while (ptr1 != ptr);
    return;
}

/* Wake up waiting queue */
static void DC390_waiting_timed_out (unsigned long ptr)
{
	struct dc390_acb* pACB = (struct dc390_acb*)ptr;
	unsigned long iflags;
	DEBUG0(printk ("DC390: Debug: Waiting queue woken up by timer!\n"));
	spin_lock_irqsave(pACB->pScsiHost->host_lock, iflags);
	dc390_Waiting_process (pACB);
	spin_unlock_irqrestore(pACB->pScsiHost->host_lock, iflags);
}

static struct scatterlist* dc390_sg_build_single(struct scatterlist *sg, void *addr, unsigned int length)
{
	memset(sg, 0, sizeof(struct scatterlist));
	sg->page	= virt_to_page(addr);
	sg->length	= length;
	sg->offset	= (unsigned long)addr & ~PAGE_MASK;
	return sg;
}

/* Create pci mapping */
static int dc390_pci_map (struct dc390_srb* pSRB)
{
	int error = 0;
	struct scsi_cmnd *pcmd = pSRB->pcmd;
	struct pci_dev *pdev = pSRB->pSRBDCB->pDCBACB->pdev;
	dc390_cmd_scp_t* cmdp = ((dc390_cmd_scp_t*)(&pcmd->SCp));

	/* Map sense buffer */
	if (pSRB->SRBFlag & AUTO_REQSENSE) {
		pSRB->pSegmentList	= dc390_sg_build_single(&pSRB->Segmentx, pcmd->sense_buffer, sizeof(pcmd->sense_buffer));
		pSRB->SGcount		= pci_map_sg(pdev, pSRB->pSegmentList, 1,
						     DMA_FROM_DEVICE);
		cmdp->saved_dma_handle	= sg_dma_address(pSRB->pSegmentList);

		/* TODO: error handling */
		if (pSRB->SGcount != 1)
			error = 1;
		DEBUG1(printk("%s(): Mapped sense buffer %p at %x\n", __FUNCTION__, pcmd->sense_buffer, cmdp->saved_dma_handle));
	/* Map SG list */
	} else if (pcmd->use_sg) {
		pSRB->pSegmentList	= (struct scatterlist *) pcmd->request_buffer;
		pSRB->SGcount		= pci_map_sg(pdev, pSRB->pSegmentList, pcmd->use_sg,
						     pcmd->sc_data_direction);
		/* TODO: error handling */
		if (!pSRB->SGcount)
			error = 1;
		DEBUG1(printk("%s(): Mapped SG %p with %d (%d) elements\n",\
			      __FUNCTION__, pcmd->request_buffer, pSRB->SGcount, pcmd->use_sg));
	/* Map single segment */
	} else if (pcmd->request_buffer && pcmd->request_bufflen) {
		pSRB->pSegmentList	= dc390_sg_build_single(&pSRB->Segmentx, pcmd->request_buffer, pcmd->request_bufflen);
		pSRB->SGcount		= pci_map_sg(pdev, pSRB->pSegmentList, 1,
						     pcmd->sc_data_direction);
		cmdp->saved_dma_handle	= sg_dma_address(pSRB->pSegmentList);

		/* TODO: error handling */
		if (pSRB->SGcount != 1)
			error = 1;
		DEBUG1(printk("%s(): Mapped request buffer %p at %x\n", __FUNCTION__, pcmd->request_buffer, cmdp->saved_dma_handle));
	/* No mapping !? */	
    	} else
		pSRB->SGcount = 0;

	return error;
}

/* Remove pci mapping */
static void dc390_pci_unmap (struct dc390_srb* pSRB)
{
	struct scsi_cmnd *pcmd = pSRB->pcmd;
	struct pci_dev *pdev = pSRB->pSRBDCB->pDCBACB->pdev;
	DEBUG1(dc390_cmd_scp_t* cmdp = ((dc390_cmd_scp_t*)(&pcmd->SCp)));

	if (pSRB->SRBFlag) {
		pci_unmap_sg(pdev, &pSRB->Segmentx, 1, DMA_FROM_DEVICE);
		DEBUG1(printk("%s(): Unmapped sense buffer at %x\n", __FUNCTION__, cmdp->saved_dma_handle));
	} else if (pcmd->use_sg) {
		pci_unmap_sg(pdev, pcmd->request_buffer, pcmd->use_sg, pcmd->sc_data_direction);
		DEBUG1(printk("%s(): Unmapped SG at %p with %d elements\n", __FUNCTION__, pcmd->request_buffer, pcmd->use_sg));
	} else if (pcmd->request_buffer && pcmd->request_bufflen) {
		pci_unmap_sg(pdev, &pSRB->Segmentx, 1, pcmd->sc_data_direction);
		DEBUG1(printk("%s(): Unmapped request buffer at %x\n", __FUNCTION__, cmdp->saved_dma_handle));
	}
}

static int DC390_queuecommand(struct scsi_cmnd *cmd,
		void (*done)(struct scsi_cmnd *))
{
	struct scsi_device *sdev = cmd->device;
	struct dc390_acb *acb = (struct dc390_acb *)sdev->host->hostdata;
	struct dc390_dcb *dcb = sdev->hostdata;
	struct dc390_srb *srb;

	if (dcb->pWaitingSRB)
		goto device_busy;
	if (dcb->MaxCommand <= dcb->GoingSRBCnt)
		goto device_busy;
	if (acb->pActiveDCB)
		goto host_busy;
	if (acb->ACBFlag & (RESET_DETECT|RESET_DONE|RESET_DEV))
		goto host_busy;

	srb = acb->pFreeSRB;
	if (unlikely(srb == NULL))
		goto host_busy;

	cmd->scsi_done = done;
	cmd->result = 0;
	acb->Cmds++;

	acb->pFreeSRB = srb->pNextSRB;
	srb->pNextSRB = NULL;

	srb->pSRBDCB = dcb;
	srb->pcmd = cmd;
    
	srb->SGIndex = 0;
	srb->AdaptStatus = 0;
	srb->TargetStatus = 0;
	srb->MsgCnt = 0;
	if (dcb->DevType == TYPE_TAPE)
		srb->RetryCnt = 0;
	else
		srb->RetryCnt = 1;
	srb->SRBStatus = 0;
	srb->SRBFlag = 0;
	srb->SRBState = 0;
	srb->TotalXferredLen = 0;
	srb->SGBusAddr = 0;
	srb->SGToBeXferLen = 0;
	srb->ScsiPhase = 0;
	srb->EndMessage = 0;
	srb->TagNumber = 255;

	if (dc390_StartSCSI(acb, dcb, srb)) {
		dc390_Waiting_insert(dcb, srb);
		dc390_waiting_timer(acb, HZ/5);
		goto done;
	}

	dc390_Going_append(dcb, srb);
 done:
	return 0;

 host_busy:
	dc390_Waiting_process(acb);
	return SCSI_MLQUEUE_HOST_BUSY;

 device_busy:
	dc390_Waiting_process(acb);
	return SCSI_MLQUEUE_DEVICE_BUSY;
}

static void dc390_dumpinfo (struct dc390_acb* pACB, struct dc390_dcb* pDCB, struct dc390_srb* pSRB)
{
    struct pci_dev *pdev;
    u16 pstat;

    if (!pDCB) pDCB = pACB->pActiveDCB;
    if (!pSRB && pDCB) pSRB = pDCB->pActiveSRB;

    if (pSRB) 
    {
	printk ("DC390: SRB: Xferred %08lx, Remain %08lx, State %08x, Phase %02x\n",
		pSRB->TotalXferredLen, pSRB->SGToBeXferLen, pSRB->SRBState,
		pSRB->ScsiPhase);
	printk ("DC390: AdpaterStatus: %02x, SRB Status %02x\n", pSRB->AdaptStatus, pSRB->SRBStatus);
    }
    printk ("DC390: Status of last IRQ (DMA/SC/Int/IRQ): %08x\n", dc390_laststatus);
    printk ("DC390: Register dump: SCSI block:\n");
    printk ("DC390: XferCnt  Cmd Stat IntS IRQS FFIS Ctl1 Ctl2 Ctl3 Ctl4\n");
    printk ("DC390:  %06x   %02x   %02x   %02x",
	    DC390_read8(CtcReg_Low) + (DC390_read8(CtcReg_Mid) << 8) + (DC390_read8(CtcReg_High) << 16),
	    DC390_read8(ScsiCmd), DC390_read8(Scsi_Status), DC390_read8(Intern_State));
    printk ("   %02x   %02x   %02x   %02x   %02x   %02x\n",
	    DC390_read8(INT_Status), DC390_read8(Current_Fifo), DC390_read8(CtrlReg1),
	    DC390_read8(CtrlReg2), DC390_read8(CtrlReg3), DC390_read8(CtrlReg4));
    DC390_write32 (DMA_ScsiBusCtrl, WRT_ERASE_DMA_STAT | EN_INT_ON_PCI_ABORT);
    if (DC390_read8(Current_Fifo) & 0x1f)
      {
	printk ("DC390: FIFO:");
	while (DC390_read8(Current_Fifo) & 0x1f) printk (" %02x", DC390_read8(ScsiFifo));
	printk ("\n");
      }
    printk ("DC390: Register dump: DMA engine:\n");
    printk ("DC390: Cmd   STrCnt    SBusA    WrkBC    WrkAC Stat SBusCtrl\n");
    printk ("DC390:  %02x %08x %08x %08x %08x   %02x %08x\n",
	    DC390_read8(DMA_Cmd), DC390_read32(DMA_XferCnt), DC390_read32(DMA_XferAddr),
	    DC390_read32(DMA_Wk_ByteCntr), DC390_read32(DMA_Wk_AddrCntr),
	    DC390_read8(DMA_Status), DC390_read32(DMA_ScsiBusCtrl));
    DC390_write32 (DMA_ScsiBusCtrl, EN_INT_ON_PCI_ABORT);

    pdev = pACB->pdev;
    pci_read_config_word(pdev, PCI_STATUS, &pstat);
    printk ("DC390: Register dump: PCI Status: %04x\n", pstat);
    printk ("DC390: In case of driver trouble read Documentation/scsi/tmscsim.txt\n");
}


static int DC390_abort(struct scsi_cmnd *cmd)
{
	struct dc390_acb *pACB = (struct dc390_acb*) cmd->device->host->hostdata;
	struct dc390_dcb *pDCB = (struct dc390_dcb*) cmd->device->hostdata;
	struct dc390_srb *pSRB, *psrb;

	printk("DC390: Abort command (pid %li, Device %02i-%02i)\n",
	       cmd->pid, cmd->device->id, cmd->device->lun);

	pSRB = pDCB->pWaitingSRB;
	if (!pSRB)
		goto on_going;

	/* Now scan Waiting queue */
	if (pSRB->pcmd != cmd) {
		psrb = pSRB;
		if (!(psrb->pNextSRB))
			goto on_going;

		while (psrb->pNextSRB->pcmd != cmd) {
			psrb = psrb->pNextSRB;
			if (!(psrb->pNextSRB) || psrb == pSRB)
				goto on_going;
		}

		pSRB = psrb->pNextSRB;
		psrb->pNextSRB = pSRB->pNextSRB;
		if (pSRB == pDCB->pWaitLast)
			pDCB->pWaitLast = psrb;
	} else
		pDCB->pWaitingSRB = pSRB->pNextSRB;

	dc390_Free_insert(pACB, pSRB);
	pDCB->WaitSRBCnt--;
	INIT_LIST_HEAD((struct list_head*)&cmd->SCp);

	return SUCCESS;

on_going:
	/* abort() is too stupid for already sent commands at the moment. 
	 * If it's called we are in trouble anyway, so let's dump some info 
	 * into the syslog at least. (KG, 98/08/20,99/06/20) */
	dc390_dumpinfo(pACB, pDCB, pSRB);

	pDCB->DCBFlag |= ABORT_DEV_;
	printk(KERN_INFO "DC390: Aborted pid %li\n", cmd->pid);

	return FAILED;
}


static void dc390_ResetDevParam( struct dc390_acb* pACB )
{
    struct dc390_dcb *pDCB, *pdcb;

    pDCB = pACB->pLinkDCB;
    if (! pDCB) return;
    pdcb = pDCB;
    do
    {
	pDCB->SyncMode &= ~SYNC_NEGO_DONE;
	pDCB->SyncPeriod = 0;
	pDCB->SyncOffset = 0;
	pDCB->TagMask = 0;
	pDCB->CtrlR3 = FAST_CLK;
	pDCB->CtrlR4 &= NEGATE_REQACKDATA | CTRL4_RESERVED | NEGATE_REQACK;
	pDCB->CtrlR4 |= pACB->glitch_cfg;
	pDCB = pDCB->pNextDCB;
    }
    while( pdcb != pDCB );
    pACB->ACBFlag &= ~(RESET_DEV | RESET_DONE | RESET_DETECT);

}

static int DC390_bus_reset (struct scsi_cmnd *cmd)
{
	struct dc390_acb*    pACB = (struct dc390_acb*) cmd->device->host->hostdata;
	u8   bval;

	del_timer (&pACB->Waiting_Timer);

	bval = DC390_read8(CtrlReg1) | DIS_INT_ON_SCSI_RST;
	DC390_write8(CtrlReg1, bval);	/* disable IRQ on bus reset */

	pACB->ACBFlag |= RESET_DEV;
	dc390_ResetSCSIBus(pACB);

	dc390_ResetDevParam(pACB);
	udelay(1000);
	pACB->pScsiHost->last_reset = jiffies + 3*HZ/2 
		+ HZ * dc390_eepromBuf[pACB->AdapterIndex][EE_DELAY];
    
	DC390_write8(ScsiCmd, CLEAR_FIFO_CMD);
	DC390_read8(INT_Status);		/* Reset Pending INT */

	dc390_DoingSRB_Done(pACB, cmd);

	pACB->pActiveDCB = NULL;
	pACB->ACBFlag = 0;

	bval = DC390_read8(CtrlReg1) & ~DIS_INT_ON_SCSI_RST;
	DC390_write8(CtrlReg1, bval);	/* re-enable interrupt */

	dc390_Waiting_process(pACB);

	return SUCCESS;
}

#include "scsiiom.c"

/***********************************************************************
 * Function : static void dc390_updateDCB()
 *
 * Purpose :  Set the configuration dependent DCB parameters
 ***********************************************************************/

static void dc390_updateDCB (struct dc390_acb* pACB, struct dc390_dcb* pDCB)
{
  pDCB->SyncMode &= EN_TAG_QUEUEING | SYNC_NEGO_DONE /*| EN_ATN_STOP*/;
  if (pDCB->DevMode & TAG_QUEUEING_) {
	//if (pDCB->SyncMode & EN_TAG_QUEUEING) pDCB->MaxCommand = pACB->TagMaxNum;
  } else {
	pDCB->SyncMode &= ~EN_TAG_QUEUEING;
	pDCB->MaxCommand = 1;
  }

  if( pDCB->DevMode & SYNC_NEGO_ )
	pDCB->SyncMode |= SYNC_ENABLE;
  else {
	pDCB->SyncMode &= ~(SYNC_NEGO_DONE | SYNC_ENABLE);
	pDCB->SyncOffset &= ~0x0f;
  }

  //if (! (pDCB->DevMode & EN_DISCONNECT_)) pDCB->SyncMode &= ~EN_ATN_STOP; 

  pDCB->CtrlR1 = pACB->pScsiHost->this_id;
  if( pDCB->DevMode & PARITY_CHK_ )
	pDCB->CtrlR1 |= PARITY_ERR_REPO;
}  

/**
 * dc390_slave_alloc - Called by the scsi mid layer to tell us about a new
 * scsi device that we need to deal with.
 *
 * @scsi_device: The new scsi device that we need to handle.
 */
static int dc390_slave_alloc(struct scsi_device *scsi_device)
{
	struct dc390_acb *pACB = (struct dc390_acb*) scsi_device->host->hostdata;
	struct dc390_dcb *pDCB, *pDCB2 = 0;
	uint id = scsi_device->id;
	uint lun = scsi_device->lun;

	pDCB = kmalloc(sizeof(struct dc390_dcb), GFP_KERNEL);
	if (!pDCB)
		return -ENOMEM;
	memset(pDCB, 0, sizeof(struct dc390_dcb));

	if (!pACB->DCBCnt++) {
		pACB->pLinkDCB = pDCB;
		pACB->pDCBRunRobin = pDCB;
	} else {
		pACB->pLastDCB->pNextDCB = pDCB;
	}
   
	pDCB->pNextDCB = pACB->pLinkDCB;
	pACB->pLastDCB = pDCB;

	pDCB->pDCBACB = pACB;
	pDCB->TargetID = id;
	pDCB->TargetLUN = lun;
	pDCB->MaxCommand = 1;

	/*
	 * Some values are for all LUNs: Copy them 
	 * In a clean way: We would have an own structure for a SCSI-ID 
	 */
	if (lun && (pDCB2 = dc390_findDCB(pACB, id, 0))) {
		pDCB->DevMode = pDCB2->DevMode;
		pDCB->SyncMode = pDCB2->SyncMode;
		pDCB->SyncPeriod = pDCB2->SyncPeriod;
		pDCB->SyncOffset = pDCB2->SyncOffset;
		pDCB->NegoPeriod = pDCB2->NegoPeriod;
      
		pDCB->CtrlR3 = pDCB2->CtrlR3;
		pDCB->CtrlR4 = pDCB2->CtrlR4;
		pDCB->Inquiry7 = pDCB2->Inquiry7;
	} else {
		u8 index = pACB->AdapterIndex;
		PEEprom prom = (PEEprom) &dc390_eepromBuf[index][id << 2];

		pDCB->DevMode = prom->EE_MODE1;
		pDCB->NegoPeriod =
			(dc390_clock_period1[prom->EE_SPEED] * 25) >> 2;
		pDCB->CtrlR3 = FAST_CLK;
		pDCB->CtrlR4 = pACB->glitch_cfg | CTRL4_RESERVED;
		if (dc390_eepromBuf[index][EE_MODE2] & ACTIVE_NEGATION)
			pDCB->CtrlR4 |= NEGATE_REQACKDATA | NEGATE_REQACK;
	}

	dc390_updateDCB(pACB, pDCB);

	pACB->scan_devices = 1;
	scsi_device->hostdata = pDCB;
	return 0;
}

/**
 * dc390_slave_destroy - Called by the scsi mid layer to tell us about a
 * device that is going away.
 *
 * @scsi_device: The scsi device that we need to remove.
 */
static void dc390_slave_destroy(struct scsi_device *scsi_device)
{
	struct dc390_acb* pACB = (struct dc390_acb*) scsi_device->host->hostdata;
	struct dc390_dcb* pDCB = (struct dc390_dcb*) scsi_device->hostdata;
	struct dc390_dcb* pPrevDCB = pACB->pLinkDCB;

	pACB->scan_devices = 0;

	BUG_ON(pDCB->GoingSRBCnt > 1);
	
	if (pDCB == pACB->pLinkDCB) {
		if (pACB->pLastDCB == pDCB) {
			pDCB->pNextDCB = NULL;
			pACB->pLastDCB = NULL;
		}
		pACB->pLinkDCB = pDCB->pNextDCB;
	} else {
		while (pPrevDCB->pNextDCB != pDCB)
			pPrevDCB = pPrevDCB->pNextDCB;
		pPrevDCB->pNextDCB = pDCB->pNextDCB;
		if (pDCB == pACB->pLastDCB)
			pACB->pLastDCB = pPrevDCB;
	}

	if (pDCB == pACB->pActiveDCB)
		pACB->pActiveDCB = NULL;
	if (pDCB == pACB->pLinkDCB)
		pACB->pLinkDCB = pDCB->pNextDCB;
	if (pDCB == pACB->pDCBRunRobin)
		pACB->pDCBRunRobin = pDCB->pNextDCB;
	kfree(pDCB); 
	
	pACB->DCBCnt--;
}

static int dc390_slave_configure(struct scsi_device *scsi_device)
{
	struct dc390_acb* pACB = (struct dc390_acb*) scsi_device->host->hostdata;
	pACB->scan_devices = 0;
	return 0;
}

static struct scsi_host_template driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= "tmscsim", 
	.name			= DC390_BANNER " V" DC390_VERSION,
	.slave_alloc		= dc390_slave_alloc,
	.slave_configure	= dc390_slave_configure,
	.slave_destroy		= dc390_slave_destroy,
	.queuecommand		= DC390_queuecommand,
	.eh_abort_handler	= DC390_abort,
	.eh_bus_reset_handler	= DC390_bus_reset,
	.can_queue		= 42,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 16,
	.use_clustering		= DISABLE_CLUSTERING,
};

/***********************************************************************
 * Functions for access to DC390 EEPROM
 * and some to emulate it
 *
 **********************************************************************/


static void __devinit dc390_EnDisableCE(u8 mode, struct pci_dev *pdev, u8 *regval)
{
    u8 bval;

    bval = 0;
    if(mode == ENABLE_CE)
	*regval = 0xc0;
    else
	*regval = 0x80;
    pci_write_config_byte(pdev, *regval, bval);
    if(mode == DISABLE_CE)
        pci_write_config_byte(pdev, *regval, bval);
    udelay(160);
}


/* Override EEprom values with explicitly set values */
static void __devinit dc390_EEprom_Override (u8 index)
{
    u8 *ptr = (u8 *) dc390_eepromBuf[index];
    u8 id;
    
    /* Adapter Settings */
    if (tmscsim[0] != -2)
	ptr[EE_ADAPT_SCSI_ID] = (u8)tmscsim[0];	/* Adapter ID */
    if (tmscsim[3] != -2)
	ptr[EE_MODE2] = (u8)tmscsim[3];
    if (tmscsim[5] != -2)
	ptr[EE_DELAY] = tmscsim[5];			/* Reset delay */
    if (tmscsim[4] != -2)
	ptr[EE_TAG_CMD_NUM] = (u8)tmscsim[4];	/* Tagged Cmds */
    
    /* Device Settings */
    for (id = 0; id < MAX_SCSI_ID; id++)
    {
	if (tmscsim[2] != -2)
		ptr[id<<2] = (u8)tmscsim[2];		/* EE_MODE1 */
	if (tmscsim[1] != -2)
		ptr[(id<<2) + 1] = (u8)tmscsim[1];	/* EE_Speed */
    }
}

/* Handle "-1" case */
static void __devinit dc390_check_for_safe_settings (void)
{
	if (tmscsim[0] == -1 || tmscsim[0] > 15) /* modules-2.0.0 passes -1 as string */
	{
		tmscsim[0] = 7; tmscsim[1] = 4;
		tmscsim[2] = 0x09; tmscsim[3] = 0x0f;
		tmscsim[4] = 2; tmscsim[5] = 10;
		printk (KERN_INFO "DC390: Using safe settings.\n");
	}
}


static int __initdata tmscsim_def[] = {7, 0 /* 10MHz */,
		PARITY_CHK_ | SEND_START_ | EN_DISCONNECT_
		| SYNC_NEGO_ | TAG_QUEUEING_,
		MORE2_DRV | GREATER_1G | RST_SCSI_BUS | ACTIVE_NEGATION
		/* | NO_SEEK */
# ifdef CONFIG_SCSI_MULTI_LUN
		| LUN_CHECK
# endif
		, 3 /* 16 Tags per LUN */, 1 /* s delay after Reset */ };

/* Copy defaults over set values where missing */
static void __devinit dc390_fill_with_defaults (void)
{
	int i;
	PARSEDEBUG(printk(KERN_INFO "DC390: setup %08x %08x %08x %08x %08x %08x\n", tmscsim[0],\
			  tmscsim[1], tmscsim[2], tmscsim[3], tmscsim[4], tmscsim[5]));
	for (i = 0; i < 6; i++)
	{
		if (tmscsim[i] < 0 || tmscsim[i] > 255)
			tmscsim[i] = tmscsim_def[i];
	}
	/* Sanity checks */
	if (tmscsim[0] >   7) tmscsim[0] =   7;
	if (tmscsim[1] >   7) tmscsim[1] =   4;
	if (tmscsim[4] >   5) tmscsim[4] =   4;
	if (tmscsim[5] > 180) tmscsim[5] = 180;
}

static void __devinit dc390_EEpromOutDI(struct pci_dev *pdev, u8 *regval, u8 Carry)
{
    u8 bval;

    bval = 0;
    if(Carry)
    {
	bval = 0x40;
	*regval = 0x80;
	pci_write_config_byte(pdev, *regval, bval);
    }
    udelay(160);
    bval |= 0x80;
    pci_write_config_byte(pdev, *regval, bval);
    udelay(160);
    bval = 0;
    pci_write_config_byte(pdev, *regval, bval);
    udelay(160);
}


static u8 __devinit dc390_EEpromInDO(struct pci_dev *pdev)
{
    u8 bval;

    pci_write_config_byte(pdev, 0x80, 0x80);
    udelay(160);
    pci_write_config_byte(pdev, 0x80, 0x40);
    udelay(160);
    pci_read_config_byte(pdev, 0x00, &bval);
    if(bval == 0x22)
	return(1);
    else
	return(0);
}


static u16 __devinit dc390_EEpromGetData1(struct pci_dev *pdev)
{
    u8 i;
    u8 carryFlag;
    u16 wval;

    wval = 0;
    for(i=0; i<16; i++)
    {
	wval <<= 1;
	carryFlag = dc390_EEpromInDO(pdev);
	wval |= carryFlag;
    }
    return(wval);
}


static void __devinit dc390_Prepare(struct pci_dev *pdev, u8 *regval, u8 EEpromCmd)
{
    u8 i,j;
    u8 carryFlag;

    carryFlag = 1;
    j = 0x80;
    for(i=0; i<9; i++)
    {
	dc390_EEpromOutDI(pdev, regval, carryFlag);
	carryFlag = (EEpromCmd & j) ? 1 : 0;
	j >>= 1;
    }
}


static void __devinit dc390_ReadEEprom(struct pci_dev *pdev, u16 *ptr)
{
    u8   regval,cmd;
    u8   i;

    cmd = EEPROM_READ;
    for(i=0; i<0x40; i++)
    {
	dc390_EnDisableCE(ENABLE_CE, pdev, &regval);
	dc390_Prepare(pdev, &regval, cmd++);
	*ptr++ = dc390_EEpromGetData1(pdev);
	dc390_EnDisableCE(DISABLE_CE, pdev, &regval);
    }
}


static void __devinit dc390_interpret_delay (u8 index)
{
    char interpd [] = {1,3,5,10,16,30,60,120};
    dc390_eepromBuf[index][EE_DELAY] = interpd [dc390_eepromBuf[index][EE_DELAY]];
}

static u8 __devinit dc390_CheckEEpromCheckSum(struct pci_dev *pdev, u8 index)
{
    u8  i;
    char  EEbuf[128];
    u16 wval, *ptr = (u16 *)EEbuf;

    dc390_ReadEEprom(pdev, ptr);
    memcpy (dc390_eepromBuf[index], EEbuf, EE_ADAPT_SCSI_ID);
    memcpy (&dc390_eepromBuf[index][EE_ADAPT_SCSI_ID], 
	    &EEbuf[REAL_EE_ADAPT_SCSI_ID], EE_LEN - EE_ADAPT_SCSI_ID);
    dc390_interpret_delay (index);
    
    wval = 0;
    for(i=0; i<0x40; i++, ptr++)
	wval += *ptr;
    return (wval == 0x1234 ? 0 : 1);
}

static void __devinit dc390_init_hw(struct dc390_acb *pACB, u8 index)
{
	struct Scsi_Host *shost = pACB->pScsiHost;
	u8 dstate;

	/* Disable SCSI bus reset interrupt */
	DC390_write8(CtrlReg1, DIS_INT_ON_SCSI_RST | shost->this_id);

	if (pACB->Gmode2 & RST_SCSI_BUS) {
		dc390_ResetSCSIBus(pACB);
		udelay(1000);
		shost->last_reset = jiffies + HZ/2 +
			HZ * dc390_eepromBuf[pACB->AdapterIndex][EE_DELAY];
	}

	pACB->ACBFlag = 0;

	/* Reset Pending INT */
	DC390_read8(INT_Status);
	
	/* 250ms selection timeout */
	DC390_write8(Scsi_TimeOut, SEL_TIMEOUT);
	
	/* Conversion factor = 0 , 40MHz clock */
	DC390_write8(Clk_Factor, CLK_FREQ_40MHZ);
	
	/* NOP cmd - clear command register */
	DC390_write8(ScsiCmd, NOP_CMD);
	
	/* Enable Feature and SCSI-2 */
	DC390_write8(CtrlReg2, EN_FEATURE+EN_SCSI2_CMD);
	
	/* Fast clock */
	DC390_write8(CtrlReg3, FAST_CLK);

	/* Negation */
	DC390_write8(CtrlReg4, pACB->glitch_cfg | /* glitch eater */
		(dc390_eepromBuf[index][EE_MODE2] & ACTIVE_NEGATION) ?
		 NEGATE_REQACKDATA : 0);
	
	/* Clear Transfer Count High: ID */
	DC390_write8(CtcReg_High, 0);
	DC390_write8(DMA_Cmd, DMA_IDLE_CMD);
	DC390_write8(ScsiCmd, CLEAR_FIFO_CMD);
	DC390_write32(DMA_ScsiBusCtrl, EN_INT_ON_PCI_ABORT);

	dstate = DC390_read8(DMA_Status);
	DC390_write8(DMA_Status, dstate);
}

static int __devinit dc390_probe_one(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	struct dc390_acb *pACB;
	struct Scsi_Host *shost;
	unsigned long io_port;
	int error = -ENODEV, i;

	if (pci_enable_device(pdev))
		goto out;

	pci_set_master(pdev);

	error = -ENOMEM;
	shost = scsi_host_alloc(&driver_template, sizeof(struct dc390_acb));
	if (!shost)
		goto out_disable_device;

	pACB = (struct dc390_acb *)shost->hostdata;
	memset(pACB, 0, sizeof(struct dc390_acb));

	if (dc390_CheckEEpromCheckSum(pdev, dc390_adapterCnt)) {
		int speed;
		printk(KERN_INFO "DC390_init: No EEPROM found! Trying default settings ...\n");
		dc390_check_for_safe_settings();
		dc390_fill_with_defaults();
		dc390_EEprom_Override(dc390_adapterCnt);
		speed = dc390_clock_speed[tmscsim[1]];
		printk(KERN_INFO "DC390: Used defaults: AdaptID=%i, SpeedIdx=%i (%i.%i MHz),"
		       " DevMode=0x%02x, AdaptMode=0x%02x, TaggedCmnds=%i (%i), DelayReset=%is\n", 
		       tmscsim[0], tmscsim[1], speed/10, speed%10,
		       (u8)tmscsim[2], (u8)tmscsim[3], tmscsim[4], 2 << (tmscsim[4]), tmscsim[5]);
	} else {
		dc390_check_for_safe_settings();
		dc390_EEprom_Override(dc390_adapterCnt);
	}

	io_port = pci_resource_start(pdev, 0);

	shost->can_queue = MAX_CMD_QUEUE;
	shost->cmd_per_lun = MAX_CMD_PER_LUN;
	shost->this_id = dc390_eepromBuf[dc390_adapterCnt][EE_ADAPT_SCSI_ID];
	shost->io_port = io_port;
	shost->n_io_port = 0x80;
	shost->irq = pdev->irq;
	shost->base = io_port;
	shost->unique_id = io_port;
	shost->last_reset = jiffies;
	
	pACB->pScsiHost = shost;
	pACB->IOPortBase = (u16) io_port;
	pACB->IRQLevel = pdev->irq;
	
	shost->max_id = 8;

	if (shost->max_id - 1 ==
	    dc390_eepromBuf[dc390_adapterCnt][EE_ADAPT_SCSI_ID])
		shost->max_id--;

	if (dc390_eepromBuf[dc390_adapterCnt][EE_MODE2] & LUN_CHECK)
		shost->max_lun = 8;
	else
		shost->max_lun = 1;

	pACB->pFreeSRB = pACB->SRB_array;
	pACB->SRBCount = MAX_SRB_CNT;
	pACB->AdapterIndex = dc390_adapterCnt;
	pACB->TagMaxNum =
		2 << dc390_eepromBuf[dc390_adapterCnt][EE_TAG_CMD_NUM];
	pACB->Gmode2 = dc390_eepromBuf[dc390_adapterCnt][EE_MODE2];

	for (i = 0; i < pACB->SRBCount-1; i++)
		pACB->SRB_array[i].pNextSRB = &pACB->SRB_array[i+1];
	pACB->SRB_array[pACB->SRBCount-1].pNextSRB = NULL;
	pACB->pTmpSRB = &pACB->TmpSRB;

	pACB->sel_timeout = SEL_TIMEOUT;
	pACB->glitch_cfg = EATER_25NS;
	pACB->pdev = pdev;
	init_timer(&pACB->Waiting_Timer);

	if (!request_region(io_port, shost->n_io_port, "tmscsim")) {
		printk(KERN_ERR "DC390: register IO ports error!\n");
		goto out_host_put;
	}

	/* Reset Pending INT */
	DC390_read8_(INT_Status, io_port);

	if (request_irq(pdev->irq, do_DC390_Interrupt, SA_SHIRQ,
				"tmscsim", pACB)) {
		printk(KERN_ERR "DC390: register IRQ error!\n");
		goto out_release_region;
	}

	dc390_init_hw(pACB, dc390_adapterCnt);
	
	dc390_adapterCnt++;

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_free_irq;
	scsi_scan_host(shost);
	return 0;

 out_free_irq:
	free_irq(pdev->irq, pACB);
 out_release_region:
	release_region(io_port, shost->n_io_port);
 out_host_put:
	scsi_host_put(shost);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

/**
 * dc390_remove_one - Called to remove a single instance of the adapter.
 *
 * @dev: The PCI device to remove.
 */
static void __devexit dc390_remove_one(struct pci_dev *dev)
{
	struct Scsi_Host *scsi_host = pci_get_drvdata(dev);
	unsigned long iflags;
	struct dc390_acb* pACB = (struct dc390_acb*) scsi_host->hostdata;
	u8 bval;

	scsi_remove_host(scsi_host);

	spin_lock_irqsave(scsi_host->host_lock, iflags);
	pACB->ACBFlag = RESET_DEV;
	bval = DC390_read8(CtrlReg1) | DIS_INT_ON_SCSI_RST;
	DC390_write8 (CtrlReg1, bval);	/* disable interrupt */
	if (pACB->Gmode2 & RST_SCSI_BUS)
		dc390_ResetSCSIBus(pACB);
	spin_unlock_irqrestore(scsi_host->host_lock, iflags);

	del_timer_sync(&pACB->Waiting_Timer);

	free_irq(scsi_host->irq, pACB);
	release_region(scsi_host->io_port, scsi_host->n_io_port);

	pci_disable_device(dev);
	scsi_host_put(scsi_host);
	pci_set_drvdata(dev, NULL);
}

static struct pci_device_id tmscsim_pci_tbl[] = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD53C974,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ }
};
MODULE_DEVICE_TABLE(pci, tmscsim_pci_tbl);

static struct pci_driver dc390_driver = {
	.name           = "tmscsim",
	.id_table       = tmscsim_pci_tbl,
	.probe          = dc390_probe_one,
	.remove         = __devexit_p(dc390_remove_one),
};

static int __init dc390_module_init(void)
{
	return pci_module_init(&dc390_driver);
}

static void __exit dc390_module_exit(void)
{
	pci_unregister_driver(&dc390_driver);
}

module_init(dc390_module_init);
module_exit(dc390_module_exit);

#ifndef MODULE
static int __init dc390_setup (char *str)
{	
	int ints[8],i, im;

	get_options(str, ARRAY_SIZE(ints), ints);
	im = ints[0];

	if (im > 6) {
		printk (KERN_NOTICE "DC390: ignore extra params!\n");
		im = 6;
	}

	for (i = 0; i < im; i++)
		tmscsim[i] = ints[i+1];
	/* dc390_checkparams (); */
	return 1;
}

__setup("tmscsim=", dc390_setup);
#endif
