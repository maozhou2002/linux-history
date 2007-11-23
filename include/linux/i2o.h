#ifndef _I2O_H
#define _I2O_H

/*
 *	Tunable parameters first
 */

/* How many different OSM's are we allowing */ 
#define MAX_I2O_MODULES		64
/* How many controllers are we allowing */
#define MAX_I2O_CONTROLLERS	32


#ifdef __KERNEL__   /* ioctl stuff only thing exported to users */

/*
 *	I2O Interface Objects
 */

#include <linux/notifier.h>
#include <asm/atomic.h>

/*
 * message structures
 */

struct i2o_message
{
	u32	version_size;
	u32	function_addr;
	u32	initiator_context;
	/* List follows */
};

/**************************************************************************
 * HRT related constants and structures
 **************************************************************************/
#define I2O_BUS_LOCAL	0
#define I2O_BUS_ISA	1
#define I2O_BUS_EISA	2
#define I2O_BUS_MCA	3
#define I2O_BUS_PCI	4
#define I2O_BUS_PCMCIA	5
#define I2O_BUS_NUBUS	6
#define I2O_BUS_CARDBUS	7
#define I2O_BUS_UNKNOWN	0x80

typedef struct _i2o_pci_bus {
	u8 PciFunctionNumber;
	u8 PciDeviceNumber;
	u8 PciBusNumber;
	u8 reserved;
	u16 PciVendorID;
	u16 PciDeviceID;
} i2o_pci_bus, *pi2o_pci_bus;

typedef struct _i2o_local_bus {
	u16 LbBaseIOPort;
	u16 reserved;
	u32 LbBaseMemoryAddress;
} i2o_local_bus, *pi2o_local_bus;

typedef struct _i2o_isa_bus {
	u16 IsaBaseIOPort;
	u8 CSN;
	u8 reserved;
	u32 IsaBaseMemoryAddress;
} i2o_isa_bus, *pi2o_isa_bus;

typedef struct _i2o_eisa_bus_info {
	u16 EisaBaseIOPort;
	u8 reserved;
	u8 EisaSlotNumber;
	u32 EisaBaseMemoryAddress;
} i2o_eisa_bus, *pi2o_eisa_bus;

typedef struct _i2o_mca_bus {
	u16 McaBaseIOPort;
	u8 reserved;
	u8 McaSlotNumber;
	u32 McaBaseMemoryAddress;
} i2o_mca_bus, *pi2o_mca_bus;

typedef struct _i2o_other_bus {
	u16 BaseIOPort;
	u16 reserved;
	u32 BaseMemoryAddress;
} i2o_other_bus, *pi2o_other_bus;


typedef struct _i2o_hrt_entry {
	u32 adapter_id;
	u32 parent_tid:12;
	u32 state:4;
	u32 bus_num:8;
	u32 bus_type:8;
	union {
		i2o_pci_bus pci_bus;
		i2o_local_bus local_bus;
		i2o_isa_bus isa_bus;
		i2o_eisa_bus eisa_bus;
		i2o_mca_bus mca_bus;
		i2o_other_bus other_bus;
	} bus;
} i2o_hrt_entry, *pi2o_hrt_entry;

typedef struct _i2o_hrt {
	u16 num_entries;
	u8 entry_len;
	u8 hrt_version;
	u32 change_ind;
	i2o_hrt_entry hrt_entry[1];
} i2o_hrt, *pi2o_hrt;

typedef struct _i2o_lct_entry {
	u32 entry_size:16;
	u32 tid:12;
	u32 reserved:4;
	u32 change_ind;
	u32 device_flags;
	u32 class_id;
	u32 sub_class;
	u32 user_tid:12;
	u32 parent_tid:12;
	u32 bios_info:8;
	u8 identity_tag[8];
	u32 event_capabilities;
} i2o_lct_entry, *pi2o_lct_entry;

typedef struct _i2o_lct {
	u32 table_size:16;
	u32 boot_tid:12;
	u32 lct_ver:4;
	u32 iop_flags;
	u32 current_change_ind;
	i2o_lct_entry lct_entry[1];
} i2o_lct, *pi2o_lct;


