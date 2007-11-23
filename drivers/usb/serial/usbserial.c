/*
 * USB Serial Converter driver
 *
 *	(C) Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * This driver was originally based on the ACM driver by Armin Fuerst (which was 
 * based on a driver by Brad Keryan)
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 * (03/19/2000) gkh
 *	Fixed oops that could happen when device was removed while a program
 *	was talking to the device.
 *	Removed the static urbs and now all urbs are created and destroyed
 *	dynamically.
 *	Reworked the internal interface. Now everything is based on the 
 *	usb_serial_port structure instead of the larger usb_serial structure.
 *	This fixes the bug that a multiport device could not have more than
 *	one port open at one time.
 *
 * (03/17/2000) gkh
 *	Added config option for debugging messages.
 *	Added patch for keyspan pda from Brian Warner.
 *
 * (03/06/2000) gkh
 *	Added the keyspan pda code from Brian Warner <warner@lothar.com>
 *	Moved a bunch of the port specific stuff into its own structure. This
 *	is in anticipation of the true multiport devices (there's a bug if you
 *	try to access more than one port of any multiport device right now)
 *
 * (02/21/2000) gkh
 *	Made it so that any serial devices only have to specify which functions
 *	they want to overload from the generic function calls (great, 
 *	inheritance in C, in a driver, just what I wanted...)
 *	Added support for set_termios and ioctl function calls. No drivers take
 *	advantage of this yet.
 *	Removed the #ifdef MODULE, now there is no module specific code.
 *	Cleaned up a few comments in usb-serial.h that were wrong (thanks again
 *	to Miles Lott).
 *	Small fix to get_free_serial.
 *
 * (02/14/2000) gkh
 *	Removed the Belkin and Peracom functionality from the driver due to
 *	the lack of support from the vendor, and me not wanting people to 
 *	accidenatly buy the device, expecting it to work with Linux.
 *	Added read_bulk_callback and write_bulk_callback to the type structure
 *	for the needs of the FTDI and WhiteHEAT driver.
 *	Changed all reverences to FTDI to FTDI_SIO at the request of Bill
 *	Ryder.
 *	Changed the output urb size back to the max endpoint size to make
 *	the ftdi_sio driver have it easier, and due to the fact that it didn't
 *	really increase the speed any.
 *
 * (02/11/2000) gkh
 *	Added VISOR_FUNCTION_CONSOLE to the visor startup function. This was a
 *	patch from Miles Lott (milos@insync.net).
 *	Fixed bug with not restoring the minor range that a device grabs, if
 *	the startup function fails (thanks Miles for finding this).
 *
 * (02/05/2000) gkh
 *	Added initial framework for the Keyspan PDA serial converter so that
 *	Brian Warner has a place to put his code.
 *	Made the ezusb specific functions generic enough that different
 *	devices can use them (whiteheat and keyspan_pda both need them).
 *	Split out a whole bunch of structure and other stuff to a seperate
 *	usb-serial.h file.
 *	Made the Visor connection messages a little more understandable, now
 *	that Miles Lott (milos@insync.net) has gotten the Generic channel to
 *	work. Also made them always show up in the log file.
 * 
 * (01/25/2000) gkh
 *	Added initial framework for FTDI serial converter so that Bill Ryder
 *	has a place to put his code.
 *	Added the vendor specific info from Handspring. Now we can print out
 *	informational debug messages as well as understand what is happening.
 *
 * (01/23/2000) gkh
 *	Fixed problem of crash when trying to open a port that didn't have a
 *	device assigned to it. Made the minor node finding a little smarter,
 *	now it looks to find a continous space for the new device.
 *
 * (01/21/2000) gkh
 *	Fixed bug in visor_startup with patch from Miles Lott (milos@insync.net)
 *	Fixed get_serial_by_minor which was all messed up for multi port 
 *	devices. Fixed multi port problem for generic devices. Now the number
 *	of ports is determined by the number of bulk out endpoints for the
 *	generic device.
 *
 * (01/19/2000) gkh
 *	Removed lots of cruft that was around from the old (pre urb) driver 
 *	interface.
 *	Made the serial_table dynamic. This should save lots of memory when
 *	the number of minor nodes goes up to 256.
 *	Added initial support for devices that have more than one port. 
 *	Added more debugging comments for the Visor, and added a needed 
 *	set_configuration call.
 *
 * (01/17/2000) gkh
 *	Fixed the WhiteHEAT firmware (my processing tool had a bug)
 *	and added new debug loader firmware for it.
 *	Removed the put_char function as it isn't really needed.
 *	Added visor startup commands as found by the Win98 dump.
 * 
 * (01/13/2000) gkh
 *	Fixed the vendor id for the generic driver to the one I meant it to be.
 *
 * (01/12/2000) gkh
 *	Forget the version numbering...that's pretty useless...
 *	Made the driver able to be compiled so that the user can select which
 *	converter they want to use. This allows people who only want the Visor
 *	support to not pay the memory size price of the WhiteHEAT.
 *	Fixed bug where the generic driver (idVendor=0000 and idProduct=0000)
 *	grabbed the root hub. Not good.
 * 
 * version 0.4.0 (01/10/2000) gkh
 *	Added whiteheat.h containing the firmware for the ConnectTech WhiteHEAT
 *	device. Added startup function to allow firmware to be downloaded to
 *	a device if it needs to be.
 *	Added firmware download logic to the WhiteHEAT device.
 *	Started to add #defines to split up the different drivers for potential
 *	configuration option.
 *	
 * version 0.3.1 (12/30/99) gkh
 *      Fixed problems with urb for bulk out.
 *      Added initial support for multiple sets of endpoints. This enables
 *      the Handspring Visor to be attached successfully. Only the first
 *      bulk in / bulk out endpoint pair is being used right now.
 *
 * version 0.3.0 (12/27/99) gkh
 *	Added initial support for the Handspring Visor based on a patch from
 *	Miles Lott (milos@sneety.insync.net)
 *	Cleaned up the code a bunch and converted over to using urbs only.
 *
 * version 0.2.3 (12/21/99) gkh
 *	Added initial support for the Connect Tech WhiteHEAT converter.
 *	Incremented the number of ports in expectation of getting the
 *	WhiteHEAT to work properly (4 ports per connection).
 *	Added notification on insertion and removal of what port the
 *	device is/was connected to (and what kind of device it was).
 *
 * version 0.2.2 (12/16/99) gkh
 *	Changed major number to the new allocated number. We're legal now!
 *
 * version 0.2.1 (12/14/99) gkh
 *	Fixed bug that happens when device node is opened when there isn't a
 *	device attached to it. Thanks to marek@webdesign.no for noticing this.
 *
 * version 0.2.0 (11/10/99) gkh
 *	Split up internals to make it easier to add different types of serial 
 *	converters to the code.
 *	Added a "generic" driver that gets it's vendor and product id
 *	from when the module is loaded. Thanks to David E. Nelson (dnelson@jump.net)
 *	for the idea and sample code (from the usb scanner driver.)
 *	Cleared up any licensing questions by releasing it under the GNU GPL.
 *
 * version 0.1.2 (10/25/99) gkh
 * 	Fixed bug in detecting device.
 *
 * version 0.1.1 (10/05/99) gkh
 * 	Changed the major number to not conflict with anything else.
 *
 * version 0.1 (09/28/99) gkh
 * 	Can recognize the two different devices and start up a read from
 *	device when asked to. Writes also work. No control signals yet, this
 *	all is vendor specific data (i.e. no spec), also no control for
 *	different baud rates or other bit settings.
 *	Currently we are using the same devid as the acm driver. This needs
 *	to change.
 * 
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

/* Module information */
MODULE_AUTHOR("Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux-usb/");
MODULE_DESCRIPTION("USB Serial Driver");

