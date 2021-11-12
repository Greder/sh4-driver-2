/****************************************************************************
 *
 * opt9600_fp_file.c
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2020 Audioniek: ported to Opticum (TS) HD 9600
 *
 * Largely based on cn_micom, enhanced and ported to Opticum HD (TS) 9600
 * by Audioniek. Reason for this is that the cn_micom driver does not work
 * on the Opticum HD (TS) 9600 models.
 *
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
 * 20201222 Audioniek       Initial version, based on cn_micom.
 * 20201228 Audioniek       Add Opticum HD 9600 specifics.
 * 20201230 Audioniek       Fix Opticum HD 9600 startup sequence.
 * 20210110 Audioniek       VFDTEST added, conditionally compiled.
 * 20210929 Audioniek       procfs added.
 * 20210929 Audioniek       Writing a text to /dev/vfd scrolls once if
 *                          length is longer than display width.
 * 20210930 Audioniek       Add simulated FP get time.
 * 20210930 Audioniek       Remove sending response with request for a
 *                          response.
 * 20210930 Audioniek       Replace PRN_LOG with dprintk.
 * 20210930 Audioniek       Change wake up reason from Unknown (0) to 1
 *                          (AC power on). Command to retrieve reason from
 *                          FP not discovered yet.
 * 20210930 Audioniek       Add module parameters waitTime and gmt_offset.
 * 20210930 Audioniek       Deep standby and reboot at given time fixed.
 * 20210930 Audioniek       VFDDISPLAYWRITEONOFF made functional.
 * 20211002 Audioniek       UTF-8 support on HD 9600 models added.
 * 20211003 Audioniek       Deep standby/reboot at given time further
 *                          debugged and take prviously specified FP clock
 *                          and wake uptimes into account; also improved
 *                          checking on valid/usable wake up times.
 *
 ****************************************************************************/

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/termbits.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
#include <linux/stm/pio.h>
#else
#include <linux/stpio.h>
#endif
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/poll.h>
#include <linux/syscalls.h>

#include "opt9600_fp.h"
#include "opt9600_fp_asc.h"
#include "opt9600_fp_utf.h"
#include "opt9600_fp_char.h"

extern void ack_sem_up(void);
extern int  ack_sem_down(void);
int mcom_WriteString(unsigned char *aBuf, int len);
extern void mcom_putc(unsigned char data);

struct semaphore write_sem;
int errorOccured = 0;
unsigned char expectedData = 0;
extern int dataflag;
unsigned char ioctl_data[12];
int eb_flag = 0;
unsigned char rsp_cksum;

tFrontPanelOpen FrontPanelOpen [LASTMINOR];

struct saved_data_s lastdata;

time_t        mcom_time_set_time;  // system time when FP clock time was received on BOOT
unsigned char mcom_time[12];

static int    mcom_settime_set_time_flag = 0;  // flag: new FP clock time was specified
time_t        mcom_settime_set_time;  // system time when mcom_settime was input
static time_t mcom_settime;  // time to set FP clock to when shutting down

unsigned char mcom_wakeup_time[5];  // wake up time specified
static int    mcom_wakeup_time_set_flag = 0;  // flag: new FP wake up time was specified

unsigned char mcom_version[4];
//static unsigned char mcom_private[8]; // 0000 0000(Boot from AC on), 0000 0001(Boot from standby), 0000 0002(micom did not use this value, bug)

/**************************************************
 *
 * Character tables
 *
 * Unfortunately all frontprocessors of the various
 * modelsdo not allow direct control of the
 * characters displayed and effectively only
 * support for digits and uppercase letters.
 *
 */

/**************************************************
 *
 * Character table for LED models
 *
 * Order is ASCII.
 *
 * NOTE: lower case letters are displayed in the
 * same way as uppercase
 */
static char *mcom_ascii_char_7seg[] =
{
	_7SEG_NULL,   // 0x00, unprintable control character
	_7SEG_NULL,   // 0x01, unprintable control character
	_7SEG_NULL,   // 0x02, unprintable control character
	_7SEG_NULL,   // 0x03, unprintable control character
	_7SEG_NULL,   // 0x04, unprintable control character
	_7SEG_NULL,   // 0x05, unprintable control character
	_7SEG_NULL,   // 0x06, unprintable control character
	_7SEG_NULL,   // 0x07, unprintable control character
	_7SEG_NULL,   // 0x08, unprintable control character
	_7SEG_NULL,   // 0x09, unprintable control character
	_7SEG_NULL,   // 0x0a, unprintable control character
	_7SEG_NULL,   // 0x0b, unprintable control character
	_7SEG_NULL,   // 0x0c, unprintable control character
	_7SEG_NULL,   // 0x0d, unprintable control character
	_7SEG_NULL,   // 0x0e, unprintable control character
	_7SEG_NULL,   // 0x0f, unprintable control character

	_7SEG_NULL,   // 0x10, unprintable control character
	_7SEG_NULL,   // 0x11, unprintable control character
	_7SEG_NULL,   // 0x12, unprintable control character
	_7SEG_NULL,   // 0x13, unprintable control character
	_7SEG_NULL,   // 0x14, unprintable control character
	_7SEG_NULL,   // 0x15, unprintable control character
	_7SEG_NULL,   // 0x16, unprintable control character
	_7SEG_NULL,   // 0x17, unprintable control character
	_7SEG_NULL,   // 0x18, unprintable control character
	_7SEG_NULL,   // 0x19, unprintable control character
	_7SEG_NULL,   // 0x1a, unprintable control character
	_7SEG_NULL,   // 0x1b, unprintable control character
	_7SEG_NULL,   // 0x1c, unprintable control character
	_7SEG_NULL,   // 0x1d, unprintable control character
	_7SEG_NULL,   // 0x1e, unprintable control character
	_7SEG_NULL,   // 0x1f, unprintable control character

	_7SEG_NULL,   // 0x20, <space>
	_7SEG_NULL,   // 0x21, !
	_7SEG_NULL,   // 0x22, "
	_7SEG_NULL,   // 0x23, #
	_7SEG_NULL,   // 0x24, $
	_7SEG_NULL,   // 0x25, %
	_7SEG_NULL,   // 0x26, &
	_7SEG_NULL,   // 0x27, '
	_7SEG_NULL,   // 0x28, (
	_7SEG_NULL,   // 0x29, )
	_7SEG_NULL,   // 0x2a, *
	_7SEG_NULL,   // 0x2b, +
	_7SEG_NULL,   // 0x2c, ,
	_7SEG_NULL,   // 0x2d, -
	_7SEG_NULL,   // 0x2e, .
	_7SEG_NULL,   // 0x2f, /

	_7SEG_NUM_0,  // 0x30, 0,
	_7SEG_NUM_1,  // 0x31, 1,
	_7SEG_NUM_2,  // 0x32, 2,
	_7SEG_NUM_3,  // 0x33, 3,
	_7SEG_NUM_4,  // 0x34, 4,
	_7SEG_NUM_5,  // 0x35, 5,
	_7SEG_NUM_6,  // 0x36, 6,
	_7SEG_NUM_7,  // 0x37, 7,
	_7SEG_NUM_8,  // 0x38, 8,
	_7SEG_NUM_9,  // 0x39, 9,
	_7SEG_NULL,   // 0x3a, :
	_7SEG_NULL,   // 0x3b, ;
	_7SEG_NULL,   // 0x3c, <
	_7SEG_NULL,   // 0x3d, =
	_7SEG_NULL,   // 0x3e, >
	_7SEG_NULL,   // 0x3f, ?

	_7SEG_NULL,   // 0x40, @
	_7SEG_CH_A,   // 0x41, A
	_7SEG_CH_B,   // 0x42, B
	_7SEG_CH_C,   // 0x43, C
	_7SEG_CH_D,   // 0x44, D
	_7SEG_CH_E,   // 0x45, E
	_7SEG_CH_F,   // 0x46, F
	_7SEG_CH_G,   // 0x47, G
	_7SEG_CH_H,   // 0x48, H
	_7SEG_CH_I,   // 0x49, I
	_7SEG_CH_J,   // 0x4a, J
	_7SEG_CH_K,   // 0x4b, K
	_7SEG_CH_L,   // 0x4c, L
	_7SEG_CH_M,   // 0x4d, M
	_7SEG_CH_N,   // 0x4e, N
	_7SEG_CH_O,   // 0x4f, O

	_7SEG_CH_P,   // 0x50, P
	_7SEG_CH_Q,   // 0x51, Q
	_7SEG_CH_R,   // 0x52, R
	_7SEG_CH_S,   // 0x53, S
	_7SEG_CH_T,   // 0x54, T
	_7SEG_CH_U,   // 0x55, U
	_7SEG_CH_V,   // 0x56, V
	_7SEG_CH_W,   // 0x57, W
	_7SEG_CH_X,   // 0x58, X
	_7SEG_CH_Y,   // 0x59, Y
	_7SEG_CH_Z,   // 0x5a, Z
	_7SEG_NULL,   // 0x5b  [
	_7SEG_NULL,   // 0x5c, |
	_7SEG_NULL,   // 0x5d, ]
	_7SEG_NULL,   // 0x5e, ^
	_7SEG_NULL,   // 0x5f, _

	_7SEG_NULL,   // 0x60, `
	_7SEG_CH_A,   // 0x61, a
	_7SEG_CH_B,   // 0x62, b
	_7SEG_CH_C,   // 0x63, c
	_7SEG_CH_D,   // 0x64, d
	_7SEG_CH_E,   // 0x65, e
	_7SEG_CH_F,   // 0x66, f
	_7SEG_CH_G,   // 0x67, g
	_7SEG_CH_H,   // 0x68, h
	_7SEG_CH_I,   // 0x69, i
	_7SEG_CH_J,   // 0x6a, j
	_7SEG_CH_K,   // 0x6b, k
	_7SEG_CH_L,   // 0x6c, l
	_7SEG_CH_M,   // 0x6d, m
	_7SEG_CH_N,   // 0x6e, n
	_7SEG_CH_O,   // 0x6f, o

	_7SEG_CH_P,   // 0x70, p
	_7SEG_CH_Q,   // 0x71, q
	_7SEG_CH_R,   // 0x72, r
	_7SEG_CH_S,   // 0x73, s
	_7SEG_CH_T,   // 0x74, t
	_7SEG_CH_U,   // 0x75, u
	_7SEG_CH_V,   // 0x76, v
	_7SEG_CH_W,   // 0x77, w
	_7SEG_CH_X,   // 0x78, x
	_7SEG_CH_Y,   // 0x79, y
	_7SEG_CH_Z,   // 0x7a, z
	_7SEG_NULL,   // 0x7b, {
	_7SEG_NULL,   // 0x7c, backslash
	_7SEG_NULL,   // 0x7d, }
	_7SEG_NULL,   // 0x7e, ~
	_7SEG_NULL    // 0x7f, <DEL>--> all segments on
};