/*
 *	Each I2O device entity has one or more of these. There is one
 *	per device. *FIXME* how to handle multiple types on one unit.
 */
 
struct i2o_device
{
	int class;		/* Block, Net, SCSI etc (from spec) */
	int subclass;		/* eth, fddi, tr etc (from spec)   */
	int id;			/* I2O ID assigned by the controller */
	int parent;		/* Parent device */
	int flags;		/* Control flags */
	int i2oversion;		/* I2O version supported. Actually there
				 * should be high and low version */
	struct proc_dir_entry* proc_entry;	/* /proc dir */
	struct i2o_driver *owner;		/* Owning device */
	struct i2o_controller *controller;	/* Controlling IOP */
	struct i2o_device *next;	/* Chain */
	char dev_name[8];		/* linux /dev name if available */
};

/*
 *	Resource data for each PCI I2O controller
 */	 	

struct i2o_pci
{
	int irq;
};

/*
 *	Each I2O controller has one of these objects
 */
 
struct i2o_controller
{
	char name[16];
	int unit;
	int status;				/* I2O status */
	int i2oversion;
	int type;
#define I2O_TYPE_PCI		0x01		/* PCI I2O controller */	
	struct notifier_block *event_notifer;	/* Events */
	atomic_t users;
	struct i2o_device *devices;		/* I2O device chain */
	struct i2o_controller *next;		/* Controller chain */
	volatile u32 *post_port;		/* Messaging ports */
	volatile u32 *reply_port;
	volatile u32 *irq_mask;			/* Interrupt port */
	u32 *lct;
	u32 *hrt;
	u32 mem_offset;				/* MFA offset */
	u32 mem_phys;				/* MFA physical */
	u32 priv_mem;
	u32 priv_mem_size;
	u32 priv_io;
	u32 priv_io_size;

	struct proc_dir_entry* proc_entry;	/* /proc dir */

	union
	{					/* Bus information */
		struct i2o_pci pci;
	} bus;
	void (*destructor)(struct i2o_controller *);			/* Bus specific destructor */
	int (*bind)(struct i2o_controller *, struct i2o_device *);	/* Bus specific attach/detach */
	int (*unbind)(struct i2o_controller *, struct i2o_device *);
	void *page_frame;		/* Message buffers */
	int inbound_size;		/* Inbound queue size */
};

struct i2o_handler
{
	void (*reply)(struct i2o_handler *, struct i2o_controller *, struct i2o_message *);
	char *name;
	int context;		/* Low 8 bits of the transaction info */
	/* User data follows */
};

/*
 *	Messenger inlines
 */

extern inline u32 I2O_POST_READ32(struct i2o_controller *c)
{
	return *c->post_port;
}

extern inline void I2O_POST_WRITE32(struct i2o_controller *c, u32 Val)
{
	*c->post_port = Val;
}


extern inline u32 I2O_REPLY_READ32(struct i2o_controller *c)
{
	return *c->reply_port;
}

extern inline void I2O_REPLY_WRITE32(struct i2o_controller *c, u32 Val)
{
	*c->reply_port= Val;
}
 

extern inline u32 I2O_IRQ_READ32(struct i2o_controller *c)
{
	return *c->irq_mask;
}

extern inline void I2O_IRQ_WRITE32(struct i2o_controller *c, u32 Val)
{
	*c->irq_mask = Val;
}


extern inline void i2o_post_message(struct i2o_controller *c, u32 m)
{
	/* The second line isnt spurious - thats forcing PCI posting */
	I2O_POST_WRITE32(c,m);
	(void) I2O_IRQ_READ32(c);
}

extern inline void i2o_flush_reply(struct i2o_controller *c, u32 m)
{
	I2O_REPLY_WRITE32(c,m);
}


struct i2o_controller *i2o_controller_chain;

extern int i2o_quiesce_controller(struct i2o_controller *);
extern int i2o_clear_controller(struct i2o_controller *);
extern int i2o_install_controller(struct i2o_controller *);
extern int i2o_delete_controller(struct i2o_controller *);
extern int i2o_activate_controller(struct i2o_controller *);
extern void i2o_unlock_controller(struct i2o_controller *);
extern struct i2o_controller *i2o_find_controller(int);
extern int i2o_num_controllers;

