/*
 * $Id: serio.c,v 1.15 2002/01/22 21:12:03 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  The Serio abstraction module
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/suspend.h>
#include <linux/slab.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Serio abstraction core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(serio_interrupt);
EXPORT_SYMBOL(serio_register_port);
EXPORT_SYMBOL(serio_unregister_port);
EXPORT_SYMBOL(serio_register_device);
EXPORT_SYMBOL(serio_unregister_device);
EXPORT_SYMBOL(serio_open);
EXPORT_SYMBOL(serio_close);
EXPORT_SYMBOL(serio_rescan);

struct serio_event {
	int type;
	struct serio *serio;
	struct list_head node;
};

static LIST_HEAD(serio_list);
static LIST_HEAD(serio_dev_list);
static LIST_HEAD(serio_event_list);
static int serio_pid;

static void serio_find_dev(struct serio *serio)
{
	struct serio_dev *dev;

	list_for_each_entry(dev, &serio_dev_list, node) {
		if (serio->dev)
			break;
		if (dev->connect)
			dev->connect(serio, dev);
	}
}

#define SERIO_RESCAN	1

static DECLARE_WAIT_QUEUE_HEAD(serio_wait);
static DECLARE_COMPLETION(serio_exited);

void serio_handle_events(void)
{
	struct list_head *node, *next;
	struct serio_event *event;

	list_for_each_safe(node, next, &serio_event_list) {
		event = container_of(node, struct serio_event, node);	

		switch (event->type) {
			case SERIO_RESCAN :
				if (event->serio->dev && event->serio->dev->disconnect)
					event->serio->dev->disconnect(event->serio);
				serio_find_dev(event->serio);
				break;
			default:
				break;
		}
		list_del_init(node);
		kfree(event);
	}
}

static int serio_thread(void *nothing)
{
	lock_kernel();
	daemonize("kseriod");
	allow_signal(SIGTERM);

	do {
		serio_handle_events();
		wait_event_interruptible(serio_wait, !list_empty(&serio_event_list)); 
		if (current->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);
	} while (!signal_pending(current));

	printk(KERN_DEBUG "serio: kseriod exiting\n");

	unlock_kernel();
	complete_and_exit(&serio_exited, 0);
}

void serio_rescan(struct serio *serio)
{
	struct serio_event *event;

	if (!(event = kmalloc(sizeof(struct serio_event), GFP_ATOMIC)))
		return;

	event->type = SERIO_RESCAN;
	event->serio = serio;

	list_add_tail(&event->node, &serio_event_list);
	wake_up(&serio_wait);
}

void serio_interrupt(struct serio *serio, unsigned char data, unsigned int flags, struct pt_regs *regs)
{       
        if (serio->dev && serio->dev->interrupt) 
                serio->dev->interrupt(serio, data, flags, regs);
	else 
		if (!flags)
			serio_rescan(serio);
}

void serio_register_port(struct serio *serio)
{
	list_add_tail(&serio->node, &serio_list);
	serio_find_dev(serio);
}

void serio_unregister_port(struct serio *serio)
{
	list_del_init(&serio->node);
	if (serio->dev && serio->dev->disconnect)
		serio->dev->disconnect(serio);
}

void serio_register_device(struct serio_dev *dev)
{
	struct serio *serio;
	list_add_tail(&dev->node, &serio_dev_list);
	list_for_each_entry(serio, &serio_list, node)
		if (!serio->dev && dev->connect)
			dev->connect(serio, dev);
}

void serio_unregister_device(struct serio_dev *dev)
{
	struct serio *serio;

	list_del_init(&dev->node);

	list_for_each_entry(serio, &serio_list, node) {
		if (serio->dev == dev && dev->disconnect)
			dev->disconnect(serio);
		serio_find_dev(serio);
	}
}

int serio_open(struct serio *serio, struct serio_dev *dev)
{
	if (serio->open(serio))
		return -1;
	serio->dev = dev;
	return 0;
}

void serio_close(struct serio *serio)
{
	serio->close(serio);
	serio->dev = NULL;
}

int serio_init(void)
{
	int pid;

	pid = kernel_thread(serio_thread, NULL,
		CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

	if (!pid) {
		printk(KERN_WARNING "serio: Failed to start kseriod\n");
		return -1;
	}

	serio_pid = pid;

	return 0;
}

void serio_exit(void)
{
	kill_proc(serio_pid, SIGTERM, 1);
	wait_for_completion(&serio_exited);
}

module_init(serio_init);
module_exit(serio_exit);