/**************************************************
 *
 * Character table for early VFD models
 *
 * Order is ASCII.
 *
 * NOTE: lower case letters are translated
 * to uppercase
 */
static unsigned char mcom_ascii_char_14seg[] =
{
	0x00,  // 0x00, unprintable control character
	0x00,  // 0x01, unprintable control character
	0x00,  // 0x02, unprintable control character
	0x00,  // 0x03, unprintable control character
	0x00,  // 0x04, unprintable control character
	0x00,  // 0x05, unprintable control character
	0x00,  // 0x06, unprintable control character
	0x00,  // 0x07, unprintable control character
	0x00,  // 0x08, unprintable control character
	0x00,  // 0x09, unprintable control character
	0x00,  // 0x0a, unprintable control character
	0x00,  // 0x0b, unprintable control character
	0x00,  // 0x0c, unprintable control character
	0x00,  // 0x0d, unprintable control character
	0x00,  // 0x0e, unprintable control character
	0x00,  // 0x0f, unprintable control character

	0x00,  // 0x10, unprintable control character
	0x00,  // 0x11, unprintable control character
	0x00,  // 0x12, unprintable control character
	0x00,  // 0x13, unprintable control character
	0x00,  // 0x14, unprintable control character
	0x00,  // 0x15, unprintable control character
	0x00,  // 0x16, unprintable control character
	0x00,  // 0x17, unprintable control character
	0x00,  // 0x18, unprintable control character
	0x00,  // 0x19, unprintable control character
	0x00,  // 0x1a, unprintable control character
	0x00,  // 0x1b, unprintable control character
	0x00,  // 0x1c, unprintable control character
	0x00,  // 0x1d, unprintable control character
	0x00,  // 0x1e, unprintable control character
	0x00,  // 0x1f, unprintable control character

	0x00,  // 0x20, <space>
	0x00,  // 0x21, !
	0x00,  // 0x22, "
	0x00,  // 0x23, #
	0x00,  // 0x24, $
	0x00,  // 0x25, %
	0x00,  // 0x26, &
	0x00,  // 0x27, '
	0x00,  // 0x28, (
	0x00,  // 0x29, )
	0x00,  // 0x2a, *
	0x00,  // 0x2b, +
	0x00,  // 0x2c, ,
	0x00,  // 0x2d, -
	0x00,  // 0x2e, .
	0x00,  // 0x2f, /

	0x02,  // 0x30, 0
	0x03,  // 0x31, 1
	0x04,  // 0x32, 2
	0x05,  // 0x33, 3
	0x06,  // 0x34, 4
	0x07,  // 0x35, 5
	0x08,  // 0x36, 6
	0x09,  // 0x37, 7
	0x0a,  // 0x38, 8
	0x0b,  // 0x39, 9
	0x00,  // 0x3a, :
	0x00,  // 0x3b, ;
	0x00,  // 0x3c, <
	0x00,  // 0x3d, =
	0x00,  // 0x3e, >
	0x00,  // 0x3f, ?

	0x00,  // 0x40, @
	0x0c,  // 0x41, A
	0x0d,  // 0x42, B
	0x0e,  // 0x43, C
	0x0f,  // 0x44, D
	0x10,  // 0x45, E
	0x11,  // 0x46, F
	0x12,  // 0x47, G
	0x13,  // 0x48, H
	0x14,  // 0x49, I
	0x15,  // 0x4a, J
	0x16,  // 0x4b, K
	0x17,  // 0x4c, L
	0x18,  // 0x4d, M
	0x19,  // 0x4e, N
	0x1a,  // 0x4f, O

	0x1b,  // 0x50, P
	0x1c,  // 0x51, Q
	0x1d,  // 0x52, R
	0x1e,  // 0x53, S
	0x1f,  // 0x54, T
	0x20,  // 0x55, U
	0x21,  // 0x56, V
	0x22,  // 0x57, W
	0x23,  // 0x58, X
	0x24,  // 0x59, Y
	0x25,  // 0x5a, Z
	0x00,  // 0x5b  [
	0x00,  // 0x5c, |
	0x00,  // 0x5d, ]
	0x00,  // 0x5e, ^
	0x00,  // 0x5f, _

	0x00,  // 0x60, `
	0x0c,  // 0x61, a
	0x0d,  // 0x62, b
	0x0e,  // 0x63, c
	0x0f,  // 0x64, d
	0x10,  // 0x65, e
	0x11,  // 0x66, f
	0x12,  // 0x67, g
	0x13,  // 0x68, h
	0x14,  // 0x69, i
	0x15,  // 0x6a, j
	0x16,  // 0x6b, k
	0x17,  // 0x6c, l
	0x18,  // 0x6d, m
	0x19,  // 0x6e, n
	0x1a,  // 0x6f, o
               
	0x1b,  // 0x70, p
	0x1c,  // 0x71, q
	0x1d,  // 0x72, r
	0x1e,  // 0x73, s
	0x1f,  // 0x74, t
	0x20,  // 0x75, u
	0x21,  // 0x76, v
	0x22,  // 0x77, w
	0x23,  // 0x78, x
	0x24,  // 0x79, y
	0x25,  // 0x7a, z
	0x00,  // 0x7b, {
	0x00,  // 0x7c, backslash
	0x00,  // 0x7d, }
	0x00,  // 0x7e, ~
	0x00,  // 0x7f, <DEL>
};

/**************************************************
 *
 * Character table for late VFD models
 *
 * Order is ASCII.
 *
 * NOTE: lower case letters are translated
 * to uppercase
 */
