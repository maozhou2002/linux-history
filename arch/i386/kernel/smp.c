/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	This code is released under the GNU public license version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>

#include <asm/mtrr.h>
#include <asm/pgalloc.h>

/*
 *	Some notes on x86 processor bugs affecting SMP operation:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP erratas are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP erratas are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP erratas are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deassering IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesnt have the work
 *	arounds. Basically thats so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

/* The 'big kernel lock' */
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

struct tlb_state cpu_tlbstate[NR_CPUS] = {[0 ... NR_CPUS-1] = { &init_mm, 0 }};

/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static unsigned int cached_APIC_ICR;
static unsigned int cached_APIC_ICR2;

/*
 * Caches reserved bits, APIC reads are (mildly) expensive
 * and force otherwise unnecessary CPU synchronization.
 *
 * (We could cache other APIC registers too, but these are the
 * main ones used in RL.)
 */
#define slow_ICR (apic_read(APIC_ICR) & ~0xFDFFF)
#define slow_ICR2 (apic_read(APIC_ICR2) & 0x00FFFFFF)

void cache_APIC_registers (void)
{
	cached_APIC_ICR = slow_ICR;
	cached_APIC_ICR2 = slow_ICR2;
	mb();
}

static inline unsigned int __get_ICR (void)
{
#if FORCE_READ_AROUND_WRITE
	/*
	 * Wait for the APIC to become ready - this should never occur. It's
	 * a debugging check really.
	 */
	int count = 0;
	unsigned int cfg;

	while (count < 1000)
	{
		cfg = slow_ICR;
		if (!(cfg&(1<<12)))
			return cfg;
		printk("CPU #%d: ICR still busy [%08x]\n",
					smp_processor_id(), cfg);
		irq_err_count++;
		count++;
		udelay(10);
	}
	printk("CPU #%d: previous IPI still not cleared after 10mS\n",
			smp_processor_id());
	return cfg;
#else
	return cached_APIC_ICR;
#endif
}

static inline unsigned int __get_ICR2 (void)
{
#if FORCE_READ_AROUND_WRITE
	return slow_ICR2;
#else
	return cached_APIC_ICR2;
#endif
}

#define LOGICAL_DELIVERY 1

static inline int __prepare_ICR (unsigned int shortcut, int vector)
{
	unsigned int cfg;

	cfg = __get_ICR();
	cfg |= APIC_DEST_DM_FIXED|shortcut|vector
#if LOGICAL_DELIVERY
		|APIC_DEST_LOGICAL
#endif
		;

	return cfg;
}

static inline int __prepare_ICR2 (unsigned int mask)
{
	unsigned int cfg;

	cfg = __get_ICR2();
#if LOGICAL_DELIVERY
	cfg |= SET_APIC_DEST_FIELD(mask);
#else
	cfg |= SET_APIC_DEST_FIELD(mask);
#endif

	return cfg;
}

static inline void __send_IPI_shortcut(unsigned int shortcut, int vector)
{
	unsigned int cfg;
/*
 * Subtle. In the case of the 'never do double writes' workaround we
 * have to lock out interrupts to be safe. Otherwise it's just one
 * single atomic write to the APIC, no need for cli/sti.
 */
#if FORCE_READ_AROUND_WRITE
	unsigned long flags;

	__save_flags(flags);
	__cli();
#endif

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
#if FORCE_READ_AROUND_WRITE
	__restore_flags(flags);
#endif
}

static inline void send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then
	 * we get an APIC send error if we try to broadcast.
	 * thus we have to avoid sending IPIs in this case.
	 */
	if (smp_num_cpus > 1)
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

void send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector);
}

static inline void send_IPI_mask(int mask, int vector)
{
	unsigned long cfg;
#if FORCE_READ_AROUND_WRITE
	unsigned long flags;

	__save_flags(flags);
	__cli();
#endif

	/*
	 * prepare target chip field
	 */

	cfg = __prepare_ICR2(mask);
	apic_write(APIC_ICR2, cfg);

	/*
	 * program the ICR 
	 */
	cfg = __prepare_ICR(0, vector);
	
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
#if FORCE_READ_AROUND_WRITE
	__restore_flags(flags);
#endif
}

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfreds@colorfullife.com>
 */

static volatile unsigned long flush_cpumask;
static struct mm_struct * flush_mm;
static unsigned long flush_va;
static spinlock_t tlbstate_lock = SPIN_LOCK_UNLOCKED;
#define FLUSH_ALL	0xffffffff

static void inline leave_mm (unsigned long cpu)
{
	if (cpu_tlbstate[cpu].state == TLBSTATE_OK)
		BUG();
	clear_bit(cpu, &cpu_tlbstate[cpu].active_mm->cpu_vm_mask);
	cpu_tlbstate[cpu].state = TLBSTATE_OLD;
}