extern int i2o_install_handler(struct i2o_handler *);
extern int i2o_remove_handler(struct i2o_handler *);

extern int i2o_install_device(struct i2o_controller *, struct i2o_device *);
extern int i2o_delete_device(struct i2o_device *);
extern int i2o_claim_device(struct i2o_device *, struct i2o_driver *);
extern int i2o_release_device(struct i2o_device *);

extern int i2o_post_this(struct i2o_controller *, int, u32 *, int);
extern int i2o_post_wait(struct i2o_controller *, int, u32 *, int, int *, int);
extern int i2o_issue_claim(struct i2o_controller *, int, int, int, int *);

extern int i2o_query_scalar(struct i2o_controller *, int, int, int, int, 
			void *, int, int *);
extern int i2o_set_scalar(struct i2o_controller *, int, int, int, int, 
			void *, int, int *);

extern int i2o_query_table(int, struct i2o_controller *, int, int, int, int, 
			void *, int, void *, int, int *);
extern int i2o_clear_table(struct i2o_controller *, int, int, int, int *); 
extern int i2o_row_add_table(struct i2o_controller *, int, int, int, int,
			void *, int, int *);
extern int i2o_row_delete_table(struct i2o_controller *, int, int, int, int,
			void *, int, int *);

extern void i2o_run_queue(struct i2o_controller *);
extern void i2o_report_status(const char *, const char *, u32 *);

extern const char *i2o_get_class_name(int);


/*
 *	I2O classes / subclasses
 */

/*  Class ID and Code Assignments
 *  (LCT.ClassID.Version field)
 */
#define    I2O_CLASS_VERSION_10                        0x00
#define    I2O_CLASS_VERSION_11                        0x01

/*  Class code names
 *  (from v1.5 Table 6-1 Class Code Assignments.)
 */
 
#define    I2O_CLASS_EXECUTIVE                         0x000
#define    I2O_CLASS_DDM                               0x001
#define    I2O_CLASS_RANDOM_BLOCK_STORAGE              0x010
#define    I2O_CLASS_SEQUENTIAL_STORAGE                0x011
#define    I2O_CLASS_LAN                               0x020
#define    I2O_CLASS_WAN                               0x030
#define    I2O_CLASS_FIBRE_CHANNEL_PORT                0x040
#define    I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL          0x041
#define    I2O_CLASS_SCSI_PERIPHERAL                   0x051
#define    I2O_CLASS_ATE_PORT                          0x060
#define    I2O_CLASS_ATE_PERIPHERAL                    0x061
#define    I2O_CLASS_FLOPPY_CONTROLLER                 0x070
#define    I2O_CLASS_FLOPPY_DEVICE                     0x071
#define    I2O_CLASS_BUS_ADAPTER_PORT                  0x080
#define    I2O_CLASS_PEER_TRANSPORT_AGENT              0x090
#define    I2O_CLASS_PEER_TRANSPORT                    0x091

/*  Rest of 0x092 - 0x09f reserved for peer-to-peer classes
 */
 
#define    I2O_CLASS_MATCH_ANYCLASS                    0xffffffff

/*  Subclasses
 */

#define    I2O_SUBCLASS_i960                           0x001
#define    I2O_SUBCLASS_HDM                            0x020
#define    I2O_SUBCLASS_ISM                            0x021
 
/* Operation functions */

#define I2O_PARAMS_FIELD_GET	0x0001
#define I2O_PARAMS_LIST_GET	0x0002
#define I2O_PARAMS_MORE_GET	0x0003
#define I2O_PARAMS_SIZE_GET	0x0004
#define I2O_PARAMS_TABLE_GET	0x0005
#define I2O_PARAMS_FIELD_SET	0x0006
#define I2O_PARAMS_LIST_SET	0x0007
#define I2O_PARAMS_ROW_ADD	0x0008
#define I2O_PARAMS_ROW_DELETE	0x0009
#define I2O_PARAMS_TABLE_CLEAR	0x000A