#include "usb-serial.h"


/* function prototypes for a "generic" type serial converter (no flow control, not all endpoints needed) */
/* need to always compile these in, as some of the other devices use these functions as their own. */
/* if a driver does not provide a function pointer, the generic function will be called. */
static int  generic_open		(struct usb_serial_port *port, struct file *filp);
static void generic_close		(struct usb_serial_port *port, struct file *filp);
static int  generic_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static int  generic_write_room		(struct usb_serial_port *port);
static int  generic_chars_in_buffer	(struct usb_serial_port *port);
static void generic_read_bulk_callback	(struct urb *urb);
static void generic_write_bulk_callback	(struct urb *urb);


#ifdef CONFIG_USB_SERIAL_GENERIC
static __u16	vendor	= 0x05f9;
static __u16	product	= 0xffff;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");

/* All of the device info needed for the Generic Serial Converter */
static struct usb_serial_device_type generic_device = {
	name:			"Generic",
	idVendor:		&vendor,		/* use the user specified vendor id */
	idProduct:		&product,		/* use the user specified product id */
	needs_interrupt_in:	DONT_CARE,		/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,		/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,		/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
};
#endif


/* To add support for another serial converter, create a usb_serial_device_type
   structure for that device, and add it to this list, making sure that the
   last  entry is NULL. */