static char *mcom_ascii_char_14seg_new[] =
{
	_14SEG_NULL,   // 0x00, unprintable control character
	_14SEG_NULL,   // 0x01, unprintable control character
	_14SEG_NULL,   // 0x02, unprintable control character
	_14SEG_NULL,   // 0x03, unprintable control character
	_14SEG_NULL,   // 0x04, unprintable control character
	_14SEG_NULL,   // 0x05, unprintable control character
	_14SEG_NULL,   // 0x06, unprintable control character
	_14SEG_NULL,   // 0x07, unprintable control character
	_14SEG_NULL,   // 0x08, unprintable control character
	_14SEG_NULL,   // 0x09, unprintable control character
	_14SEG_NULL,   // 0x0a, unprintable control character
	_14SEG_NULL,   // 0x0b, unprintable control character
	_14SEG_NULL,   // 0x0c, unprintable control character
	_14SEG_NULL,   // 0x0d, unprintable control character
	_14SEG_NULL,   // 0x0e, unprintable control character
	_14SEG_NULL,   // 0x0f, unprintable control character

	_14SEG_NULL,   // 0x10, unprintable control character
	_14SEG_NULL,   // 0x11, unprintable control character
	_14SEG_NULL,   // 0x12, unprintable control character
	_14SEG_NULL,   // 0x13, unprintable control character
	_14SEG_NULL,   // 0x14, unprintable control character
	_14SEG_NULL,   // 0x15, unprintable control character
	_14SEG_NULL,   // 0x16, unprintable control character
	_14SEG_NULL,   // 0x17, unprintable control character
	_14SEG_NULL,   // 0x18, unprintable control character
	_14SEG_NULL,   // 0x19, unprintable control character
	_14SEG_NULL,   // 0x1a, unprintable control character
	_14SEG_NULL,   // 0x1b, unprintable control character
	_14SEG_NULL,   // 0x1c, unprintable control character
	_14SEG_NULL,   // 0x1d, unprintable control character
	_14SEG_NULL,   // 0x1e, unprintable control character
	_14SEG_NULL,   // 0x1f, unprintable control character

	_14SEG_NULL,   // 0x20, <space>
	_14SEG_NULL,   // 0x21, !
	_14SEG_NULL,   // 0x22, "
	_14SEG_NULL,   // 0x23, #
	_14SEG_NULL,   // 0x24, $
	_14SEG_NULL,   // 0x25, %
	_14SEG_NULL,   // 0x26, &
	_14SEG_NULL,   // 0x27, '
	_14SEG_NULL,   // 0x28, (
	_14SEG_NULL,   // 0x29, )
	_14SEG_NULL,   // 0x2a, *
	_14SEG_NULL,   // 0x2b, +
	_14SEG_NULL,   // 0x2c, ,
	_14SEG_NULL,   // 0x2d, -
	_14SEG_NULL,   // 0x2e, .
	_14SEG_NULL,   // 0x2f, /

	_14SEG_NUM_0,  // 0x30, 0
	_14SEG_NUM_1,  // 0x31, 1
	_14SEG_NUM_2,  // 0x32, 2
	_14SEG_NUM_3,  // 0x33, 3
	_14SEG_NUM_4,  // 0x34, 4
	_14SEG_NUM_5,  // 0x35, 5
	_14SEG_NUM_6,  // 0x36, 6
	_14SEG_NUM_7,  // 0x37, 7
	_14SEG_NUM_8,  // 0x38, 8
	_14SEG_NUM_9,  // 0x39, 9
	_14SEG_NULL,   // 0x3a, :
	_14SEG_NULL,   // 0x3b, ;
	_14SEG_NULL,   // 0x3c, <
	_14SEG_NULL,   // 0x3d, =
	_14SEG_NULL,   // 0x3e, >
	_14SEG_NULL,   // 0x3f, ?

	_14SEG_NULL,   // 0x40, @
	_14SEG_CH_A,   // 0x41, A
	_14SEG_CH_B,   // 0x42, B
	_14SEG_CH_C,   // 0x43, C
	_14SEG_CH_D,   // 0x44, D
	_14SEG_CH_E,   // 0x45, E
	_14SEG_CH_F,   // 0x46, F
	_14SEG_CH_G,   // 0x47, G
	_14SEG_CH_H,   // 0x48, H
	_14SEG_CH_I,   // 0x49, I
	_14SEG_CH_J,   // 0x4a, J
	_14SEG_CH_K,   // 0x4b, K
	_14SEG_CH_L,   // 0x4c, L
	_14SEG_CH_M,   // 0x4d, M
	_14SEG_CH_N,   // 0x4e, N
	_14SEG_CH_O,   // 0x4f, O

	_14SEG_CH_P,   // 0x50, P
	_14SEG_CH_Q,   // 0x51, Q
	_14SEG_CH_R,   // 0x52, R
	_14SEG_CH_S,   // 0x53, S
	_14SEG_CH_T,   // 0x54, T
	_14SEG_CH_U,   // 0x55, U
	_14SEG_CH_V,   // 0x56, V
	_14SEG_CH_W,   // 0x57, W
	_14SEG_CH_X,   // 0x58, X
	_14SEG_CH_Y,   // 0x59, Y
	_14SEG_CH_Z,   // 0x5a, Z
	_14SEG_NULL,   // 0x5b  [
	_14SEG_NULL,   // 0x5c, |
	_14SEG_NULL,   // 0x5d, ]
	_14SEG_NULL,   // 0x5e, ^
	_14SEG_NULL,   // 0x5f, _

	_14SEG_NULL,   // 0x60, `
	_14SEG_CH_A,   // 0x61, a
	_14SEG_CH_B,   // 0x62, b
	_14SEG_CH_C,   // 0x63, c
	_14SEG_CH_D,   // 0x64, d
	_14SEG_CH_E,   // 0x65, e
	_14SEG_CH_F,   // 0x66, f
	_14SEG_CH_G,   // 0x67, g
	_14SEG_CH_H,   // 0x68, h
	_14SEG_CH_I,   // 0x69, i
	_14SEG_CH_J,   // 0x6a, j
	_14SEG_CH_K,   // 0x6b, k
	_14SEG_CH_L,   // 0x6c, l
	_14SEG_CH_M,   // 0x6d, m
	_14SEG_CH_N,   // 0x6e, n
	_14SEG_CH_O,   // 0x6f, o

	_14SEG_CH_P,   // 0x70, p
	_14SEG_CH_Q,   // 0x71, q
	_14SEG_CH_R,   // 0x72, r
	_14SEG_CH_S,   // 0x73, s
	_14SEG_CH_T,   // 0x74, t
	_14SEG_CH_U,   // 0x75, u
	_14SEG_CH_V,   // 0x76, v
	_14SEG_CH_W,   // 0x77, w
	_14SEG_CH_X,   // 0x78, x
	_14SEG_CH_Y,   // 0x79, y
	_14SEG_CH_Z,   // 0x7a, z
	_14SEG_NULL,   // 0x7b, {
	_14SEG_NULL,   // 0x7c, backslash
	_14SEG_NULL,   // 0x7d, }
	_14SEG_NULL,   // 0x7e, ~
	_14SEG_NULL    // 0x7f, <DEL>--> all segments on
};

/****************************************************************************
 *
 * Driver code.
 *
 */

/*******************************************************
 *
 * Code to communicate with the front processor.
 *
 */

/*******************************************************
 *
 * Dump command bytes to front processor with
 * comment text.
 *
 */
static void mcom_Dump(char *string, unsigned char *v_pData, unsigned short v_szData)
{
	int           i;
	char          str[10], line[80], txt[25];
	unsigned char *pData;

	if ((v_pData == NULL) || (v_szData == 0))
	{
		return;
	}

	dprintk(0, "[ %s [%x]]\n", string, v_szData);

	i = 0;
	pData = v_pData;
	line[0] = '\0';
	while ((unsigned short)(pData - v_pData) < v_szData)
	{
		sprintf(str, "%02x ", *pData);
		strcat(line, str);
		txt[i] = ((*pData >= 0x20) && (*pData < 0x7F)) ? *pData : '.';
		i++;

		if (i == 16)
		{
			txt[i] = '\0';
			printk("%s %s\n", line, txt);
			i = 0;
			line[0] = '\0';
		}
		pData++;
	}
	if (i != 0)
	{
		txt[i] = '\0';
		do
		{
			strcat(line, "   ");
		}
		while (++i < 16);
		printk("%s %s\n\n", line, txt);
	}
}

void write_sem_up(void)
{
	up(&write_sem);
}

int write_sem_down(void)
{
	return down_interruptible(&write_sem);
}

void copyData(unsigned char *data, int len)
{
	dprintk(150, "%s > len %d\n", __func__, len);
	memcpy(ioctl_data, data, len);
}

/*****************************************************************
 *
 * mcom_Checksum: Calculate checksum byte over payload.
 *
 */
static unsigned char mcom_Checksum(unsigned char *payload, int len)
{
	int           n;
	unsigned char checksum = 0;

	for (n = 0; n < len; n++)
	{
		checksum ^= payload[n];
	}
	rsp_cksum = (unsigned char)checksum & 0xff;
	return checksum;
}

/*****************************************************************
 *
 * mcom_WriteCommand: Send command to front processor.
 *
 */