/*
 *	I2O serial number conventions / formats
 *	(circa v1.5)
 */

#define    I2O_SNFORMAT_UNKNOWN                        0
#define    I2O_SNFORMAT_BINARY                         1
#define    I2O_SNFORMAT_ASCII                          2
#define    I2O_SNFORMAT_UNICODE                        3
#define    I2O_SNFORMAT_LAN48_MAC                      4
#define    I2O_SNFORMAT_WAN                            5

/* Plus new in v2.0 (Yellowstone pdf doc)
 */

#define    I2O_SNFORMAT_LAN64_MAC                      6
#define    I2O_SNFORMAT_DDM                            7
#define    I2O_SNFORMAT_IEEE_REG64                     8
#define    I2O_SNFORMAT_IEEE_REG128                    9
#define    I2O_SNFORMAT_UNKNOWN2                       0xff

/* Transaction Reply Lists (TRL) Control Word structure */

#define TRL_SINGLE_FIXED_LENGTH		0x00
#define TRL_SINGLE_VARIABLE_LENGTH	0x40
#define TRL_MULTIPLE_FIXED_LENGTH	0x80

/*
 *	Messaging API values
 */
 
#define	I2O_CMD_ADAPTER_ASSIGN		0xB3
#define	I2O_CMD_ADAPTER_READ		0xB2
#define	I2O_CMD_ADAPTER_RELEASE		0xB5
#define	I2O_CMD_BIOS_INFO_SET		0xA5
#define	I2O_CMD_BOOT_DEVICE_SET		0xA7
#define	I2O_CMD_CONFIG_VALIDATE		0xBB
#define	I2O_CMD_CONN_SETUP		0xCA
#define	I2O_CMD_DDM_DESTROY		0xB1
#define	I2O_CMD_DDM_ENABLE		0xD5
#define	I2O_CMD_DDM_QUIESCE		0xC7
#define	I2O_CMD_DDM_RESET		0xD9
#define	I2O_CMD_DDM_SUSPEND		0xAF
#define	I2O_CMD_DEVICE_ASSIGN		0xB7
#define	I2O_CMD_DEVICE_RELEASE		0xB9
#define	I2O_CMD_HRT_GET			0xA8
#define	I2O_CMD_ADAPTER_CLEAR		0xBE
#define	I2O_CMD_ADAPTER_CONNECT		0xC9
#define	I2O_CMD_ADAPTER_RESET		0xBD
#define	I2O_CMD_LCT_NOTIFY		0xA2
#define	I2O_CMD_OUTBOUND_INIT		0xA1
#define	I2O_CMD_PATH_ENABLE		0xD3
#define	I2O_CMD_PATH_QUIESCE		0xC5
#define	I2O_CMD_PATH_RESET		0xD7
#define	I2O_CMD_STATIC_MF_CREATE	0xDD
#define	I2O_CMD_STATIC_MF_RELEASE	0xDF
#define	I2O_CMD_STATUS_GET		0xA0
#define	I2O_CMD_SW_DOWNLOAD		0xA9
#define	I2O_CMD_SW_UPLOAD		0xAB
#define	I2O_CMD_SW_REMOVE		0xAD
#define	I2O_CMD_SYS_ENABLE		0xD1
#define	I2O_CMD_SYS_MODIFY		0xC1
#define	I2O_CMD_SYS_QUIESCE		0xC3
#define	I2O_CMD_SYS_TAB_SET		0xA3

#define I2O_CMD_UTIL_NOP		0x00
#define I2O_CMD_UTIL_ABORT		0x01
#define I2O_CMD_UTIL_CLAIM		0x09
#define I2O_CMD_UTIL_RELEASE		0x0B
#define I2O_CMD_UTIL_PARAMS_GET		0x06
#define I2O_CMD_UTIL_PARAMS_SET		0x05
#define I2O_CMD_UTIL_EVT_REGISTER	0x13
#define I2O_CMD_UTIL_ACK		0x14
#define I2O_CMD_UTIL_CONFIG_DIALOG	0x10
#define I2O_CMD_UTIL_DEVICE_RESERVE	0x0D
#define I2O_CMD_UTIL_DEVICE_RELEASE	0x0F
#define I2O_CMD_UTIL_LOCK		0x17
#define I2O_CMD_UTIL_LOCK_RELEASE	0x19
#define I2O_CMD_UTIL_REPLY_FAULT_NOTIFY	0x15