static struct usb_serial_device_type *usb_serial_devices[] = {
#ifdef CONFIG_USB_SERIAL_GENERIC
	&generic_device,
#endif
#ifdef CONFIG_USB_SERIAL_WHITEHEAT
	&whiteheat_fake_device,
	&whiteheat_device,
#endif
#ifdef CONFIG_USB_SERIAL_VISOR
	&handspring_device,
#endif
#ifdef CONFIG_USB_SERIAL_FTDI_SIO
	&ftdi_sio_device,
#endif
#ifdef CONFIG_USB_SERIAL_KEYSPAN_PDA
	&keyspan_pda_fake_device,
	&keyspan_pda_device,
#endif
	NULL
};



/* local function prototypes */
static int  serial_open (struct tty_struct *tty, struct file * filp);
static void serial_close (struct tty_struct *tty, struct file * filp);
static int  serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count);
static int  serial_write_room (struct tty_struct *tty);
static int  serial_chars_in_buffer (struct tty_struct *tty);
static void serial_throttle (struct tty_struct * tty);
static void serial_unthrottle (struct tty_struct * tty);
static int  serial_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg);
static void serial_set_termios (struct tty_struct *tty, struct termios * old);

static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_serial_disconnect(struct usb_device *dev, void *ptr);

static struct usb_driver usb_serial_driver = {
	name:		"serial",
	probe:		usb_serial_probe,
	disconnect:	usb_serial_disconnect,
};

static int			serial_refcount;
static struct tty_struct *	serial_tty[SERIAL_TTY_MINORS];
static struct termios *		serial_termios[SERIAL_TTY_MINORS];
static struct termios *		serial_termios_locked[SERIAL_TTY_MINORS];
static struct usb_serial	*serial_table[SERIAL_TTY_MINORS] = {NULL, };



static struct usb_serial *get_serial_by_minor (int minor)
{
	return serial_table[minor];
}


static struct usb_serial *get_free_serial (int num_ports, int *minor)
{
	struct usb_serial *serial = NULL;
	int i, j;
	int good_spot;

	dbg("get_free_serial %d", num_ports);

	*minor = 0;
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		if (serial_table[i])
			continue;

		good_spot = 1;
		for (j = 1; j <= num_ports-1; ++j)
			if (serial_table[i+j])
				good_spot = 0;
		if (good_spot == 0)
			continue;
			
		if (!(serial = kmalloc(sizeof(struct usb_serial), GFP_KERNEL))) {
			err("Out of memory");
			return NULL;
		}
		memset(serial, 0, sizeof(struct usb_serial));
		serial->magic = USB_SERIAL_MAGIC;
		serial_table[i] = serial;
		*minor = i;
		dbg("minor base = %d", *minor);
		for (i = *minor+1; (i < (*minor + num_ports)) && (i < SERIAL_TTY_MINORS); ++i)
			serial_table[i] = serial;
		return serial;
		}
	return NULL;
}


static void return_serial (struct usb_serial *serial)
{
	int i;

	dbg("return_serial");

	if (serial == NULL)
		return;

	for (i = 0; i < serial->num_ports; ++i) {
		serial_table[serial->minor + i] = NULL;
	}

	return;
}


#ifdef USES_EZUSB_FUNCTIONS
/* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */
#define CPUCS_REG    0x7F92