/*
 *
 * The flush IPI assumes that a thread switch happens in this order:
 * 1) set_bit(cpu, &new_mm->cpu_vm_mask);
 * 2) update cpu_tlbstate
 * [now the cpu can accept tlb flush request for the new mm]
 * 3) change cr3 (if required, or flush local tlb,...)
 * 4) clear_bit(cpu, &old_mm->cpu_vm_mask);
 * 5) switch %%esp, ie current
 *
 * The interrupt must handle 2 special cases:
 * - cr3 is changed before %%esp, ie. it cannot use current->{active_,}mm.
 * - the cpu performs speculative tlb reads, i.e. even if the cpu only
 *   runs in kernel space, the cpu could load tlb entries for user space
 *   pages.
 *
 * The good news is that cpu_tlbstate is local to each cpu, no
 * write/read ordering problems.
 */

/*
 * TLB flush IPI:
 *
 * 1) Flush the tlb entries if the cpu uses the mm that's being flushed.
 * 2) Leave the mm if we are in the lazy tlb mode.
 * We cannot call mmdrop() because we are in interrupt context, 
 * instead update cpu_tlbstate.
 */

asmlinkage void smp_invalidate_interrupt (void)
{
	unsigned long cpu = smp_processor_id();

	if (!test_bit(cpu, &flush_cpumask))
		BUG();
	if (flush_mm == cpu_tlbstate[cpu].active_mm) {
		if (cpu_tlbstate[cpu].state == TLBSTATE_OK) {
			if (flush_va == FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(flush_va);
		} else
			leave_mm(cpu);
	}
	ack_APIC_irq();
	clear_bit(cpu, &flush_cpumask);
}

static void flush_tlb_others (unsigned long cpumask, struct mm_struct *mm,
						unsigned long va)
{
	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - we do not send IPIs to not-yet booted CPUs.
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	if (!cpumask)
		BUG();
	if ((cpumask & cpu_online_map) != cpumask)
		BUG();
	if (cpumask & (1 << smp_processor_id()))
		BUG();
	if (!mm)
		BUG();

	/*
	 * i'm not happy about this global shared spinlock in the
	 * MM hot path, but we'll see how contended it is.
	 * Temporarily this turns IRQs off, so that lockups are
	 * detected by the NMI watchdog.
	 */
	spin_lock(&tlbstate_lock);
	
	flush_mm = mm;
	flush_va = va;
	atomic_set_mask(cpumask, &flush_cpumask);
	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(cpumask, INVALIDATE_TLB_VECTOR);

	while (flush_cpumask)
		/* nothing. lockup detection does not belong here */;

	flush_mm = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}
	
void flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~(1 << smp_processor_id());

	local_flush_tlb();
	if (cpu_mask)
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);
}

void flush_tlb_mm (struct mm_struct * mm)
{
	unsigned long cpu_mask = mm->cpu_vm_mask & ~(1 << smp_processor_id());

	if (current->active_mm == mm) {
		if (current->mm)
			local_flush_tlb();
		else
			leave_mm(smp_processor_id());
	}
	if (cpu_mask)
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);
}

void flush_tlb_page(struct vm_area_struct * vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~(1 << smp_processor_id());

	if (current->active_mm == mm) {
		if(current->mm)
			__flush_tlb_one(va);
		 else
		 	leave_mm(smp_processor_id());
	}

	if (cpu_mask)
		flush_tlb_others(cpu_mask, mm, va);
}

static inline void do_flush_tlb_all_local(void)
{
	unsigned long cpu = smp_processor_id();

	__flush_tlb_all();
	if (cpu_tlbstate[cpu].state == TLBSTATE_LAZY)
		leave_mm(cpu);
}

static void flush_tlb_all_ipi(void* info)
{
	do_flush_tlb_all_local();
}

void flush_tlb_all(void)
{
	smp_call_function (flush_tlb_all_ipi,0,1,1);

	do_flush_tlb_all_local();
}

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
	send_IPI_mask(1 << cpu, RESCHEDULE_VECTOR);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 */
static volatile struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data = NULL;

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler, you may call it from a bottom half handler.
 */
{
	struct call_data_struct data;
	int ret, cpus = smp_num_cpus-1;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;

	if(cpus == 0)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock_bh(&lock);
	call_data = &data;
	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);

	/* Wait for response */
	/* FIXME: lock-up detection, backtrace on lock-up */
	while(atomic_read(&data.started) != cpus)
		barrier();

	ret = 0;
	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
	spin_unlock_bh(&lock);
	return 0;
}

static void stop_this_cpu (void * dummy)
{
	/*
	 * Remove this CPU:
	 */
	clear_bit(smp_processor_id(), &cpu_online_map);
	__cli();
	disable_local_APIC();
	if (cpu_data[smp_processor_id()].hlt_works_ok)
		for(;;) __asm__("hlt");
	for (;;);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
	smp_num_cpus = 1;

	__cli();
	disable_local_APIC();
	__sti();
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	ack_APIC_irq();
}

asmlinkage void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	ack_APIC_irq();
	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait)
		atomic_inc(&call_data->finished);
}