#define I2O_CMD_SCSI_EXEC		0x81
#define I2O_CMD_SCSI_ABORT		0x83
#define I2O_CMD_SCSI_BUSRESET		0x27

#define I2O_CMD_BLOCK_READ		0x30
#define I2O_CMD_BLOCK_WRITE		0x31
#define I2O_CMD_BLOCK_CFLUSH		0x37
#define I2O_CMD_BLOCK_MLOCK		0x49
#define I2O_CMD_BLOCK_MUNLOCK		0x4B
#define I2O_CMD_BLOCK_MMOUNT		0x41
#define I2O_CMD_BLOCK_MEJECT		0x43

#define I2O_PRIVATE_MSG			0xFF

/*
 *	Init Outbound Q status 
 */
 
#define I2O_CMD_OUTBOUND_INIT_IN_PROGRESS	0x01
#define I2O_CMD_OUTBOUND_INIT_REJECTED		0x02
#define I2O_CMD_OUTBOUND_INIT_FAILED		0x03
#define I2O_CMD_OUTBOUND_INIT_COMPLETE		0x04

/*
 *	I2O Get Status State values 
 */

#define	ADAPTER_STATE_INITIALIZING		0x01
#define	ADAPTER_STATE_RESET			0x02
#define	ADAPTER_STATE_HOLD			0x04
#define ADAPTER_STATE_READY			0x05
#define	ADAPTER_STATE_OPERATIONAL		0x08
#define	ADAPTER_STATE_FAILED			0x10
#define	ADAPTER_STATE_FAULTED			0x11
	
/* I2O API function return values */

#define I2O_RTN_NO_ERROR			0
#define I2O_RTN_NOT_INIT			1
#define I2O_RTN_FREE_Q_EMPTY			2
#define I2O_RTN_TCB_ERROR			3
#define I2O_RTN_TRANSACTION_ERROR		4
#define I2O_RTN_ADAPTER_ALREADY_INIT		5
#define I2O_RTN_MALLOC_ERROR			6
#define I2O_RTN_ADPTR_NOT_REGISTERED		7
#define I2O_RTN_MSG_REPLY_TIMEOUT		8
#define I2O_RTN_NO_STATUS			9
#define I2O_RTN_NO_FIRM_VER			10
#define	I2O_RTN_NO_LINK_SPEED			11

/* Reply message status defines for all messages */

#define I2O_REPLY_STATUS_SUCCESS                    	0x00
#define I2O_REPLY_STATUS_ABORT_DIRTY                	0x01
#define I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER     	0x02
#define	I2O_REPLY_STATUS_ABORT_PARTIAL_TRANSFER		0x03
#define	I2O_REPLY_STATUS_ERROR_DIRTY			0x04
#define	I2O_REPLY_STATUS_ERROR_NO_DATA_TRANSFER		0x05
#define	I2O_REPLY_STATUS_ERROR_PARTIAL_TRANSFER		0x06
#define	I2O_REPLY_STATUS_PROCESS_ABORT_DIRTY		0x08
#define	I2O_REPLY_STATUS_PROCESS_ABORT_NO_DATA_TRANSFER	0x09
#define	I2O_REPLY_STATUS_PROCESS_ABORT_PARTIAL_TRANSFER	0x0A
#define	I2O_REPLY_STATUS_TRANSACTION_ERROR		0x0B
#define	I2O_REPLY_STATUS_PROGRESS_REPORT		0x80

/* Status codes and Error Information for Parameter functions */