int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest)
{
	int result;
	unsigned char *transfer_buffer =  kmalloc (length, GFP_KERNEL);

//	dbg("ezusb_writememory %x, %d", address, length);

	if (!transfer_buffer) {
		err("ezusb_writememory: kmalloc(%d) failed.", length);
		return -ENOMEM;
	}
	memcpy (transfer_buffer, data, length);
	result = usb_control_msg (serial->dev, usb_sndctrlpipe(serial->dev, 0), bRequest, 0x40, address, 0, transfer_buffer, length, 300);
	kfree (transfer_buffer);
	return result;
}


int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit)
{
	int	response;
	dbg("ezusb_set_reset: %d", reset_bit);
	response = ezusb_writememory (serial, CPUCS_REG, &reset_bit, 1, 0xa0);
	if (response < 0) {
		err("ezusb_set_reset %d failed", reset_bit);
	}
	return response;
}

#endif	/* USES_EZUSB_FUNCTIONS */


/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	int portNumber;
	
	dbg("serial_open");

	/* initialize the pointer incase something fails */
	tty->driver_data = NULL;

	/* get the serial object associated with this tty pointer */
	serial = get_serial_by_minor (MINOR(tty->device));

	if (serial_paranoia_check (serial, "serial_open")) {
		return -ENODEV;
	}

	/* set up our port structure */
	portNumber = MINOR(tty->device) - serial->minor;
	port = &serial->port[portNumber];
	port->number = portNumber;
	port->serial = serial;
	port->magic = USB_SERIAL_PORT_MAGIC;
	
	/* make the tty driver remember our port object, and us it */
	tty->driver_data = port;
	port->tty = tty;
	 
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->open) {
		return (serial->type->open(port, filp));
	} else {
		return (generic_open(port, filp));
	}
}


static void serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;

	dbg("serial_close");

	if (port_paranoia_check (port, "serial_close")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_close")) {
		return;
	}

	dbg("serial_close port %d", port->number);
	
	if (!port->active) {
		dbg ("port not opened");
		return;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->close) {
		serial->type->close(port, filp);
	} else {
		generic_close(port, filp);
	}
}	


static int serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_write");
	
	if (port_paranoia_check (port, "serial_write")) {
		return -ENODEV;
	}
	
	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_write")) {
		return -ENODEV;
	}
	
	dbg("serial_write port %d, %d byte(s)", port->number, count);

	if (!port->active) {
		dbg ("port not opened");
		return -EINVAL;
	}
	
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->write) {
		return (serial->type->write(port, from_user, buf, count));
	} else {
		return (generic_write(port, from_user, buf, count));
	}
}


static int serial_write_room (struct tty_struct *tty) 
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_write_room");
	
	if (port_paranoia_check (port, "serial_write")) {
		return -ENODEV;
	}
	
	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_write")) {
		return -ENODEV;
	}
	
	dbg("serial_write_room port %d", port->number);
	
	if (!port->active) {
		dbg ("port not open");
		return -EINVAL;
	}
	
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->write_room) {
		return (serial->type->write_room(port));
	} else {
		return (generic_write_room(port));
	}
}


static int serial_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_chars_in_buffer");
	
	if (port_paranoia_check (port, "serial_chars_in_buffer")) {
		return -ENODEV;
	}
	
	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_chars_in_buffer")) {
		return -ENODEV;
	}
	
	if (!port->active) {
		dbg ("port not open");
		return -EINVAL;
	}
	
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->chars_in_buffer) {
		return (serial->type->chars_in_buffer(port));
	} else {
		return (generic_chars_in_buffer(port));
	}
}


static void serial_throttle (struct tty_struct * tty)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_throttle");
	
	if (port_paranoia_check (port, "serial_throttle")) {
		return;
	}
	
	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_throttle")) {
		return;
	}
	
	dbg("serial_throttle port %d", port->number);
	
	if (!port->active) {
		dbg ("port not open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->throttle) {
		serial->type->throttle(port);
	}

	return;
}