int mcom_WriteCommand(char *buffer, int len, int needAck)
{
	int i;

	dprintk(150, "%s > len = %d\n", __func__, len);

	if (paramDebug > 50)
	{
		dprintk(50, "Send Front uC command ");
		for (i = 0; i < len; i++)
		{
			printk("0x%02x ", buffer[i] & 0xff);
		}
		printk("\n");
	}
	for (i = 0; i < len; i++)
	{
#ifdef DIRECT_ASC  // not defined anywhere
		serial_putc(buffer[i]);
#else
		mcom_putc(buffer[i]);
#endif
	}
	msleep(10);

	if (needAck)
	{
		rsp_cksum = mcom_Checksum(buffer, buffer[_LEN] + _VAL);  // determine checksum
//		dprintk(70, "Checksum = 0x%02x\n", rsp_cksum);
		if (ack_sem_down())
		{
			return -ERESTARTSYS;
		}
	}
	dprintk(150, "%s < \n", __func__);
	return 0;
}

/*****************************************************************
 *
 * mcom_SendResponse: Send response string.
 *
 */
int mcom_SendResponse(char *buf)
{
	unsigned char response[3];
	int           res = 0;

	dprintk(150, "%s >\n", __func__);

	response[_TAG] = FP_CMD_RESPONSE;
	response[_LEN] = 1;
	response[_VAL] = mcom_Checksum(buf, buf[_LEN] + _VAL);

	res = mcom_WriteCommand((char *)response, 3, 0);
	dprintk(150, "%s <\n", __func__);
	return res;
}

/*****************************************************************
 *
 * mcom_GetDisplayType: Return type of frontpanel display.
 *
 */
static DISPLAYTYPE mcom_GetDisplayType(void)
{
	if (mcom_version[0] == 4)
	{
		return _7SEG;
	}
	if (mcom_version[0] == 5)
	{
		return _VFD;
	}
	return _VFD_OLD;
}

/*******************************************************
 *
 * Time routines.
 *
 */
static unsigned short mcomYMD2MJD(unsigned short Y, unsigned char M, unsigned char D)
{  // converts YMD to 16 bit MJD
	int L;

	Y -= 1900;
	L = ((M == 1) || (M == 2)) ? 1 : 0;
	return (unsigned short)(14956 + D + ((Y - L) * 36525 / 100) + ((M + 1 + L * 12) * 306001 / 10000));
}

static void mcom_MJD2YMD(unsigned short usMJD, unsigned short *year, unsigned char *month, unsigned char *day)
{  // converts MJD to year month day
	int Y, M, D, K;

	Y = (usMJD * 100 - 1507820) / 36525;
	M = ((usMJD * 10000 - 149561000) - (Y * 36525 / 100) * 10000) / 306001;
	D = usMJD - 14956 - (Y * 36525 / 100) - (M * 306001 / 10000);
	K = ((M == 14) || (M == 15)) ? 1 : 0;

	Y = Y + K + 1900;
	M = M - 1 - K * 12;

	*year  = (unsigned short)Y;
	*month = (unsigned char)M;
	*day   = (unsigned char)D;
}

/*******************************************************
 *
 * mcom_SetStandby: put receiver in (deep) standby.
 *
 * time = wake up time as MJD H M S
 *
 * Notes:
 * - This function sets the front panel clock to the time
 *   specified previously through VFDSETTIME, if there
 *   was one, or else to the system time corrected with
 *   the GMT offset (local time).
 * - The wake up time is handled as follows:
 *   1. If it is in the future compared to the
 *      current front panel clock time, the specified
 *      wake up time is used, except when it is the
 *      maximum possible Linux time;
 *   2. If a wake up time specified through
 *      VFDSETPOWERONTIME is set, again provided it is
 *      in the future compared to the current front
 *      panel clock time, then that time is used.
 *   If both wake up times are not usable, the driver
 *   will not set a wake up time at all, causing the
 *   receiver to remain in deep standby until woken up
 *   by the user.
 *
 */
int mcom_SetStandby(char *time, int showTime, int twentyfour)
{
	char comm_buf[16];
	int  res = 0;
	int  wakeup = 1;
	unsigned short  mjd;
	unsigned short  year;
	unsigned char   month;
	unsigned char   day;
	unsigned short  wake_year;
	unsigned char   wake_month;
	unsigned char   wake_day;
	time_t          curr_time;
	unsigned char   curr_time_h;
	unsigned char   curr_time_m;
	unsigned char   curr_time_s;
	time_t          wake_time;
	struct timespec tp;

	dprintk(150, "%s >\n", __func__);

#if 0
	if (mcom_GetDisplayType() == _VFD_OLD || mcom_GetDisplayType() == _VFD)
	{
		res = mcom_WriteString("Bye bye", strlen("Bye bye"));
	}
	else
	{
		res = mcom_WriteString("WAIT", strlen("WAIT"));
	}
#endif

	// Step one: determine system time
	// and what time to use to synchronize the FP clock
	tp = current_kernel_time();  // get system time (seconds since epoch)

	dprintk(20, "Set FP time ");
	if (mcom_settime_set_time_flag)  // if set time flag set
	{  // use set time
		curr_time = mcom_settime + (tp.tv_sec - mcom_settime_set_time);  // add time difference since setting
		if (paramDebug >= 20)
		{
			printk("to previously set FP time:");
		}
	}
	else
	{
		curr_time = tp.tv_sec + rtc_offset;  // else use current system time plus UTC offset
		if (paramDebug >= 20)
		{
			printk("to system time:");
		}
	}
	mjd = (curr_time / (24 * 60 * 60)) + 40587;
	mcom_MJD2YMD(mjd, &year, &month, &day);
	curr_time_h = (curr_time % (24 * 60 * 60)) / (60 * 60);  // get (seconds in current day / seconds per hour) = hours
	curr_time_m = ((curr_time % (24 * 60 * 60)) % (60 * 60)) / 60;  // get remaining minutes
	if (paramDebug >= 20)
	{
		printk(" %02d:%02d %02d-%02d-%04d\n", curr_time_h, curr_time_m, day, month, year);
	}

	// Step two: if valid timer set, set WAKEUP flag
	// wake up time is sent as MJD H M S
	// convert wake up MJD to date
	mcom_MJD2YMD(((time[0] << 8) + time[1]), &wake_year, &wake_month, &wake_day);
//	dprintk(20, "Wake up time supplied: %02d:%02d:%02d %02d-%02d-%04d\n", time[2], time[3], time[4], wake_day, wake_month, wake_year);

	// evaluate wake up time specified
	// convert time specified to a time_t
	wake_time = calcGetmcomTime(time);
	if (wake_time < curr_time || wake_time == LONG_MAX)
	{  // it appears no timer was set, try specifyable wake up time
//		dprintk(20, "No timer set (or in the past)\n");
		mcom_MJD2YMD(((mcom_wakeup_time[0] << 8) + mcom_wakeup_time[1]), &wake_year, &wake_month, &wake_day);

//		dprintk(20, "Current set wake up time: %02d:%02d:%02d %02d-%02d-%04d\n", mcom_wakeup_time[2], mcom_wakeup_time[3], mcom_wakeup_time[4], wake_day, wake_month, wake_year);
		if (mcom_wakeup_time_set_flag
		&& ((mcom_wakeup_time[0] << 8) + mcom_wakeup_time[1]) >= mjd
		&& (mcom_wakeup_time[2] >= curr_time_h)
		&& (mcom_wakeup_time[3] > curr_time_m))
		{  // wake up time specified is later than system time -> use that
//			dprintk(20, "Set wake up time in the future, use it\n");
			time[2] = mcom_wakeup_time[2];  // overwrite hours
			time[3] = mcom_wakeup_time[3];  // overwrite minutes
		}
		else
		{  //  no wake up time was usable, do not send WAKEUP command
			dprintk(20, "Do not send wake up command\n");
			wakeup = 0;
		}
	}

	memset(comm_buf, 0, sizeof(comm_buf));

	// Step three: send "STANDBY"
	if (mcom_GetDisplayType() == _VFD_OLD)
	{
		comm_buf[_TAG] = FP_CMD_STANDBY;  // 0xe5
		comm_buf[_LEN] = 3;
		comm_buf[_VAL + 0] = (showTime) ? 1 : 0;  // flag: show time in standby if 1
		comm_buf[_VAL + 1] = (twentyfour) ? 1: 0;  // 24h flag (24h mode = 1)
		comm_buf[_VAL + 2] = wakeup;  // flag: wake up will follow if 1

		mcom_Dump("@@@ SEND STANDBY @@@", comm_buf, 2 + comm_buf[_LEN]);
		res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);  // wait for ACK
	}
	else //if (mcom_GetDisplayType() == _VFD)
	{
		comm_buf[_TAG] = FP_CMD_STANDBY;  // 0xe5
		comm_buf[_LEN] = 5;
		comm_buf[_VAL + 0] = (showTime) ? 1 : 0;  // flag: show time in standby if 1
		comm_buf[_VAL + 1] = (twentyfour) ? 1: 0;  // 24h flag (24h mode = 1)
		comm_buf[_VAL + 2] = (wakeup) ? 1 : 0;
		comm_buf[_VAL + 3] = 3;  // power on delay
		comm_buf[_VAL + 4] = 1;  // extra data (private data)

		res = mcom_WriteCommand(comm_buf, 7, 0);
	}
	if (res != 0)
	{
		goto _EXIT_MCOM_STANDBY;
	}
	msleep(100);

	// Step four: send "SETTIME" to sychronize FP clock
	comm_buf[_TAG] = FP_CMD_SETTIME;  // 0xec
	comm_buf[_LEN] = 5;
	comm_buf[_VAL + 0] = (year - 2000) & 0xff;  // strip century
	comm_buf[_VAL + 1] = month;
	comm_buf[_VAL + 2] = day;
	comm_buf[_VAL + 3] = curr_time_h;
	comm_buf[_VAL + 4] = curr_time_m;

	res = mcom_WriteCommand(comm_buf, 7, 1);  // set FP clock, wait for ACK
	if (res != 0)
	{
		goto _EXIT_MCOM_STANDBY;
	}
	msleep(100);

	// Step five: send "SETWAKEUPTIME" if a valid wake up time was found
	if (wakeup)
	{
		comm_buf[_TAG] = FP_CMD_SETWAKEUPTIME;
		comm_buf[_LEN] = 4;
		comm_buf[_VAL + 0] = wake_month & 0xFF;
		comm_buf[_VAL + 1] = wake_day & 0xFF;
		comm_buf[_VAL + 2] = time[2] & 0xFF;
		comm_buf[_VAL + 3] = time[3] & 0xFF;
		// Note: no seconds
		dprintk(20, "Set Wake up time to %02d:%02d %02d-%02d (current year)\n", comm_buf[_VAL + 2], comm_buf[_VAL + 3], comm_buf[_VAL + 1], comm_buf[_VAL + 0]);

		mcom_Dump("@@@ SEND WAKEUP @@@", comm_buf, comm_buf[_LEN] + _VAL);
		res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);  // wait for ACK
		if (res != 0)
		{
			goto _EXIT_MCOM_STANDBY;
		}
		msleep(100);
	}

	// Step six: send "PRIVATE" on late VFD models
	if (mcom_GetDisplayType() == _VFD)
	{
		// send "FP_CMD_PRIVATE"
		comm_buf[_TAG] = FP_CMD_PRIVATE;
		comm_buf[_LEN] = 8;
		comm_buf[_VAL + 0] = 0;
		comm_buf[_VAL + 1] = 1;
		comm_buf[_VAL + 2] = 2;
		comm_buf[_VAL + 3] = 3;
		comm_buf[_VAL + 4] = 4;
		comm_buf[_VAL + 5] = 5;
		comm_buf[_VAL + 6] = 6;
		comm_buf[_VAL + 7] = 7;
	
//		memset(comm_buf + _VAL, 1, 8);  // why is this?
		res = mcom_WriteCommand(comm_buf, 10, 0);
		if (res != 0)
		{
			goto _EXIT_MCOM_STANDBY;
		}
		msleep(100);
	}