#define I2O_PARAMS_STATUS_SUCCESS		0x00
#define I2O_PARAMS_STATUS_BAD_KEY_ABORT		0x01
#define I2O_PARAMS_STATUS_BAD_KEY_CONTINUE   	0x02
#define I2O_PARAMS_STATUS_BUFFER_FULL		0x03
#define I2O_PARAMS_STATUS_BUFFER_TOO_SMALL	0x04
#define I2O_PARAMS_STATUS_FIELD_UNREADABLE	0x05
#define I2O_PARAMS_STATUS_FIELD_UNWRITEABLE	0x06
#define I2O_PARAMS_STATUS_INSUFFICIENT_FIELDS	0x07
#define I2O_PARAMS_STATUS_INVALID_GROUP_ID	0x08
#define I2O_PARAMS_STATUS_INVALID_OPERATION	0x09
#define I2O_PARAMS_STATUS_NO_KEY_FIELD		0x0A
#define I2O_PARAMS_STATUS_NO_SUCH_FIELD		0x0B
#define I2O_PARAMS_STATUS_NON_DYNAMIC_GROUP	0x0C
#define I2O_PARAMS_STATUS_OPERATION_ERROR	0x0D
#define I2O_PARAMS_STATUS_SCALAR_ERROR		0x0E
#define I2O_PARAMS_STATUS_TABLE_ERROR		0x0F
#define I2O_PARAMS_STATUS_WRONG_GROUP_TYPE	0x10

/* DetailedStatusCode defines for Executive, DDM, Util and Transaction error
 * messages: Table 3-2 Detailed Status Codes.*/

#define I2O_DSC_SUCCESS                        0x0000
#define I2O_DSC_BAD_KEY                        0x0002
#define I2O_DSC_TCL_ERROR                      0x0003
#define I2O_DSC_REPLY_BUFFER_FULL              0x0004
#define I2O_DSC_NO_SUCH_PAGE                   0x0005
#define I2O_DSC_INSUFFICIENT_RESOURCE_SOFT     0x0006
#define I2O_DSC_INSUFFICIENT_RESOURCE_HARD     0x0007
#define I2O_DSC_CHAIN_BUFFER_TOO_LARGE         0x0009
#define I2O_DSC_UNSUPPORTED_FUNCTION           0x000A
#define I2O_DSC_DEVICE_LOCKED                  0x000B
#define I2O_DSC_DEVICE_RESET                   0x000C
#define I2O_DSC_INAPPROPRIATE_FUNCTION         0x000D
#define I2O_DSC_INVALID_INITIATOR_ADDRESS      0x000E
#define I2O_DSC_INVALID_MESSAGE_FLAGS          0x000F
#define I2O_DSC_INVALID_OFFSET                 0x0010
#define I2O_DSC_INVALID_PARAMETER              0x0011
#define I2O_DSC_INVALID_REQUEST                0x0012
#define I2O_DSC_INVALID_TARGET_ADDRESS         0x0013
#define I2O_DSC_MESSAGE_TOO_LARGE              0x0014
#define I2O_DSC_MESSAGE_TOO_SMALL              0x0015
#define I2O_DSC_MISSING_PARAMETER              0x0016
#define I2O_DSC_TIMEOUT                        0x0017
#define I2O_DSC_UNKNOWN_ERROR                  0x0018
#define I2O_DSC_UNKNOWN_FUNCTION               0x0019
#define I2O_DSC_UNSUPPORTED_VERSION            0x001A
#define I2O_DSC_DEVICE_BUSY                    0x001B
#define I2O_DSC_DEVICE_NOT_AVAILABLE           0x001C
 
/* Message header defines for VersionOffset */
#define I2OVER15	0x0001
#define I2OVER20	0x0002
/* Default is 1.5, FIXME: Need support for both 1.5 and 2.0 */
#define I2OVERSION	I2OVER15
#define SGL_OFFSET_0    I2OVERSION
#define SGL_OFFSET_4    (0x0040 | I2OVERSION)
#define SGL_OFFSET_5    (0x0050 | I2OVERSION)
#define SGL_OFFSET_6    (0x0060 | I2OVERSION)
#define SGL_OFFSET_7    (0x0070 | I2OVERSION)
#define SGL_OFFSET_8    (0x0080 | I2OVERSION)
#define SGL_OFFSET_9    (0x0090 | I2OVERSION)
#define SGL_OFFSET_10   (0x00A0 | I2OVERSION)

#define TRL_OFFSET_5    (0x0050 | I2OVERSION)
#define TRL_OFFSET_6    (0x0060 | I2OVERSION)

 /* msg header defines for MsgFlags */
