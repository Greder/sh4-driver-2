/****************************************************************************
 *
 * opt9600_fp.h
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2020 Audioniek: ported to Opticum HD (TS) 9600
 *
 * Largely based on cn_micom, enhanced and ported to Opticum HD (TS) 9600
 * by Audioniek.
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
 * 20201223 Audioniek       Initial version, based on cn_micom.
 * 20210110 Audioniek       VFDTEST added.
 *
 ****************************************************************************/
#ifndef _opt9600_fp_h
#define _opt9600_fp_h
/*
 */

extern short paramDebug;  // debug print level is zero as default (0=nothing, 1= errors, 10=some detail, 20=more detail, 50=open/close functions, 100=all)
#define TAGDEBUG "[opt9600_fp] "
#define dprintk(level, x...) \
do \
{ \
	if ((paramDebug) && (paramDebug >= level) || level == 0) \
	{ \
		printk(TAGDEBUG x); \
	} \
} while (0)

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

#define FRONTPANEL_MINOR_RC   1
#define LASTMINOR             2

extern tFrontPanelOpen FrontPanelOpen[LASTMINOR];

#define VFD_MAJOR             147

#define MAX_MCOM_MSG          30

#define DISPLAY_WIDTH         8

/* ioctl numbers ->hacky */
#define VFDDISPLAYCHARS       0xc0425a00
#define VFDBRIGHTNESS         0xc0425a03  // not supported by hardware
#define VFDDISPLAYWRITEONOFF  0xc0425a05
#define VFDDRIVERINIT         0xc0425a08
#define VFDICONDISPLAYONOFF   0xc0425a0a  // not supported by hardware

#define VFDTEST               0xc0425af0  // added by audioniek
#define VFDSETPOWERONTIME     0xc0425af6
#define VFDGETWAKEUPMODE      0xc0425af9  // always power on, not supported by hardware
#define VFDGETTIME            0xc0425afa  // simulated, not supported by hardware
#define VFDSETTIME            0xc0425afb
#define VFDSTANDBY            0xc0425afc
//#define VFDSETLED             0xc0425afe  // receiver has no controllable LEDs
#define VFDSETMODE            0xc0425aff
#define VFDGETWAKEUPTIME      0xc0425b03  // simulated, not supported by hardware

#define ACK_WAIT_TIME msecs_to_jiffies(1000)

#define cPackageSizeStandby   5
#define cPackageSizeKeyIn     3

#define cGetTimeSize          8  // was 11
#define cGetVersionSize       5  // was 6
#define cGetWakeupReasonSize  10
#define cGetResponseSize      3

#define cMinimumSize          3

// commands to front processor
#define FP_CMD_RESPONSE       0xC5  // send checksum over previous command (FP_CMD_BOOT only, triggers next response)
#define FP_CMD_VERSION        0xDB  // get front uC version
#define FP_CMD_ICON           0xE1  // set icon (not used or implemented?)
#define FP_CMD_STANDBY        0xE5  // switch to standby
#define FP_CMD_REBOOT         0xE6
#define FP_CMD_WAKEUPREASON   0xE7  // request wakeup reason?
#define FP_CMD_PRIVATE        0xE7
#define FP_CMD_KEYCODE        0xE9
#define FP_CMD_SETWAKEUPTIME  0xEA  // set uC wakup time
#define FP_CMD_BOOT           0xEB  // request FP uC boot / switch off (start/stop front uC?)
#define FP_CMD_SETTIME        0xEC  // set uC current time
#define FP_CMD_DISPLAY        0xED  // display text

// answer tags from front processor
#define FP_RSP_RESPONSE       0xC5  // acknowledge command (with checksum return)
#define FP_RSP_VERSION        0xDB  // version info response
#define FP_RSP_WAKEUP_REASON  0xE7
#define FP_RSP_KEYIN          0xF1  // key press response
#define FP_RSP_TIME           0xFC  // report uC time

#define _TAG                  0  // offset for command byte in command string
#define _LEN                  1  // offset for length byte in command string
#define _VAL                  2  // offset for argument byte(s) in command string

/* time must be given as follows:
 * time[0] = year
 * time[1] = month
 * time[2] = day
 * time[3] = hour
 * time[4] = min  // NOTE: front processor does not use seconds
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

struct set_test_s
{
	unsigned char data[12];
};

struct opt9600_fp_ioctl_data
{
	union
	{
		struct set_standby_s standby;
		struct set_time_s time;
		struct set_light_s light;
		struct set_test_s test;
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
extern int mcom_SendResponse(char *buf);
extern void getRCData(unsigned char *data, int *len);
void dumpValues(void);

extern void calcSetmcomTime(time_t theTime, char *destString);
extern time_t calcGetmcomTime(char *time);

extern int errorOccured;
extern unsigned char expectedData;
extern unsigned char ioctl_data[];
extern unsigned char mcom_time[];
extern unsigned char mcom_version[];
extern time_t        mcom_time_set_time;  // system time when FP clock time was received on BOOT
extern int           eb_flag;
extern unsigned char rsp_cksum;

extern struct file_operations vfd_fops;
extern char *gmt_offset;  // module param, string
extern int rtc_offset;
#endif  // _opt9600_fp_h
// vim:ts=4