_EXIT_MCOM_STANDBY:
	dprintk(150, "%s <\n", __func__);
	return res;
}

/*******************************************************
 *
 * mcom_SetTime: set front processor time.
 *
 * The front processor time cannot be set directly with
 * a command, but only as part of the shut down
 * procedure.
 *
 * To simulate setting, the time specified is stored in
 * a variable, as well as the system time at the time
 * this command was issued.
 *
 * Each time the FP clock time is asked for, the time
 * stored is taken into account, and the time elapsed
 * according to the advance in system time is added. The
 * The result of this is returned as "the FP time".
 *
 * Setting the FP clock time is actually done on a
 * shutdown, using the same mechanism.
 *
 * It should be noted that this approach is not failsafe
 * as resetting the system time (for instance with
 * daylight saving changes) may lead to undesirable
 * effects.
 *
 * Wake up time specified is assumed to be local.
 */
int mcom_SetTime(time_t time)
{
	struct timespec tp;
	char timeString[5];
	unsigned short year;
	unsigned char month;
	unsigned char day;
	int res;
	char comm_buf[8];

	dprintk(150, "%s >\n", __func__);

	calcSetmcomTime(time, timeString);
	dprintk(20, "MJD = %u\n", ((timeString[0] << 8) + timeString[1]));
	mcom_MJD2YMD((unsigned short)((timeString[0] << 8) + timeString[1]), &year, &month, &day);
	dprintk(20, "FP time to set: %02d:%02d:%02d %02d-%02d-%04d\n", timeString[2], timeString[3], timeString[4], (int)day, (int)month, (int)year);
#if 1
 	tp = current_kernel_time();  // get current system time
	mcom_settime_set_time = tp.tv_sec;  // save time of setting
	mcom_settime = time;  // save time to set
	mcom_settime_set_time_flag = 1;  // and flag set time
#else  // following does not work
	comm_buf[_TAG] = FP_CMD_SETTIME;
	comm_buf[_LEN] = 5;
	comm_buf[_VAL + 0] = (year - 2000) & 0xff;  // strip century
	comm_buf[_VAL + 1] = month;
	comm_buf[_VAL + 2] = day;
	comm_buf[_VAL + 3] = timeString[2];
	comm_buf[_VAL + 4] = timeString[3];
	// NOTE: no seconds
	res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);  // wait for ACK
	if (res != 0)
	{
		return -1;
	}
	msleep(100);
#endif
	return 0;
}

/*******************************************************
 *
 * mcom_GetTime: get front processor time.
 *
 * The front processor does not have a command to set
 * its clock time directly. To circumvent this, this
 * function returns the time read when the FP clock was
 * read at the last BOOT command, plus the time the
 * system time has advanced since then.
 * NOTE: this will miserably fail when the system time
 *       is set to a new value between the BOOT command
 *       and calling this function!
 *
 */
int mcom_GetTime(void)
{
	unsigned short  cur_mjd;
	unsigned int    cur_time_in_sec;
	unsigned int    time_in_sec;
	unsigned int    passed_time_in_sec;
	struct timespec tp_now;
	time_t          FP_time;

	dprintk(150, "%s >\n", __func__);

	tp_now = current_kernel_time();  // get current system time

	if (mcom_settime_set_time_flag)  // if a new time was previously input
	{
		FP_time = mcom_settime + (tp_now.tv_sec - mcom_settime_set_time);  // calculate "FP time"
		calcSetmcomTime(FP_time, ioctl_data);  // convert to MJD H M S
	}
	else
	{
		cur_mjd = mcomYMD2MJD(mcom_time[0] + 2000, mcom_time[1], mcom_time[2]);  // get mjd

		time_in_sec = mcom_time[3] * 60 * 60  // hours
		            + mcom_time[4] * 60  // minutes
		            + mcom_time[5]
		            + (tp_now.tv_sec - mcom_time_set_time); // add time elapsed since FP clock time retrieval at BOOT
	
		cur_mjd        += time_in_sec / (24 * 60 * 60);
		cur_time_in_sec = time_in_sec % (24 * 60 * 60);

		ioctl_data[0] = cur_mjd >> 8;
		ioctl_data[1] = cur_mjd & 0xff;
		ioctl_data[2] = cur_time_in_sec / (60 * 60);
		ioctl_data[3] = (cur_time_in_sec % (60 * 60)) / 60;
		ioctl_data[4] = (cur_time_in_sec % (60 * 60)) % 60;
	}
	dprintk(20, "FP time: MJD = %d, %02d:%02d:%02d\n", ((ioctl_data[0] << 8) + ioctl_data[1]), ioctl_data[2], ioctl_data[3], ioctl_data[4]); 
	dprintk(150, "%s <\n", __func__);
	return 0;
}