static void serial_unthrottle (struct tty_struct * tty)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_unthrottle");
	
	if (port_paranoia_check (port, "serial_unthrottle")) {
		return;
	}
	
	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_unthrottle")) {
		return;
	}
	
	dbg("serial_unthrottle port %d", port->number);
	
	if (!port->active) {
		dbg ("port not open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->unthrottle) {
		serial->type->unthrottle(port);
	}

	return;
}


static int serial_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_ioctl");
	
	if (port_paranoia_check (port, "serial_ioctl")) {
		return -ENODEV;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_ioctl")) {
		return -ENODEV;
	}
	
	dbg("serial_ioctl port %d", port->number);
	
	if (!port->active) {
		dbg ("port not open");
		return -ENODEV;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->ioctl) {
		return (serial->type->ioctl(port, file, cmd, arg));
	} else {
		return -ENOIOCTLCMD;
	}
}


static void serial_set_termios (struct tty_struct *tty, struct termios * old)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_set_termios");
	
	if (port_paranoia_check (port, "serial_set_termios")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_set_termios")) {
		return;
	}

	dbg("serial_set_termios port %d", port->number);

	if (!port->active) {
		dbg ("port not open");
		return;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->set_termios) {
		serial->type->set_termios(port, old);
	}
	
	return;
}


static void serial_break (struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = (struct usb_serial_port *) tty->driver_data;
	struct usb_serial *serial;
	
	dbg("serial_break");
	
	if (port_paranoia_check (port, "serial_break")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "serial_break")) {
		return;
	}

	dbg("serial_break port %d", port->number);

	if (!port->active) {
		dbg ("port not open");
		return;
	}

	/* pass on to the driver specific version of this function if it is
           available */
	if (serial->type->break_ctl) {
		serial->type->break_ctl(port, break_state);
	}
}



/*****************************************************************************
 * generic devices specific driver functions
 *****************************************************************************/
static int generic_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;

	dbg("generic_open port %d", port->number);

	if (port->active) {
		dbg ("device already open");
		return -EINVAL;
	}
	port->active = 1;
 
	/* if we have a bulk interrupt, start reading from it */
	if (serial->num_bulk_in) {
		/*Start reading from the device*/
		if (usb_submit_urb(port->read_urb))
			dbg("usb_submit_urb(read bulk) failed");
	}

	return (0);
}


static void generic_close (struct usb_serial_port *port, struct file * filp)
{
	struct usb_serial *serial = port->serial;

	dbg("generic_close port %d", port->number);
	
	/* shutdown any bulk reads that might be going on */
	if (serial->num_bulk_out) {
		usb_unlink_urb (port->write_urb);
	}
	if (serial->num_bulk_in) {
		usb_unlink_urb (port->read_urb);
	}

	port->active = 0;
}


static int generic_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;

	dbg("generic_serial_write port %d", port->number);

	if (count == 0) {
		dbg("write request of 0 bytes");
		return (0);
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS) {
			dbg ("already writing");
			return (0);
		}

		count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

		if (from_user) {
			copy_from_user(port->write_urb->transfer_buffer, buf, count);
		}
		else {
			memcpy (port->write_urb->transfer_buffer, buf, count);
		}  

		/* send the data out the bulk port */
		port->write_urb->transfer_buffer_length = count;

		if (usb_submit_urb(port->write_urb))
			dbg("usb_submit_urb(write bulk) failed");

		return (count);
	}
	
	/* no bulk out, so return 0 bytes written */
	return (0);
} 


static int generic_write_room (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	int room;

	dbg("generic_write_room port %d", port->number);
	
	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS)
			room = 0;
		else
			room = port->bulk_out_size;
		dbg("generic_write_room returns %d", room);
		return (room);
	}
	
	return (0);
}


static int generic_chars_in_buffer (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	dbg("generic_chars_in_buffer port %d", port->number);
	
	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS) {
			return (port->bulk_out_size);
		}
	}

	return (0);
}


