/*
 * These are the public elements of the Linux kernel Rose implementation.
 * For kernel AX.25 see the file ax25.h. This file requires ax25.h for the
 * definition of the ax25_address structure.
 */

#ifndef	ROSE_KERNEL_H
#define	ROSE_KERNEL_H

#define PF_ROSE		AF_ROSE
#define ROSE_MTU	251

#define	ROSE_DEFER	1
#define ROSE_T1		2
#define	ROSE_T2		3
#define	ROSE_T3		4
#define	ROSE_IDLE	5
#define	ROSE_QBITINCL	6
#define	ROSE_HOLDBACK	7

#define	SIOCRSGCAUSE		(SIOCPROTOPRIVATE+0)
#define	SIOCRSSCAUSE		(SIOCPROTOPRIVATE+1)
#define	SIOCRSL2CALL		(SIOCPROTOPRIVATE+2)
#define	SIOCRSACCEPT		(SIOCPROTOPRIVATE+3)
#define	SIOCRSCLRRT		(SIOCPROTOPRIVATE+4)

#define	ROSE_DTE_ORIGINATED	0x00
#define	ROSE_NUMBER_BUSY	0x01
#define	ROSE_INVALID_FACILITY	0x03
#define	ROSE_NETWORK_CONGESTION	0x05
#define	ROSE_OUT_OF_ORDER	0x09
#define	ROSE_ACCESS_BARRED	0x0B
#define	ROSE_NOT_OBTAINABLE	0x0D
#define	ROSE_REMOTE_PROCEDURE	0x11
#define	ROSE_LOCAL_PROCEDURE	0x13
#define	ROSE_SHIP_ABSENT	0x39

typedef struct {
	char		rose_addr[5];
} rose_address;

struct sockaddr_rose {
	unsigned short	srose_family;
	rose_address	srose_addr;
	ax25_address	srose_call;
	int		srose_ndigis;
	ax25_address	srose_digi;
};

struct rose_route_struct {
	rose_address	address;
	unsigned short	mask;
	ax25_address	neighbour;
	char		device[16];
	unsigned char	ndigis;
	ax25_address	digipeaters[AX25_MAX_DIGIS];
};

struct rose_cause_struct {
	unsigned char	cause;
	unsigned char	diagnostic;
};

#endif