/*******************************************************
 *
 * mcom_SetWakeUpTime: set front processor wake up time.
 *
 * NOTE: The frontprocessor does not seem to have
 *       facilities to set and read back the wake up
 *       time directly; setting it can only be done
 *       upon a shutdown to deep standby.
 *       Because of this restriction the wake up time
 *       is stored in the driver, not in the front
 *       processor, and is only used in case the shut
 *       down function detects a wake up time in the
 *       past (fp_control does this) or end-of-Linux-
 *       time (time_t LONG_MAX).
 *
 */
int mcom_SetWakeUpTime(char *wtime)
{
//	struct timespec tp;
//	char *timeString;
	unsigned short year;
	unsigned char month;
	unsigned char day;

	int i;

	dprintk(150, "%s >\n", __func__);

	for (i = 0; i < 5; i++)
	{
		mcom_wakeup_time[i] = wtime[i];
	}
	mcom_MJD2YMD((unsigned short)((mcom_wakeup_time[0] << 8) + mcom_wakeup_time[1]), &year, &month, &day);
	dprintk(20, "FP wake up time to set: %02d:%02d:%02d %02d-%02d-%04d\n", mcom_wakeup_time[2], mcom_wakeup_time[3], mcom_wakeup_time[4], day, month, year);
	mcom_wakeup_time_set_flag = 1;
	return 0;
}

/*******************************************************
 *
 * mcom_GetWakeUpTime: get front processor wake up time.
 *
 * The front processor does not seem to have a command
 * to set its wake up time directly. To circumvent this,
 * this function returns one of two things:
 * - If a wake up time was specified previously through
 *   mcom_SetWakeUpTime: that wake up time (this time
 *   may be in the past);
 * - In all other cases: 01-01-2000 (earliest possible
 *   in front processor terms as it assumes the century
 *   to be always 20.
 *
 */
int mcom_GetWakeUpTime(char *time)
{
	int i;

	for (i = 0; i < 5; i++)
	{
		ioctl_data[i] = mcom_wakeup_time[i];
	}
	dprintk(20, "FP wake up time (local): MJD = %d, %02d:%02d:%02d\n", ((ioctl_data[0] << 8) + ioctl_data[1]), ioctl_data[2], ioctl_data[3], ioctl_data[4]); 
	dprintk(150, "%s <\n", __func__);
	return 0;
}

/*******************************************************
 *
 * mcom_GetWakeUpMode: get wake up reason.
 *
 * NOTE: currently hard coded to power on (1).
 *       The front processor does not seem to have
 *       capabilities for handling/reporting a wake up
 *       reason at all, not even reporting it on a
 *       boot command as later Crenova's do.
 */
int mcom_GetWakeUpMode(void)
{
	char comm_buf[10];
	int  res = 0;

	dprintk(150, "%s >\n", __func__);

	if (mcom_version[0] == 4)  // 4.x.x
	{
		ioctl_data[0] = 1;
	}
	else if (mcom_version[0] == 5)  // 5.x.x
	{
		ioctl_data[0] = 1;
	}
	else if (mcom_version[0] == 2)  // 2.x.x
	{
#if 1
		ioctl_data[0] = 1;
#else  // TODO: try and find command
		// send "FP_CMD_WAKEUPREASON"
		comm_buf[_TAG] = FP_CMD_WAKEUPREASON;
		comm_buf[_LEN] = 8;
		comm_buf[_VAL + 0] = 0;
		comm_buf[_VAL + 1] = 1;
		comm_buf[_VAL + 2] = 2;
		comm_buf[_VAL + 3] = 3;
		comm_buf[_VAL + 4] = 4;
		comm_buf[_VAL + 5] = 5;
		comm_buf[_VAL + 6] = 6;
		comm_buf[_VAL + 7] = 7;
	
//		memset(comm_buf + _VAL, 1, 8); // why is this?
		res |= mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);
		if (res != 0)
		{
			dprintk(150, "%s < error = %d\n", __func__, res);
			return -1;
		}
		ioctl_data[0] = 1;
		msleep(100);
#endif
	}
	else
	{
		ioctl_data[0] = 0;
	}
	dprintk(150, "%s <\n", __func__);
	return 0;
}

/*******************************************************
 *
 * mcom_SetDisplayOnOff: switch display on or off.
 *
 */
int mcom_SetDisplayOnOff(int on)
{
	int  res = 0;
	char buf[DISPLAY_WIDTH + _VAL];

	if (on)
	{
		res = mcom_WriteString(lastdata.data, lastdata.length);
	}
	else
	{
		buf[0] = FP_CMD_DISPLAY;
		buf[1] = DISPLAY_WIDTH;
		memset(buf + _VAL, 0, DISPLAY_WIDTH);  // fill command buffer with 'spaces'
		res = mcom_WriteCommand(buf, _VAL + buf[_LEN], 1);
	}
	return res;
}		

#if defined(VFDTEST)
/*****************************************************************
 *
 * mcom_VfdTest: Send arbitrary bytes to front processor
 *               and read back status, dataflag and if
 *               applicable return bytes (for development
 *               purposes).
 *
 */
int mcom_VfdTest(unsigned char *data)
{
	unsigned char buffer[12];
	int  res = 0;
	int  i;

	dprintk(150, "%s >\n", __func__);

	memset(buffer, 0, sizeof(buffer));
	memset(ioctl_data, 0, sizeof(ioctl_data));

	memcpy(buffer, data + 1, data[0]);  // data[0] = number of bytes

	if (paramDebug >= 50)
	{
		dprintk(1, "Send command: 0x%02x | 0x%02x | ", buffer[0] & 0xff, data[0] & 0xff);
		for (i = 1; i < data[0]; i++)
		{
			printk("0x%02x ", buffer[i] & 0xff);
		}
		printk("(length = 0x%02x)\n", data[0] & 0xff);
	}

	errorOccured = 0;
//	res = mcom_WriteCommand(buffer, data[0], 0);

	if (errorOccured == 1)
	{
		/* error */
		memset(data + 1, 0, 9);
		dprintk(1, "Error in function %s\n", __func__);
		res = data[0] = -ETIMEDOUT;
	}
	else
	{
		if (dataflag == 0)
		{
			memset(data, 0, sizeof(data));
		}
		else
		{
			data[0] = 0;  // command went OK
			data[1] = 1; // ioctl_data[2];  // # of bytes received
			for (i = 0; i <= data[1]; i++)
			{
				data[i + 2] = ioctl_data[i];  // copy return data
			}
			// send response if required
#if 1
			switch (ioctl_data[1])  // determine answer byte
			{
				case FP_RSP_TIME:
				{
					dprintk(50, "Note: sending response on 0x%02x\n", FP_RSP_TIME);
					mcom_SendResponse(buffer);
					msleep(100);
					break;
				}
				case FP_RSP_VERSION:
				{
					dprintk(50, "Note: sending response on 0x%02x\n", FP_RSP_VERSION);
					mcom_SendResponse(buffer);
					msleep(100);
					break;
				}
				default:
				{
					break;
				}
			}
#endif
		}
	}	dprintk(150, "%s <\n", __func__);
	return res;
}
#endif

/******************************************************
 *
 * Convert UTF-8 formatted text to display text for
 * early VFD models.
 *
 * Returns corrected length.
 *
 */
int mcom_utf8conv(unsigned char *text, unsigned char len)
{
	int i;
	int wlen;
	unsigned char kbuf[64];
	unsigned char *UTF_Char_Table = NULL;

	text[len] = 0x00;  // terminate text

	dprintk(150, "%s > Text: [%s], len = %d\n", __func__, text, len);

	wlen = 0;  // output index
	i = 0;  // input index
	memset(kbuf, 0x20, sizeof(kbuf));  // fill output buffer with spaces

	while ((i < len) /* && (wlen <= DISPLAY_WIDTH) */)
	{
		if (text[i] == 0x00)
		{
//			kbuf[wlen] = 0;  // terminate text
			break;  // stop processing
		}
		else if (text[i] >= 0x20 && text[i] < 0x80)  // if normal ASCII, but not a control character
		{
			kbuf[wlen] = text[i];
			wlen++;
		}
		else if (text[i] < 0xe0)
		{
			switch (text[i])
			{
				case 0xc2:
				{
					if (text[i + 1] < 0xa0)
					{
						i++; // skip non-printing character
					}
					else
					{
						UTF_Char_Table = VFD_OLD_UTF_C2;
					}
					break;
				}
				case 0xc3:
				{
					UTF_Char_Table = VFD_OLD_UTF_C3;
					break;
				}
				case 0xc4:
				{
					UTF_Char_Table = VFD_OLD_UTF_C4;
					break;
				}
				case 0xc5:
				{
					UTF_Char_Table = VFD_OLD_UTF_C5;
					break;
				}
#if 0  // Cyrillic currently not supported
				case 0xd0:
				{
					UTF_Char_Table = VFD_OLD_UTF_D0;
					break;
				}
				case 0xd1:
				{
					UTF_Char_Table = VFD_OLD_UTF_D1;
					break;
				}
#endif
				default:
				{
					UTF_Char_Table = NULL;
				}
			}
			i++;  // skip UTF-8 lead in
			if (UTF_Char_Table)
			{
				kbuf[wlen] = UTF_Char_Table[text[i] & 0x3f];
				wlen++;
			}
		}
		else
		{
			if (text[i] < 0xF0)
			{
				i += 2;  // skip 2 bytes
			}
			else if (text[i] < 0xF8)
			{
				i += 3;  // skip 3 bytes
			}
			else if (text[i] < 0xFC)
			{
				i += 4;  // skip 4 bytes
			}
			else
			{
				i += 5;  // skip 5 bytes
			}
		}
		i++;
	}
	memset(text, 0x00, sizeof(kbuf) + 1);
	memcpy(text, kbuf, wlen + 1);
	dprintk(150, "%s < Converted text: [%s], wlen = %d\n", __func__, text, wlen);
	return wlen;
}