static void generic_read_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty;
       	unsigned char *data = urb->transfer_buffer;
	int i;

	dbg("generic_read_bulk_callback");

	if (port_paranoia_check (port, "generic_read_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "generic_read_bulk_callback")) {
		return;
	}
	
	if (urb->status) {
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef DEBUG
	if (urb->actual_length) {
		printk (KERN_DEBUG __FILE__ ": data read - length = %d, data = ", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif

	tty = port->tty;
	if (urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			 tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	if (usb_submit_urb(urb))
		dbg("failed resubmitting read urb");

	return;
}


static void generic_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty;

	dbg("generic_write_bulk_callback");

	if (port_paranoia_check (port, "generic_write_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "generic_write_bulk_callback")) {
		return;
	}
	
	if (urb->status) {
		dbg("nonzero write bulk status received: %d", urb->status);
		return;
	}

	tty = port->tty;
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
	
	return;
}


static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_serial *serial = NULL;
	struct usb_serial_port *port;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_out_endpoint[MAX_NUM_PORTS];
	struct usb_serial_device_type *type;
	int device_num;
	int minor;
	int buffer_size;
	int i;
	char interrupt_pipe;
	char bulk_in_pipe;
	char bulk_out_pipe;
	int num_interrupt_in = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	int num_ports;
	
	/* loop through our list of known serial converters, and see if this device matches */
	device_num = 0;
	while (usb_serial_devices[device_num] != NULL) {
		type = usb_serial_devices[device_num];
		dbg ("Looking at %s Vendor id=%.4x Product id=%.4x", type->name, *(type->idVendor), *(type->idProduct));

		/* look at the device descriptor */
		if ((dev->descriptor.idVendor == *(type->idVendor)) &&
		    (dev->descriptor.idProduct == *(type->idProduct))) {

			dbg("descriptor matches...looking at the endpoints");

			/* descriptor matches, let's try to find the endpoints needed */
			interrupt_pipe = bulk_in_pipe = bulk_out_pipe = HAS_NOT;
			
			/* check out the endpoints */
			interface = &dev->actconfig->interface[ifnum].altsetting[0];
			for (i = 0; i < interface->bNumEndpoints; ++i) {
				endpoint = &interface->endpoint[i];
		
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x02)) {
					/* we found a bulk in endpoint */
					dbg("found bulk in");
					bulk_in_pipe = HAS;
					bulk_in_endpoint[num_bulk_in] = endpoint;
					++num_bulk_in;
				}

				if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
				    ((endpoint->bmAttributes & 3) == 0x02)) {
					/* we found a bulk out endpoint */
					dbg("found bulk out");
					bulk_out_pipe = HAS;
					bulk_out_endpoint[num_bulk_out] = endpoint;
					++num_bulk_out;
				}
		
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x03)) {
					/* we found a interrupt in endpoint */
					dbg("found interrupt in");
					interrupt_pipe = HAS;
					interrupt_in_endpoint[num_interrupt_in] = endpoint;
					++num_interrupt_in;
				}

			}
	
			/* verify that we found all of the endpoints that we need */
			if ((interrupt_pipe & type->needs_interrupt_in) &&
			    (bulk_in_pipe & type->needs_bulk_in) &&
			    (bulk_out_pipe & type->needs_bulk_out)) {
				/* found all that we need */
				info("%s converter detected", type->name);

#ifdef CONFIG_USB_SERIAL_GENERIC
				if (type == &generic_device)
					num_ports = num_bulk_out;
				else
#endif
					num_ports = type->num_ports;

				serial = get_free_serial (num_ports, &minor);
				if (serial == NULL) {
					err("No more free serial devices");
					return NULL;
				}
	
			       	serial->dev = dev;
				serial->type = type;
				serial->minor = minor;
				serial->num_ports = num_ports;
				serial->num_bulk_in = num_bulk_in;
				serial->num_bulk_out = num_bulk_out;
				serial->num_interrupt_in = num_interrupt_in;

				/* collect interrupt_in endpoints now, because
				   the keyspan_pda startup function needs
				   to know about them */
				for (i = 0; i < num_interrupt_in; ++i) {
					port = &serial->port[i];
					buffer_size = interrupt_in_endpoint[i]->wMaxPacketSize;
					port->interrupt_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
					if (!port->interrupt_in_buffer) {
						err("Couldn't allocate interrupt_in_buffer");
						goto probe_error;
					}
					port->interrupt_in_endpoint = interrupt_in_endpoint[i];
				}

				/* if this device type has a startup function, call it */
				if (type->startup) {
					if (type->startup (serial)) {
						return_serial (serial);
						return NULL;
					}
				}

				/* set up the endpoint information */
				for (i = 0; i < num_bulk_in; ++i) {
					port = &serial->port[i];
					port->read_urb = usb_alloc_urb (0);
					if (!port->read_urb) {
						err("No free urbs available");
						goto probe_error;
					}
					buffer_size = bulk_in_endpoint[i]->wMaxPacketSize;
					port->bulk_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
					if (!port->bulk_in_buffer) {
						err("Couldn't allocate bulk_in_buffer");
						goto probe_error;
					}
					if (serial->type->read_bulk_callback) {
						FILL_BULK_URB(port->read_urb, dev, usb_rcvbulkpipe (dev, bulk_in_endpoint[i]->bEndpointAddress),
								port->bulk_in_buffer, buffer_size, serial->type->read_bulk_callback, port);
					} else {
						FILL_BULK_URB(port->read_urb, dev, usb_rcvbulkpipe (dev, bulk_in_endpoint[i]->bEndpointAddress),
								port->bulk_in_buffer, buffer_size, generic_read_bulk_callback, port);
					}
				}

				for (i = 0; i < num_bulk_out; ++i) {
					port = &serial->port[i];
					port->write_urb = usb_alloc_urb(0);
					if (!port->write_urb) {
						err("No free urbs available");
						goto probe_error;
					}
					port->bulk_out_size = bulk_out_endpoint[i]->wMaxPacketSize;
					port->bulk_out_buffer = kmalloc (port->bulk_out_size, GFP_KERNEL);
					if (!port->bulk_out_buffer) {
						err("Couldn't allocate bulk_out_buffer");
						goto probe_error;
					}
					if (serial->type->write_bulk_callback) {
						FILL_BULK_URB(port->write_urb, dev, usb_sndbulkpipe (dev, bulk_out_endpoint[i]->bEndpointAddress),
								port->bulk_out_buffer, port->bulk_out_size, serial->type->write_bulk_callback, port);
					} else {
						FILL_BULK_URB(port->write_urb, dev, usb_sndbulkpipe (dev, bulk_out_endpoint[i]->bEndpointAddress),
								port->bulk_out_buffer, port->bulk_out_size, generic_write_bulk_callback, port);
					}
				}

#if 0 /* use this code when WhiteHEAT is up and running */
				for (i = 0; i < num_interrupt_in; ++i) {
					port = &serial->port[i];
					port->control_urb = usb_alloc_urb(0);
					if (!port->control_urb) {
						err("No free urbs available");
						goto probe_error;
					}
					buffer_size = interrupt_in_endpoint[i]->wMaxPacketSize;
					port->interrupt_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
					if (!port->interrupt_in_buffer) {
						err("Couldn't allocate interrupt_in_buffer");
						goto probe_error;
					}
					FILL_INT_URB(port->control_urb, dev, usb_rcvintpipe (dev, interrupt_in_endpoint[i]->bEndpointAddress),
							port->interrupt_in_buffer, buffer_size, serial_control_irq,
							port, interrupt_in_endpoint[i]->bInterval);
				}
#endif

				for (i = 0; i < serial->num_ports; ++i) {
					info("%s converter now attached to ttyUSB%d", type->name, serial->minor + i);
				}

				MOD_INC_USE_COUNT;

				return serial;
			} else {
				info("descriptors matched, but endpoints did not");
			}
		}

		/* look at the next type in our list */
		++device_num;
	}

