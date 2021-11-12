/****************************************************************************
 *
 * cn_micom_fp.h
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2021 Audioniek: debugged on AM 520 HD
 *
 * Front panel driver for following CreNova models
 * Atemio AM 520 HD (Miniline B)
 * Sogno HD 800-V3 (Miniline B)
 * Opticum HD 9600 Mini (Miniline A)
 * Opticum HD 9600 PRIMA (?)
 * Opticum HD TS 9600 PRIMA (?)
 *
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
 ****************************************************************************
 *
 * Changes
 *
 * Date     By              Description
 * --------------------------------------------------------------------------
 * 20?????? ?               Initial version.
 * 20211101 Audioniek       VFDTEST added.
 *
 ****************************************************************************/
#ifndef _123_cn_micom
#define _123_cn_micom
/*
 */

extern short paramDebug;  // debug print level is zero as default (0=nothing, 1= errors, 10=some detail, 20=more detail, 50=open/close functions, 100=all)
#define TAGDEBUG "[cn_micom] "
#ifndef dprintk
#define dprintk(level, x...) \
do \
{ \
	if ((paramDebug) && (paramDebug >= level) || level == 0) \
	{ \
		printk(TAGDEBUG x); \
	} \
} while (0)
#endif

static __inline__ unsigned long p4_inl(unsigned long addr)
{
	return *(volatile unsigned long *)addr;
}

static __inline__ void p4_outl(unsigned long addr, unsigned int b)
{
	*(volatile unsigned long *)addr = b;
}

#define p2_inl	p4_inl
#define p2_outl	p4_outl

typedef struct
{
	struct file      *fp;
	int              read;
	struct semaphore sem;

} tFrontPanelOpen;

#define FRONTPANEL_MINOR_RC  1
#define LASTMINOR            2

extern tFrontPanelOpen FrontPanelOpen[LASTMINOR];

#define VFD_MAJOR            147

/* ioctl numbers ->hacky */
#define VFDDISPLAYCHARS      0xc0425a00
#define VFDBRIGHTNESS        0xc0425a03  // not supported by hardware
#define VFDDISPLAYWRITEONOFF 0xc0425a05
#define VFDDRIVERINIT        0xc0425a08
#define VFDICONDISPLAYONOFF  0xc0425a0a  // not supported by hardware

#define VFDTEST              0xc0425af0  // added by audioniek
#define VFDSETPOWERONTIME    0xc0425af6
#define VFDGETVERSION        0xc0425af7
#define VFDGETWAKEUPMODE     0xc0425af9
#define VFDGETTIME           0xc0425afa  // simulated, not supported by hardware
#define VFDSETTIME           0xc0425afb  // simulated, not supported by hardware
#define VFDSTANDBY           0xc0425afc
#define VFDGETWAKEUPTIME     0xc0425b03  // simulated, not supported by hardware

//#define MAX_MCOM_MSG         30

//#define MCU_COMM_PORT        UART_1
//#define MCU_COMM_TIMEOUT     1000

// Command and response format is:
//
// ID LL DD DD DD DD
//
// ID: Identification byte
// LL: number of ddata bytes DD that follow
// DD: data of the command

// offsets into command/response string
#define _TAG                 0  // offset for command byte in command string
#define _LEN                 1  // offset for length byte in command string
#define _VAL                 2  // offset for argument byte(s) in command string

#define cMinimumSize         3  // shortest command/response is this long

// commands sent to FP       ID    resp?
#define FP_CMD_RESPONSE      0xC5  // N  send checksum over previous command (FP_CMD_BOOT only, triggers next response)
#define FP_CMD_ICON          0xE1  // N  set icon, not used
#define FP_CMD_STANDBY       0xE5  // Y  switch to standby
#define FP_CMD_REBOOT        0xE6
#define FP_CMD_PRIVATE       0xE7
#define FP_CMD_KEYCODE       0xE9  // N  currently not used
#define FP_CMD_SETWAKEUPTIME 0xEA  // Y  set uC wake up time
#define FP_CMD_BOOT          0xEB  // N  restart FP
#define FP_CMD_SETTIME       0xEC  // Y  set FP time
#define FP_CMD_DISPLAY       0xED  // Y  display text (length n + 2 for text length n, arguments are segment data)

// answer IDs sent by FP     ID    resp?
#define FP_RSP_RESPONSE      0xC5  // N
#define FP_RSP_VERSION       0xDB  // Y  report FP SW version
#define FP_RSP_PRIVATE       0xE7  // N
#define FP_RSP_KEYIN         0xF1  // N  front panel keys and RC keys, length 3 (incl. ID and length byte)
#define FP_RSP_TIME          0xFC  // Y  report FP time

#define cPackageSizeKeyIn    _VAL + 1
#define cGetResponseSize     _VAL + 1
#define cPackageSizeStandby  _VAL + 3
#define cGetTimeSize         _VAL + 9
#define cGetVersionSize      _VAL + 4
#define cGetPrivateSize      _VAL + 8

// uncomment next line if you want VFD_TEST enabled
//#define VFD_TEST             1

typedef enum
{
	_VFD,
	_7SEG,
	_VFD_OLD
} DISPLAYTYPE;

enum
{
	PWR_ON,
	STANDBY,
	TIMER
};

/* standby time must be given as follows:
 * time[0] & time[1] = mjd
 * time[2] = hour
 * time[3] = min
 * time[4] = sec
 */
struct set_standby_s
{
	char time[5];
};

struct set_time_s
{
	time_t localTime;
};

struct set_light_s
{
	int onoff;
};

#if defined(VFD_TEST)
struct set_test_s
{
	unsigned char data[12];
};
#endif

struct cn_micom_ioctl_data
{
	union
	{
		struct set_standby_s standby;
		struct set_time_s time;
		struct set_light_s light;
#if defined(VFD_TEST)
		struct set_test_s test;
#endif
	} u;
};

struct vfd_ioctl_data
{
	unsigned char start_address;
	unsigned char data[64];
	unsigned char length;
};

struct saved_data_s
{
	int length;
	char data[128];
	int display_on;
};
extern struct saved_data_s lastdata;

extern int mcom_init_func(void);
extern void copyData(unsigned char *data, int len);
extern int mcom_WriteCommand(char *buffer, int len, int needAck);
extern unsigned char mcom_Checksum(unsigned char *payload, int len);
extern time_t calcGetmcomTime(char *time);
extern struct tm *gmtime(register const time_t time);
extern void getRCData(unsigned char *data, int *len);
extern int mcom_SendResponse(char *buf);
void dumpValues(void);

extern int errorOccured;

extern struct file_operations vfd_fops;

extern unsigned char ioctl_data[];
extern unsigned char mcom_time[];
extern unsigned char mcom_wakeup_time[];
extern unsigned char mcom_version[];
extern time_t        mcom_time_set_time;  // system time when FP clock time was received on BOOT
extern unsigned char mcom_private[];
extern int           eb_flag;
extern int           preamblecount;
extern unsigned char rsp_cksum;
extern int           wakeupreason;

extern char          *gmt_offset;  // module param, string
extern int           rtc_offset;
#endif  // _123_cn_micom
// vim:ts=4