/*****************************************************************
 *
 * mcom_WriteString: Display a text on the front panel display.
 *
 * Note: does not scroll; displays the first DISPLAY_WIDTH
 *       characters. Scrolling is available by writing to
 *       /dev/vfd: this scrolls a maximum of 64 characters once
 *       if the text length exceeds DISPLAY_WIDTH.
 *
 */
int mcom_WriteString(unsigned char *aBuf, int len)
{
	unsigned char bBuf[DISPLAY_WIDTH + _VAL + 1];
	unsigned char cBuf[64 + 1];
	int i = 0;
	int j = 0;
	int payload_len;
	int res = 0;

	dprintk(150, "%s >\n", __func__);

	if (len == 0)
	{
		return 0;
	}
	// leave input buffer untouched and work with a copy
	memset(cBuf, 0, sizeof(cBuf));
	strncpy(cBuf, aBuf, len);

	if (mcom_GetDisplayType() == _VFD_OLD)
	{
		len = mcom_utf8conv(cBuf, len);  // process UTF-8
	}

	if (len > DISPLAY_WIDTH)
	{
		len = DISPLAY_WIDTH;  // display DISPLAY_WIDTH characters maximum
	}
	cBuf[len] = 0;
	dprintk(70, "%s: Display text: [%s] len = %d\n", __func__, cBuf, len);

	memset(bBuf, 0, sizeof(bBuf));  // fill output buffer with 'spaces'
	payload_len = 0;

	// build command argument
	for (i = 0; i < len; i++)
	{
		if (mcom_GetDisplayType() == _VFD)  // new VFD display (HD 9600 PRIMA models)
		{
			memcpy(bBuf + _VAL + payload_len, mcom_ascii_char_14seg_new[cBuf[i]], 2);
			payload_len += 2;
		}
		else if (mcom_GetDisplayType() == _7SEG)  // 4 digit 7 segment display (HD 9600 Mini models)
		{
			memcpy(bBuf + _VAL + payload_len, mcom_ascii_char_7seg[cBuf[i]], 1);
			payload_len++;
		}
		else  // default to _VFD_OLD (HD 9600 models)
		{
			bBuf[i + _VAL] = mcom_ascii_char_14seg[cBuf[i]];
			payload_len++;
		}
	}

	/* complete command write */
	bBuf[0] = FP_CMD_DISPLAY;
	bBuf[1] = DISPLAY_WIDTH;  // payload_len;

	res = mcom_WriteCommand(bBuf, _VAL + bBuf[_LEN], 1);

	/* save last string written to FP */
	memcpy(&lastdata.data, cBuf, len + 1);  // save display text for /dev/vfd read
	lastdata.length = len;  // save length for /dev/vfd read
	dprintk(150, "%s <\n", __func__);
	return res;
}

/****************************************************************
 *
 * mcom_init_func: Initialize the driver.
 *
 */
int mcom_init_func(void)
{
	unsigned char comm_buf[20];
	unsigned char k;
	unsigned char checksum;
	int           res;
	int           bootSendCount;

	dprintk(150, "%s >\n", __func__);
	sema_init(&write_sem, 1);

	bootSendCount = 3;  // allow three tries
	eb_flag = 0;  // flag wait for time

	while (bootSendCount)
	{
		// send BOOT command
		comm_buf[_TAG] = FP_CMD_BOOT;
		comm_buf[_LEN] = 1;
		comm_buf[_VAL] = 0x01;  // dummy

		errorOccured = 0;
//		dprintk(50, "Send BOOT command (0x%02x 0x%02x 0x%02x)\n", comm_buf[_TAG], comm_buf[_LEN], comm_buf[_VAL]);
		res = mcom_WriteCommand(comm_buf, 3, 1);

		if (errorOccured == 1)
		{
			memset(ioctl_data, 0, 8);
			dprintk(1, "%s: Error sending BOOT command\n", __func__);
			msleep(100);
			bootSendCount--;
			goto MCOM_RETRY;
		}

		res = 1000;  // time out is one second
		
		while (res != 0 && eb_flag != 2)  // wait until FP has sent time and version
		{
			msleep(1);
			res --;
		}
		if (res == 0)
		{
			dprintk(1, "%s: Error; timeout on BOOT_CMD\n", __func__);
			bootSendCount--;
			goto MCOM_RETRY;
		}
		break;

MCOM_RETRY:
		bootSendCount--;
	}
	if (bootSendCount <= 0)
	{
		dprintk(1, "%s < Error: Front processor is not responding.\n", __func__);
		return -1;
	}

	// set initial wake up time to 01-01-2000 00:00:00 -> MJD = 51544
	mcom_wakeup_time[0] = 51544 >> 8;
	mcom_wakeup_time[1] = 51544 & 0xff;
	mcom_wakeup_time[2] = 0;
	mcom_wakeup_time[3] = 0;
	mcom_wakeup_time[4] = 0;
		
	// Handle initial GMT offset (may be changed by writing to /proc/stb/fp/rtc_offset)
	res = strict_strtol(gmt_offset, 10, (long *)&rtc_offset);
	if (res && gmt_offset[0] == '+')
	{
		res = strict_strtol(gmt_offset + 1, 10, (long *)&rtc_offset);
	}
	dprintk(150, "%s <\n", __func__);
	return 0;
}

/****************************************
 *
 * Code for writing to /dev/vfd
 *
 */
void clear_display(void)
{
	unsigned char bBuf[9];
	int res = 0;

	dprintk(150, "%s >\n", __func__);

	memset(bBuf, ' ', sizeof(bBuf));
	bBuf[DISPLAY_WIDTH] = 0;
	res = mcom_WriteString(bBuf, DISPLAY_WIDTH);
	dprintk(150, "%s <\n", __func__);
}

static ssize_t MCOMdev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	char *kernel_buf;
	int minor;
	int vLoop;
	int res = 0;
	int llen;
	int offset = 0;
	char buf[64 + DISPLAY_WIDTH];
	char *b;

	dprintk(150, "%s >\n", __func__);
	dprintk(100, "%s len %d, offs %d\n", __func__, len, (int) *off);

	if (len == 0)
	{
		return len;
	}

	minor = -1;
	for (vLoop = 0; vLoop < LASTMINOR; vLoop++)
	{
		if (FrontPanelOpen[vLoop].fp == filp)
		{
			minor = vLoop;
		}
	}
	if (minor == -1)
	{
		dprintk(1, "Error: Bad Minor\n");
		return -1; //FIXME
	}
	dprintk(150, "minor = %d\n", minor);

	/* do not write to the remote control */
	if (minor == FRONTPANEL_MINOR_RC)
	{
		return -EOPNOTSUPP;
	}
	kernel_buf = kmalloc(len, GFP_KERNEL);

	if (kernel_buf == NULL)
	{
		dprintk(1, "%s < Return -ENOMEM\n", __func__);
		return -ENOMEM;
	}

	copy_from_user(kernel_buf, buff, len);

	if (write_sem_down())
	{
		return -ERESTARTSYS;
	}
	llen = len;  // preserve len for return value

	if (llen >= 64)  // do not display more than 64 characters
	{
		llen = 64;
	}
	// Strip possible trailing LF
	if (kernel_buf[llen - 1] == '\n')
	{
		llen--;
	}
	kernel_buf[llen] = 0;

	if (llen <= DISPLAY_WIDTH)  // no scroll
	{
//		dprintk(70, "%s No scroll (llen = %d)\n", __func__, len);
		res = mcom_WriteString(kernel_buf, llen);
	}
	else  // scroll, display string is longer than display length
	{
//		dprintk(70, "%s Scroll (llen = %d)\n", __func__, llen);
		// initial display starting at 2nd position to ease reading
		memset(buf, ' ', sizeof(buf));
		offset = 2;
		memcpy(buf + offset, kernel_buf, llen);
		llen += offset;
		buf[llen + DISPLAY_WIDTH] = '\0';  // terminate string

		// scroll text
		b = buf;
		for (vLoop = 0; vLoop < llen; vLoop++)
		{
			dprintk(70, "%s Text to scroll [%s]\n", __func__, b);
			res |= mcom_WriteString(b + vLoop, DISPLAY_WIDTH);
			// sleep 300 ms
			msleep(300);
		}
		clear_display();

		// final display
		res |= mcom_WriteString(kernel_buf, DISPLAY_WIDTH);
	}
	kfree(kernel_buf);
	write_sem_up();
	dprintk(150, "%s < res=%d len=%d\n", __func__, res, len);

	if (res < 0)
	{
		return res;
	}
	else
	{
		return len;
	}
}