probe_error:
	if (serial) {
		for (i = 0; i < num_bulk_in; ++i) {
			port = &serial->port[i];
			if (port->read_urb)
				usb_free_urb (port->read_urb);
			if (serial->port[i].bulk_in_buffer[i])
				kfree (serial->port[i].bulk_in_buffer);
		}
		for (i = 0; i < num_bulk_out; ++i) {
			port = &serial->port[i];
			if (port->write_urb)
				usb_free_urb (port->write_urb);
			if (serial->port[i].bulk_out_buffer)
				kfree (serial->port[i].bulk_out_buffer);
		}
		for (i = 0; i < num_interrupt_in; ++i) {
			port = &serial->port[i];
			if (port->control_urb)
				usb_free_urb (port->control_urb);
			if (serial->port[i].interrupt_in_buffer)
				kfree (serial->port[i].interrupt_in_buffer);
		}
		
		/* return the minor range that this device had */
		return_serial (serial);

		/* free up any memory that we allocated */
		kfree (serial);
	}
	return NULL;
}


static void usb_serial_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_serial *serial = (struct usb_serial *) ptr;
	struct usb_serial_port *port;
	int i;

	if (serial) {
		for (i = 0; i < serial->num_ports; ++i)
			serial->port[i].active = 0;

		for (i = 0; i < serial->num_bulk_in; ++i) {
			port = &serial->port[i];
			if (port->read_urb) {
				usb_unlink_urb (port->read_urb);
				usb_free_urb (port->read_urb);
			}
			if (port->bulk_in_buffer)
				kfree (port->bulk_in_buffer);
		}
		for (i = 0; i < serial->num_bulk_out; ++i) {
			port = &serial->port[i];
			if (port->write_urb) {
				usb_unlink_urb (port->write_urb);
				usb_free_urb (port->write_urb);
			}
			if (port->bulk_out_buffer)
				kfree (port->bulk_out_buffer);
		}
		for (i = 0; i < serial->num_interrupt_in; ++i) {
			port = &serial->port[i];
			if (port->control_urb) {
				usb_unlink_urb (port->control_urb);
				usb_free_urb (port->control_urb);
			}
			if (port->interrupt_in_buffer)
				kfree (port->interrupt_in_buffer);
		}

		for (i = 0; i < serial->num_ports; ++i) {
			info("%s converter now disconnected from ttyUSB%d", serial->type->name, serial->minor + i);
		}

		/* return the minor range that this device had */
		return_serial (serial);

		/* free up any memory that we allocated */
		kfree (serial);

	} else {
		info("device disconnected");
	}
	
	MOD_DEC_USE_COUNT;
}