#define MSG_STATIC	0x0100
#define MSG_64BIT_CNTXT	0x0200
#define MSG_MULTI_TRANS	0x1000
#define MSG_FAIL	0x2000
#define MSG_LAST	0x4000
#define MSG_REPLY	0x8000

 /* minimum size msg */
#define THREE_WORD_MSG_SIZE	0x00030000
#define FOUR_WORD_MSG_SIZE	0x00040000
#define FIVE_WORD_MSG_SIZE	0x00050000
#define SIX_WORD_MSG_SIZE	0x00060000
#define SEVEN_WORD_MSG_SIZE	0x00070000
#define EIGHT_WORD_MSG_SIZE	0x00080000
#define NINE_WORD_MSG_SIZE	0x00090000
#define TEN_WORD_MSG_SIZE	0x000A0000
#define I2O_MESSAGE_SIZE(x)	((x)<<16)


/* Special TID Assignments */

#define ADAPTER_TID		0
#define HOST_TID		1

#define MSG_FRAME_SIZE		128
#define NMBR_MSG_FRAMES		128

#define MSG_POOL_SIZE		16384

#define I2O_POST_WAIT_OK	1
#define I2O_POST_WAIT_TIMEOUT	-ETIMEDOUT

#endif /* __KERNEL__ */

#include <asm/ioctl.h>

/*
 * I2O Control IOCTLs and structures
 */
#define I2O_MAGIC_NUMBER	'i'
#define I2OGETIOPS		_IO(I2O_MAGIC_NUMBER,0)
#define I2OHRTGET		_IO(I2O_MAGIC_NUMBER,1)
#define I2OLCTGET		_IO(I2O_MAGIC_NUMBER,2)
#define I2OPARMSET		_IO(I2O_MAGIC_NUMBER,3)
#define I2OPARMGET		_IO(I2O_MAGIC_NUMBER,4)
#define I2OSWDL			_IO(I2O_MAGIC_NUMBER,5)
#define I2OSWUL			_IO(I2O_MAGIC_NUMBER,6)
#define I2OSWDEL		_IO(I2O_MAGIC_NUMBER,7)
#define I2OHTML			_IO(I2O_MAGIC_NUMBER,8)

/* On hold until we figure this out
#define I2OEVTREG		_IO(I2O_MAGIC_NUMBER,9)
#define I2OEVTCLR		_IO(I2O_MAGIC_NUMBER,10)
#define I2OEVTGET		_IO(I2O_MAGIC_NUMBER,11)
 */

struct i2o_cmd_hrtlct
{
	unsigned int iop;  /* IOP unit number */
	void  *resbuf;  /* Buffer for result */
	unsigned int *reslen;  /* Buffer length in bytes */
};


struct i2o_cmd_psetget
{
	unsigned int iop;      /* IOP unit number */
	unsigned int tid;      /* Target device TID */
	void  *opbuf;   /* Operation List buffer */
	unsigned int   oplen;    /* Operation List buffer length in bytes */
	void  *resbuf;  /* Result List buffer */
	unsigned int  *reslen;  /* Result List buffer length in bytes */
};

struct i2o_sw_xfer
{
	unsigned int iop;	/* IOP unit number */
	unsigned char dl_flags;	/* DownLoadFlags field */
	unsigned char sw_type;	/* Software type */
	unsigned int sw_id;     /* Software ID */
	void  *buf;      	/* Pointer to software buffer */
	unsigned int *swlen;    /* Length of software data */
	unsigned int *maxfrag;  /* Maximum fragment count */
        unsigned int *curfrag;  /* Current fragment count */
};

struct i2o_html
{
	unsigned int iop;      /* IOP unit number */
	unsigned int tid;      /* Target device ID */
	unsigned int page;     /* HTML page */
	void  *resbuf;  /* Buffer for reply HTML page */
	unsigned int *reslen;  /* Length in bytes of reply buffer */
	void  *qbuf;    /* Pointer to HTTP query string */
	unsigned int qlen;     /* Length in bytes of query string buffer */
};

#endif