/**********************************
 *
 * Code for reading from /dev/vfd
 *
 */
static ssize_t MCOMdev_read(struct file *filp, char __user *buff, size_t len, loff_t *off)
{
	int minor, vLoop;

	dprintk(150, "%s > (len = %d, offs = %d)\n", __func__, len, (int) *off);

	if (len == 0)
	{
		return len;
	}
	minor = -1;
	for (vLoop = 0; vLoop < LASTMINOR; vLoop++)
	{
		if (FrontPanelOpen[vLoop].fp == filp)
		{
			minor = vLoop;
		}
	}
	if (minor == -1)
	{
		dprintk(1, "%s Error: Bad Minor\n", __func__);
		return -EUSERS;
	}
	dprintk(150, "minor = %d\n", minor);

	if (minor == FRONTPANEL_MINOR_RC)  // includes front panel keyboard
	{
		int           size = 0;
		unsigned char data[20];

		memset(data, 0, sizeof(data));

		getRCData(data, &size);

		if (size > 0)
		{
			if (down_interruptible(&FrontPanelOpen[minor].sem))
			{
				return -ERESTARTSYS;
			}
			copy_to_user(buff, data, size);

			up(&FrontPanelOpen[minor].sem);

			dprintk(100, "%s < RC, size = %d\n", __func__, size);
			return size;
		}
		//printk("size = %d\n", size);
		dumpValues();
		return 0;
	}
	/* copy the current display string to the user */
	if (down_interruptible(&FrontPanelOpen[minor].sem))
	{
		dprintk(1, "%s < Return -ERESTARTSYS\n", __func__);
		return -ERESTARTSYS;
	}
	if (FrontPanelOpen[minor].read == lastdata.length)
	{
		FrontPanelOpen[minor].read = 0;

		up(&FrontPanelOpen[minor].sem);
		dprintk(150, "%s <\n", __func__);
		return 0;
	}
	if (len > lastdata.length)
	{
		len = lastdata.length;
	}
	/* fixme: needs revision because of utf8! */
	if (len > 16)
	{
		len = 16;
	}
	FrontPanelOpen[minor].read = len;
	copy_to_user(buff, lastdata.data, len);
	up(&FrontPanelOpen[minor].sem);
	dprintk(150, "%s < (len = %d)\n", __func__, len);
	return len;
}

int MCOMdev_open(struct inode *inode, struct file *filp)
{
	int minor;

	dprintk(150, "%s >\n", __func__);

	/* needed! otherwise a race condition can occur */
	if (down_interruptible(&write_sem))
	{
		return -ERESTARTSYS;
	}
	minor = MINOR(inode->i_rdev);

	dprintk(150, "Open minor %d\n", minor);

	if (FrontPanelOpen[minor].fp != NULL)
	{
		dprintk(1, "%s < return -EUSERS\n", __func__);
		up(&write_sem);
		return -EUSERS;
	}
	FrontPanelOpen[minor].fp = filp;
	FrontPanelOpen[minor].read = 0;
	up(&write_sem);
	dprintk(150, "%s <\n", __func__);
	return 0;

}

int MCOMdev_close(struct inode *inode, struct file *filp)
{
	int minor;

//	dprintk(150, "%s >\n", __func__);

	minor = MINOR(inode->i_rdev);

	dprintk(70, "Close minor %d\n", minor);

	if (FrontPanelOpen[minor].fp == NULL)
	{
		dprintk(1, "%s < return -EUSERS\n", __func__);
		return -EUSERS;
	}
	FrontPanelOpen[minor].fp = NULL;
	FrontPanelOpen[minor].read = 0;

//	dprintk(150, "%s <\n", __func__);
	return 0;
}

/****************************************
 *
 * IOCTL handling.
 *
 */
static int MCOMdev_ioctl(struct inode *Inode, struct file *File, unsigned int cmd, unsigned long arg)
{
	struct opt9600_fp_ioctl_data *mcom = (struct opt9600_fp_ioctl_data *)arg;
	struct vfd_ioctl_data *data = (struct vfd_ioctl_data *)arg;
	int res = 0;

	dprintk(150, "%s > 0x%.8x\n", __func__, cmd);

	if (down_interruptible(&write_sem))
	{
		return -ERESTARTSYS;
	}

	switch (cmd)
	{
		case VFDDRIVERINIT:
		{
			res = mcom_init_func();
			break;
		}
		case VFDSTANDBY:
		{
			res = mcom_SetStandby(mcom->u.standby.time, 1, 1);  // show clock in 24h format
			break;
		}
		case VFDSETTIME:
		{
			if (mcom->u.time.localTime != 0)
			{
				res = mcom_SetTime(mcom->u.time.localTime);
			}
			break;
		}
		case VFDGETTIME:
		{
			res = mcom_GetTime();
			copy_to_user((void *)arg, &ioctl_data, 5);
			break;
		}
		case VFDSETPOWERONTIME:
		{
			dprintk(10, "Set wake up time: (MJD= %d) - %02d:%02d:%02d (local)\n", (mcom->u.standby.time[0] & 0xff) * 256 + (mcom->u.standby.time[1] & 0xff),
				mcom->u.standby.time[2], mcom->u.standby.time[3], mcom->u.standby.time[4]);
			res = mcom_SetWakeUpTime(mcom->u.standby.time);
			break;
		}
		case VFDGETWAKEUPTIME:
		{
			dprintk(10, "Wake up time: (MJD= %d) - %02d:%02d:%02d (local)\n", (((mcom_wakeup_time[0] & 0xff) << 8) + (mcom_wakeup_time[1] & 0xff)),
				mcom_wakeup_time[2], mcom_wakeup_time[3], mcom_wakeup_time[4]);
			copy_to_user((void *)arg, &mcom_wakeup_time, 5);
			res = 0;
			break;
		}
		case VFDGETWAKEUPMODE:
		{
			res = mcom_GetWakeUpMode();
			copy_to_user((void *)arg, &ioctl_data, 1);
			break;
		}
		case VFDDISPLAYCHARS:
		{
			res = mcom_WriteString(data->data, data->length);
			break;
		}
		case VFDDISPLAYWRITEONOFF:
		{
			res = mcom_SetDisplayOnOff(mcom->u.light.onoff);
			break;
		}
		case VFDICONDISPLAYONOFF:
		{
			dprintk(1, "VFDICONDISPLAYONOFF not supported on this receiver\n");
			break;
		}
		case VFDBRIGHTNESS:
		{
			dprintk(1, "VFDBRIGHTNESS not supported on this receiver\n");
			break;
		}
#if defined(VFDTEST)
		case VFDTEST:
		{
			res = mcom_VfdTest(mcom->u.test.data);
			res |= copy_to_user((void *)arg, &(mcom->u.test.data), ((mcom->u.test.data[1] != 0) ? 8 : 2));
			break;
		}
#endif
		case 0x5305:  // Neutrino sends this
		{
			break;
		}
		default:
		{
			dprintk(1, TAGDEBUG"Unknown IOCTL 0x%x\n", cmd);
			break;
		}
	}
	up(&write_sem);
	dprintk(150, "%s <\n", __func__);
	return res;
}

struct file_operations vfd_fops =
{
	.owner    = THIS_MODULE,
	.ioctl    = MCOMdev_ioctl,
	.write    = MCOMdev_write,
	.read     = MCOMdev_read,
	.open     = MCOMdev_open,
	.release  = MCOMdev_close
};
// vim:ts=4