static struct tty_driver serial_tty_driver = {
	magic:			TTY_DRIVER_MAGIC,
	driver_name:		"usb",
	name:			"ttyUSB%d",
	major:			SERIAL_TTY_MAJOR,
	minor_start:		0,
	num:			SERIAL_TTY_MINORS,
	type:			TTY_DRIVER_TYPE_SERIAL,
	subtype:		SERIAL_TYPE_NORMAL,
	flags:			TTY_DRIVER_REAL_RAW,
	refcount:		&serial_refcount,
	table:			serial_tty,
	proc_entry:		NULL,
	other:			NULL,
	termios:		serial_termios,
	termios_locked:		serial_termios_locked,
	
	open:			serial_open,
	close:			serial_close,
	write:			serial_write,
	put_char:		NULL,
	flush_chars:		NULL,
	write_room:		serial_write_room,
	ioctl:			serial_ioctl,
	set_termios:		serial_set_termios,
	set_ldisc:		NULL, 
	throttle:		serial_throttle,
	unthrottle:		serial_unthrottle,
	stop:			NULL,
	start:			NULL,
	hangup:			NULL,
	break_ctl:		serial_break,
	wait_until_sent:	NULL,
	send_xchar:		NULL,
	read_proc:		NULL,
	chars_in_buffer:	serial_chars_in_buffer,
	flush_buffer:		NULL
};


int usb_serial_init(void)
{
	int i;

	/* Initalize our global data */
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		serial_table[i] = NULL;
	}

	/* register the tty driver */
	serial_tty_driver.init_termios		= tty_std_termios;
	serial_tty_driver.init_termios.c_cflag	= B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	if (tty_register_driver (&serial_tty_driver)) {
		err("failed to register tty driver");
		return -EPERM;
	}
	
	/* register the USB driver */
	if (usb_register(&usb_serial_driver) < 0) {
		tty_unregister_driver(&serial_tty_driver);
		return -1;
	}

	info("support registered");
	return 0;
}


void usb_serial_exit(void)
{
	tty_unregister_driver(&serial_tty_driver);
	usb_deregister(&usb_serial_driver);
}


module_init(usb_serial_init);
module_exit(usb_serial_exit);


