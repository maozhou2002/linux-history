/* $Id: isdnl1.h,v 2.9.6.3 2001/09/23 22:24:49 kai Exp $
 *
 * Layer 1 defines
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define D_RCVBUFREADY	0
#define D_XMTBUFREADY	1
#define D_L1STATECHANGE	2
#define D_CLEARBUSY	3
#define D_RX_MON0	4
#define D_RX_MON1	5
#define D_TX_MON0	6
#define D_TX_MON1	7
#define E_RCVBUFREADY	8

#define B_RCVBUFREADY   0
#define B_XMTBUFREADY   1
#define B_CMPLREADY     2

#define B_LL_NOCARRIER	8
#define B_LL_CONNECT	9
#define B_LL_OK		10

extern void debugl1(struct IsdnCardState *cs, char *fmt, ...);
extern void DChannel_proc_xmt(struct IsdnCardState *cs);
extern void DChannel_proc_rcv(struct IsdnCardState *cs);
extern void l1_msg(struct IsdnCardState *cs, int pr, void *arg);
extern void l1_msg_b(struct PStack *st, int pr, void *arg);

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir);
#endif

static inline void
sched_b_event(struct BCState *bcs, int event)
{
	set_bit(event, &bcs->event);
	schedule_work(&bcs->work);
}

/* called with the card lock held */
static inline void
xmit_complete_b(struct BCState *bcs)
{
	skb_queue_tail(&bcs->cmpl_queue, bcs->tx_skb);
	sched_b_event(bcs, B_CMPLREADY);
	bcs->tx_skb = NULL;
}

/* called with the card lock held */
static inline void
xmit_ready_b(struct BCState *bcs)
{
	bcs->tx_skb = skb_dequeue(&bcs->squeue);
	if (bcs->tx_skb) {
		bcs->count = 0;
		set_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->cs->BC_Send_Data(bcs);
	} else {
		clear_bit(BC_FLG_BUSY, &bcs->Flag);
		sched_b_event(bcs, B_XMTBUFREADY);
	}
}

static inline void
xmit_data_req_b(struct BCState *bcs, struct sk_buff *skb)
{
	struct IsdnCardState *cs = bcs->cs;
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (bcs->tx_skb) {
		skb_queue_tail(&bcs->squeue, skb);
	} else {
		set_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->tx_skb = skb;
		bcs->count = 0;
		bcs->cs->BC_Send_Data(bcs);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}

static inline void
xmit_pull_ind_b(struct BCState *bcs, struct sk_buff *skb)
{
	struct IsdnCardState *cs = bcs->cs;
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (bcs->tx_skb) {
		WARN_ON(1);
	} else {
		set_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->tx_skb = skb;
		bcs->count = 0;
		bcs->cs->BC_Send_Data(bcs);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}

/* If busy, the PH_PULL | CONFIRM scheduling is handled under
 * the card lock by xmit_ready_b() above, so no race */
static inline void
xmit_pull_req_b(struct PStack *st, struct sk_buff *skb)
{
	struct BCState *bcs = st->l1.bcs;
	struct IsdnCardState *cs = bcs->cs;
	unsigned long flags;
	int busy = 0;

	spin_lock_irqsave(&cs->lock, flags);
	if (bcs->tx_skb) {
		set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		busy = 1;
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	if (!busy)
		L1L2(st, PH_PULL | CONFIRM, NULL);
}

/* called with the card lock held */
static inline void
xmit_restart_b(struct BCState *bcs)
{
#ifdef ERROR_STATISTIC
	bcs->err_tx++;
#endif
	if (!bcs->tx_skb) {
		WARN_ON(1);
		return;
	}
	skb_push(bcs->tx_skb, bcs->count);
	bcs->tx_cnt += bcs->count;
	bcs->count = 0;
}

/* Useful for HSCX work-alike's */
/* ---------------------------------------------------------------------- */

/* XPR - transmit pool ready */
/* called with the card lock held */
static inline void
xmit_xpr_b(struct BCState *bcs)
{
	/* current frame? */
	if (bcs->tx_skb) {
		/* last frame not done yet? */
		if (bcs->tx_skb->len) {
			bcs->cs->BC_Send_Data(bcs);
			return;
		}
		xmit_complete_b(bcs);
		bcs->count = 0;
	}
	xmit_ready_b(bcs);
}

/* XDU - transmit data underrun */
/* called with the card lock held */
static inline void
xmit_xdu_b(struct BCState *bcs, void (*reset_xmit)(struct BCState *bcs))
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_WARN)
		debugl1(cs, "HSCX %c EXIR XDU", 'A' + bcs->channel);

	if (bcs->mode == L1_MODE_TRANS) {
		cs->BC_Send_Data(bcs);
	} else {
		xmit_restart_b(bcs);
		reset_xmit(bcs);
	}
}
