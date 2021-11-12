/****************************************************************************
 *
 * cn_micom_file.c
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2021 Audioniek: debugged on Atemio520
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
 * 20210915 Audioniek       Initial version.
 * 20211024 Audioniek       LED models now have full ASCII character set.
 * 20211027 Audioniek       Handling of RSP_TIME, RSP_VERSION and RSP_PRIVATE
 *                          moved to cn_micom_main.c.
 * 20211031 Audioniek       Add processing of PRIVATE preamble.
 * 20211031 Audioniek       Improve handling of shut down time in setStandby.
 * 20211031 Audioniek       Add mechanism for determining start because of
 *                          timer to provide a sensible wake up reason.
 * 20211101 Audioniek       Fix crash in UTF8 conversion.
 * 20211107 Audioniek       Fix errors in wakeup time calculation.
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

#include "cn_micom_fp.h"
#include "cn_micom_asc.h"
#include "cn_micom_utf.h"
#include "cn_micom_char.h"

extern void      ack_sem_up(void);
extern int       ack_sem_down(void);
int              mcom_WriteString(unsigned char *aBuf, int len);
extern void      mcom_putc(unsigned char data);

struct semaphore write_sem;
int              errorOccured = 0;
extern int       dataflag;
unsigned char    ioctl_data[12];
int              eb_flag = 0;  // response counter for boot command
int              preamblecount;  // preamble counter for boot command
unsigned char    rsp_cksum;  // checksum over last command sent

tFrontPanelOpen  FrontPanelOpen [LASTMINOR];

// these are set when the driver starts
time_t           mcom_time_set_time;  // system time when FP clock time was received on BOOT
unsigned char    mcom_time[12];  // FP time read by FP BOOT command (format YYMMDDHHMMWWWWUU WWWW=remaining timer minutes, UU = wakeupreason)

static int       mcom_settime_set_time_flag = 0;  // flag: new FP clock time was specified
static time_t    mcom_settime_set_time;  // system time when mcom_settime was input
time_t           mcom_settime;  // time to set FP clock to when shutting down

static int       mcom_wakeup_time_set_flag = 0;  // flag: new FP wake up time was specified
unsigned char    mcom_wakeup_time[5];  // wake up time specified (format MJD H M S)

unsigned char    mcom_version[4];  // FP software version
unsigned char    mcom_private[8];
unsigned char    *wakeupName[3] = { "power on", "from deep standby", "timer" };
int              wakeupreason = 0;  // default to power on
int              displayType = _7SEG;  // default to LED display

struct saved_data_s lastdata;

extern void calcSetmcomTime(time_t theTime, char *destString);

/**************************************************
 *
 * Character table for LED models
 *
 * Order is ASCII.
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
	_7SEG_ASC21,  // 0x21, !
	_7SEG_ASC22,  // 0x22, "
	_7SEG_ASC23,  // 0x23, #
	_7SEG_ASC24,  // 0x24, $
	_7SEG_ASC25,  // 0x25, %
	_7SEG_ASC26,  // 0x26, &
	_7SEG_ASC27,  // 0x27, '
	_7SEG_ASC28,  // 0x28, (
	_7SEG_ASC29,  // 0x29, )
	_7SEG_ASC2A,  // 0x2a, *
	_7SEG_ASC2B,  // 0x2b, +
	_7SEG_ASC2C,  // 0x2c, ,
	_7SEG_ASC2D,  // 0x2d, -
	_7SEG_ASC2E,  // 0x2e, .
	_7SEG_ASC2F,  // 0x2f, /

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
	_7SEG_ASC3A,  // 0x3a, :
	_7SEG_ASC3B,  // 0x3b, ;
	_7SEG_ASC3C,  // 0x3c, <
	_7SEG_ASC3D,  // 0x3d, =
	_7SEG_ASC3E,  // 0x3e, >
	_7SEG_ASC3F,  // 0x3f, ?

	_7SEG_ASC40,  // 0x40, @ 
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
	_7SEG_ASC5B,  // 0x5b  [
	_7SEG_ASC5C,  // 0x5c, |
	_7SEG_ASC5D,  // 0x5d, ]
	_7SEG_ASC5E,  // 0x5e, ^
	_7SEG_ASC5F,  // 0x5f, _

	_7SEG_ASC60,  // 0x60, `
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
	_7SEG_ASC7B,  // 0x7b, {
	_7SEG_ASC7C,  // 0x7c, backslash
	_7SEG_ASC7D,  // 0x7d, }
	_7SEG_ASC7E,  // 0x7e, ~
	_7SEG_ASC7F   // 0x7f, <DEL>
};

#if 0
/**************************************************
 *
 * Character table for early VFD models
 *
 * Order is ASCII.
 *
 * NOTE: lower case letters are translated
 * to uppercase
 */
static unsigned char mcom_ascii_char_14seg_old[] =
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
#endif

/**************************************************
 *
 * Character table for late VFD models
 *
 * Order is ASCII.
 */
static char *mcom_ascii_char_14seg[] =
{
	_14SEG_NULL,  // 0x00, unprintable control character
	_14SEG_NULL,  // 0x01, unprintable control character
	_14SEG_NULL,  // 0x02, unprintable control character
	_14SEG_NULL,  // 0x03, unprintable control character
	_14SEG_NULL,  // 0x04, unprintable control character
	_14SEG_NULL,  // 0x05, unprintable control character
	_14SEG_NULL,  // 0x06, unprintable control character
	_14SEG_NULL,  // 0x07, unprintable control character
	_14SEG_NULL,  // 0x08, unprintable control character
	_14SEG_NULL,  // 0x09, unprintable control character
	_14SEG_NULL,  // 0x0a, unprintable control character
	_14SEG_NULL,  // 0x0b, unprintable control character
	_14SEG_NULL,  // 0x0c, unprintable control character
	_14SEG_NULL,  // 0x0d, unprintable control character
	_14SEG_NULL,  // 0x0e, unprintable control character
	_14SEG_NULL,  // 0x0f, unprintable control character

	_14SEG_NULL,  // 0x10, unprintable control character
	_14SEG_NULL,  // 0x11, unprintable control character
	_14SEG_NULL,  // 0x12, unprintable control character
	_14SEG_NULL,  // 0x13, unprintable control character
	_14SEG_NULL,  // 0x14, unprintable control character
	_14SEG_NULL,  // 0x15, unprintable control character
	_14SEG_NULL,  // 0x16, unprintable control character
	_14SEG_NULL,  // 0x17, unprintable control character
	_14SEG_NULL,  // 0x18, unprintable control character
	_14SEG_NULL,  // 0x19, unprintable control character
	_14SEG_NULL,  // 0x1a, unprintable control character
	_14SEG_NULL,  // 0x1b, unprintable control character
	_14SEG_NULL,  // 0x1c, unprintable control character
	_14SEG_NULL,  // 0x1d, unprintable control character
	_14SEG_NULL,  // 0x1e, unprintable control character
	_14SEG_NULL,  // 0x1f, unprintable control character

	_14SEG_NULL,  // 0x20, <space>
	_14SEG_NULL,  // 0x21, !
	_14SEG_NULL,  // 0x22, "
	_14SEG_NULL,  // 0x23, #
	_14SEG_NULL,  // 0x24, $
	_14SEG_NULL,  // 0x25, %
	_14SEG_NULL,  // 0x26, &
	_14SEG_NULL,  // 0x27, '
	_14SEG_NULL,  // 0x28, (
	_14SEG_NULL,  // 0x29, )
	_14SEG_NULL,  // 0x2a, *
	_14SEG_NULL,  // 0x2b, +
	_14SEG_NULL,  // 0x2c, ,
	_14SEG_NULL,  // 0x2d, -
	_14SEG_NULL,  // 0x2e, .
	_14SEG_NULL,  // 0x2f, /

	_14SEG_NUM_0, // 0x30, 0
	_14SEG_NUM_1, // 0x31, 1
	_14SEG_NUM_2, // 0x32, 2
	_14SEG_NUM_3, // 0x33, 3
	_14SEG_NUM_4, // 0x34, 4
	_14SEG_NUM_5, // 0x35, 5
	_14SEG_NUM_6, // 0x36, 6
	_14SEG_NUM_7, // 0x37, 7
	_14SEG_NUM_8, // 0x38, 8
	_14SEG_NUM_9, // 0x39, 9
	_14SEG_NULL,  // 0x3a, :
	_14SEG_NULL,  // 0x3b, ;
	_14SEG_NULL,  // 0x3c, <
	_14SEG_NULL,  // 0x3d, =
	_14SEG_NULL,  // 0x3e, >
	_14SEG_NULL,  // 0x3f, ?

	_14SEG_NULL,  // 0x40, @
	_14SEG_CH_A,  // 0x41, A
	_14SEG_CH_B,  // 0x42, B
	_14SEG_CH_C,  // 0x43, C
	_14SEG_CH_D,  // 0x44, D
	_14SEG_CH_E,  // 0x45, E
	_14SEG_CH_F,  // 0x46, F
	_14SEG_CH_G,  // 0x47, G
	_14SEG_CH_H,  // 0x48, H
	_14SEG_CH_I,  // 0x49, I
	_14SEG_CH_J,  // 0x4a, J
	_14SEG_CH_K,  // 0x4b, K
	_14SEG_CH_L,  // 0x4c, L
	_14SEG_CH_M,  // 0x4d, M
	_14SEG_CH_N,  // 0x4e, N
	_14SEG_CH_O,  // 0x4f, O

	_14SEG_CH_P,  // 0x50, P
	_14SEG_CH_Q,  // 0x51, Q
	_14SEG_CH_R,  // 0x52, R
	_14SEG_CH_S,  // 0x53, S
	_14SEG_CH_T,  // 0x54, T
	_14SEG_CH_U,  // 0x55, U
	_14SEG_CH_V,  // 0x56, V
	_14SEG_CH_W,  // 0x57, W
	_14SEG_CH_X,  // 0x58, X
	_14SEG_CH_Y,  // 0x59, Y
	_14SEG_CH_Z,  // 0x5a, Z
	_14SEG_NULL,  // 0x5b  [
	_14SEG_NULL,  // 0x5c, |
	_14SEG_NULL,  // 0x5d, ]
	_14SEG_NULL,  // 0x5e, ^
	_14SEG_NULL,  // 0x5f, _

	_14SEG_NULL,  // 0x60, `
	_14SEG_CH_a,  // 0x61, a
	_14SEG_CH_b,  // 0x62, b
	_14SEG_CH_c,  // 0x63, c
	_14SEG_CH_d,  // 0x64, d
	_14SEG_CH_e,  // 0x65, e
	_14SEG_CH_f,  // 0x66, f
	_14SEG_CH_g,  // 0x67, g
	_14SEG_CH_h,  // 0x68, h
	_14SEG_CH_i,  // 0x69, i
	_14SEG_CH_j,  // 0x6a, j
	_14SEG_CH_k,  // 0x6b, k
	_14SEG_CH_l,  // 0x6c, l
	_14SEG_CH_m,  // 0x6d, m
	_14SEG_CH_n,  // 0x6e, n
	_14SEG_CH_o,  // 0x6f, o

	_14SEG_CH_p,  // 0x70, p
	_14SEG_CH_q,  // 0x71, q
	_14SEG_CH_r,  // 0x72, r
	_14SEG_CH_s,  // 0x73, s
	_14SEG_CH_t,  // 0x74, t
	_14SEG_CH_u,  // 0x75, u
	_14SEG_CH_v,  // 0x76, v
	_14SEG_CH_w,  // 0x77, w
	_14SEG_CH_x,  // 0x78, x
	_14SEG_CH_y,  // 0x79, y
	_14SEG_CH_z,  // 0x7a, z
	_14SEG_NULL,  // 0x7b, {
	_14SEG_NULL,  // 0x7c, backslash
	_14SEG_NULL,  // 0x7d, }
	_14SEG_NULL,  // 0x7e, ~
	_14SEG_NULL   // 0x7f, <DEL>--> all segments on
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
 * Debug: Display hexdump of front processor traffic.
 *
 */
static void mcom_Dump(char *string, unsigned char *v_pData)
{
	int           i;
	char          str[10], line[80], txt[25];
	unsigned char *pData;
	unsigned short v_szData;

	if ((v_pData == NULL) || (paramDebug < 70))
	{
		return;
	}

	v_szData = v_pData[_LEN] + 2;  // get total length
	dprintk(0, "[ %s (length %d) ]\ncmd:", string, v_szData);

	i = 0;
	pData = v_pData;
	line[0] = '\0';

	while ((unsigned short)(pData - v_pData) < v_szData)
	{
		sprintf(str, " %02x", *pData);
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
unsigned char mcom_Checksum(unsigned char *payload, int len)
{
	int           n;
	unsigned char checksum = 0;

	for (n = 0; n < len; n++)
	{
		checksum ^= payload[n];
	}
//	rsp_cksum = (unsigned char)checksum & 0xff;
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

//	dprintk(150, "%s >\n", __func__);

	if (buffer[1] != len - 2)
	{
		dprintk(1, "%s: Error: command length mismatch, len = %d, cmd = %d\n", __func__, len, buffer[1] + 2);
	}

	if (paramDebug > 50)
	{
		dprintk(0, "Send Front uC command ");
		for (i = 0; i < len; i++)
		{
			printk("0x%02x ", buffer[i] & 0xff);
		}
		printk(" (len = %d)\n", len);
	}

	for (i = 0; i < len; i++)
	{
#ifdef DIRECT_ASC  // not defined anywhere
		serial_putc(buffer[i]);
#else
		mcom_putc(buffer[i]);
#endif
	}

	rsp_cksum = mcom_Checksum(buffer, buffer[_LEN] + _VAL);  // determine checksum
	if (needAck)
	{
		if (ack_sem_down())
		{
			return -ERESTARTSYS;
		}
	}
//	dprintk(150, "%s < \n", __func__);
	return 0;
}

/*****************************************************************
 *
 * mcom_GetDisplayType: Return type of frontpanel display.
 *
 */
static DISPLAYTYPE mcom_GetDisplayType(void)
{
	if (mcom_version[0] == 4)  // if Miniline
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
 * Note: year must include century
 *
 */
static unsigned short mcomYMD2MJD(unsigned short Y, unsigned char M, unsigned char D)
{  // converts YMD to 16 bit MJD
	int L;

	Y -= 1900;
	L = ((M == 1) || (M == 2)) ? 1 : 0;
	return (unsigned short)(14956 + D + ((Y - L) * 36525 / 100) + ((M + 1 + L * 12) * 306001 / 10000));
}

void mcom_MJD2YMD(unsigned short usMJD, unsigned short *year, unsigned char *month, unsigned char *day)
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

/**************************************************************
 *
 * mcom_SetStandby: put receiver in deep standby.
 *
 * time = wake up time as MJD H M S
 *
 * NOTE: The front processor works with a number of
 *       minutes after shut down to wake up, whereby
 *       the of minutes is limited to 16777215.
 *       The latter equals 16777215 / 60 = 279620,25 hours
 *       or 16777215 / 60 / 24 = 11650 days and 20.25 hours
 *       or about 31 years and 335 days and 20.25 hours.
 *       At the time of writing this is beyond the maximum
 *       Linux time.
 *       The only check on the wake up time that is done for
 *       this reason is it being in the past.
 *       A wakeup time of LONG_MAX is regarded as a flag for
 *       no timer set -> remain in standby until woken up by
 *       the user.
 *
 */
int mcom_SetStandby(char *time, int showTime, int twentyfour)
{
	char comm_buf[16];
	int  res = 0;
	int  wakeup = 0;
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
//	unsigned char   curr_time_s;
	time_t          wake_time;
	struct timespec tp;
	int             after_min;  // wake up time expressed as minutes after set FP clock time

	dprintk(150, "%s >\n", __func__);

#if 0
	if (displayType() == _7SEG)
	{
		res = mcom_WriteString("WAIT", strlen("WAIT"));
	}
	else
	{
		res = mcom_WriteString("Bye bye", strlen("Bye bye"));
	}
#endif

	// Step one: determine system time
	// and what time to use to synchronize the FP clock
	tp = current_kernel_time();  // get system time (seconds since Linux epoch)
	dprintk(20, "Set FP time ");
	if (mcom_settime_set_time_flag)  // if time was set by VFDSETTIME
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
		printk(" %02d:%02d:00 %02d-%02d-%04d (seconds ignored)\n", curr_time_h, curr_time_m, day, month, year);
	}

	// Step two: if valid timer set or specified, set WAKEUP flag to timer
	// wake up time is sent as MJD H M S
	// convert wake up MJD to date
	mcom_MJD2YMD(((time[0] << 8) + time[1]), &wake_year, &wake_month, &wake_day);
	dprintk(20, "Wake up time supplied: %02d:%02d:%02d %02d-%02d-%04d\n", time[2], time[3], time[4], wake_day, wake_month, wake_year);

	// evaluate wake up time specified
	wake_time = calcGetmcomTime(time);  // convert time specified to a time_t

	if (wake_time < curr_time || wake_time == LONG_MAX)
	{  // it appears no timer was set, try specifyable wake up time
		dprintk(20, "No timer set (or in the past)\n");
		mcom_MJD2YMD(((mcom_wakeup_time[0] << 8) + mcom_wakeup_time[1]), &wake_year, &wake_month, &wake_day);
		wake_time = calcGetmcomTime(mcom_wakeup_time);  // get wake up time as time_t
		dprintk(20, "Current set wake up time: %02d:%02d:%02d %02d-%02d-%04d\n", mcom_wakeup_time[2], mcom_wakeup_time[3], mcom_wakeup_time[4], wake_day, wake_month, wake_year);

		if (mcom_wakeup_time_set_flag
		&&  wake_time > curr_time + 60)
		{  // wake up time specified is at least 1 minute later than system time -> use that
			mcom_MJD2YMD(((mcom_wakeup_time[0] << 8) + mcom_wakeup_time[1]), &wake_year, &wake_month, &wake_day);
			dprintk(20, "Using wake up time previously specified: %02d:%02d:%02d %02d-%02d-%04d\n",
				mcom_wakeup_time[2], mcom_wakeup_time[3], mcom_wakeup_time[4], wake_day, wake_month, wake_year);
			time[0] = mcom_wakeup_time[0];  // overwrite MJDh
			time[1] = mcom_wakeup_time[1];  // overwrite MJDl
			time[2] = mcom_wakeup_time[2];  // overwrite hours
			time[3] = mcom_wakeup_time[3];  // overwrite minutes
			time[4] = mcom_wakeup_time[4];  // overwrite seconds
			wake_time = calcGetmcomTime(time);
			wakeup = 2;  // flag timer wake up
		}
		else
		{  //  no wake up time was usable, do not send WAKEUP command
			dprintk(20, "Do not send wake up command\n");
			wakeup = 1;  // flag deep standby
		}
	}
	else
	{
		wakeup = 2;  // flag timer wake up
	}
	memset(comm_buf, 0, sizeof(comm_buf));

	// Step three: send "STANDBY"
	comm_buf[_TAG] = FP_CMD_STANDBY;  // 0xe5
	comm_buf[_LEN] = 5;
	comm_buf[_VAL + 0] = (showTime) ? 1 : 0;  // flag: show time in standby if 1
	comm_buf[_VAL + 1] = (twentyfour) ? 1: 0;  // 24h flag (24h mode = 1
	comm_buf[_VAL + 2] = (wakeup == 2) ? 1 : 0;  // flag: set wake up time command will follow if 1
	comm_buf[_VAL + 3] = 3;  // power on delay
	comm_buf[_VAL + 4] = 1;  // flag: send private data?

//	mcom_Dump("SEND STANDBY", comm_buf);
	res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);

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

//	mcom_Dump("SEND TIME", comm_buf);
	res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);
	if (res != 0)
	{
		goto _EXIT_MCOM_STANDBY;
	}
	msleep(100);

	// Step five: send "SETWAKEUPTIME" if a valid wake up time was found
	if (wakeup == 2)  // if timer set
	{
		comm_buf[_TAG] = FP_CMD_SETWAKEUPTIME;  // 0xea
		comm_buf[_LEN] = 3;

		// FP needs minutes after current FP time as wake up time
		after_min = (wake_time - curr_time) / 60;
		dprintk(20, "System will wake up in %d minutes\n", after_min);
				
		comm_buf[_VAL + 0] = (after_min >> 16) & 0xFF;  // ignored?
		comm_buf[_VAL + 1] = (after_min >>  8) & 0xFF;
		comm_buf[_VAL + 2] = (after_min >>  0) & 0xFF;
//		mcom_Dump("SEND SETWAKEUPTIME", comm_buf);
		res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);
		if (res != 0)
		{
			goto _EXIT_MCOM_STANDBY;
		}
		msleep(100);
	}

	// Step five or six: send "PRIVATE" on late VFD and LED models
	// NOTE: The data written with this command will be returned
	//       with the next boot command.
	if (mcom_version[0] > 2)
	{
		comm_buf[_TAG] = FP_CMD_PRIVATE;  // 0xe7
		comm_buf[_LEN] = 8;
		comm_buf[_VAL + 0] = mcom_private[0];
		comm_buf[_VAL + 1] = mcom_private[1];
		comm_buf[_VAL + 2] = mcom_private[2];
		comm_buf[_VAL + 3] = mcom_private[3];
		comm_buf[_VAL + 4] = mcom_private[4];
		comm_buf[_VAL + 5] = mcom_private[5];
		comm_buf[_VAL + 6] = mcom_private[6];
		comm_buf[_VAL + 7] = wakeup;

		res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);
		if (res != 0)
		{
			goto _EXIT_MCOM_STANDBY;
		}
		msleep(100);
	}
//	NOTE: Titan seems to repeat the sequence until power down occurs

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
 *
 */
int mcom_SetTime(time_t time)
{
	struct timespec tp;
	unsigned char timeString[5];
	unsigned short year;
	unsigned char month;
	unsigned char day;
	int res;
	char comm_buf[8];

//	dprintk(150, "%s >\n", __func__);

	calcSetmcomTime(time, timeString);
	mcom_MJD2YMD((unsigned short)(((timeString[0] & 0xff) << 8) + (timeString[1] & 0xff)), &year, &month, &day);
	dprintk(20, "FP time to set: %02d:%02d:%02d %02d-%02d-%04d\n", timeString[2], timeString[3], timeString[4], (int)day, (int)month, (int)year);
#if 1
	tp = current_kernel_time();  // get current system time
	dprintk(20,"%s: %d\n", __func__, tp.tv_sec);
	mcom_settime_set_time = tp.tv_sec;  // save time of setting
	mcom_settime = time;  // save time to set
	mcom_settime_set_time_flag = 1;  // and flag set time
#else  // TODO: following does not work
	comm_buf[_TAG] = FP_CMD_SETTIME;
	comm_buf[_LEN] = 5;
	comm_buf[_VAL + 0] = (year - 2000) & 0xff;  // strip century
	comm_buf[_VAL + 1] = month;
	comm_buf[_VAL + 2] = day;
	comm_buf[_VAL + 3] = timeString[2];
	comm_buf[_VAL + 4] = timeString[3];
	// NOTE: no seconds
	res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);
	if (res != 0)
	{
		return -1;
	}
	msleep(100);
#endif
//	dprintk(150, "%s <\n", __func__);
	return 0;
}

/*******************************************************
 *
 * mcom_GetTime: get front processor time.
 *
 * The front processor does not have a command to set
 * its clock time directly.
 *
 * To circumvent this, this function returns the time
 * read when the FP clock was read at the last BOOT
 * command, plus the time the system time has advanced
 * since then, unless a new clocktime was specified
 * using mcom_SetTime.
 * NOTE: the first case will miserably fail when the
 *       system time is set to a new value between the
 *       BOOT command and calling this function!
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

//	dprintk(150, "%s >\n", __func__);

	tp_now = current_kernel_time();  // get current system time

	if (mcom_settime_set_time_flag)  // if a new time was previously input
	{
		FP_time = mcom_settime + (tp_now.tv_sec - mcom_settime_set_time);  // calculate "FP time"
		calcSetmcomTime(FP_time, ioctl_data);
	}
	else  // get boot time FP time
	{
		cur_mjd = mcomYMD2MJD(mcom_time[0] + 2000, mcom_time[1], mcom_time[2]);

		time_in_sec =  mcom_time[3] * 60 * 60  // hours
		            +  mcom_time[4] * 60  // minutes
		            + (tp_now.tv_sec - mcom_time_set_time);  // add time elapsed since FP clock time retrieval at BOOT

		cur_mjd        += time_in_sec / (24 * 60 * 60);
		cur_time_in_sec = time_in_sec % (24 * 60 * 60);

		ioctl_data[0] = cur_mjd >> 8;
		ioctl_data[1] = cur_mjd & 0xff;
		ioctl_data[2] = cur_time_in_sec / (60 * 60);
		ioctl_data[3] = (cur_time_in_sec % (60 * 60)) / 60;
		ioctl_data[4] = (cur_time_in_sec % (60 * 60)) % 60;
	}
	dprintk(20, "FP time: MJD = %d, %02d:%02d:%02d\n", ((ioctl_data[0] << 8) + ioctl_data[1]), ioctl_data[2], ioctl_data[3], ioctl_data[4]); 
//	dprintk(150, "%s <\n", __func__);
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
 *       processor, and is not used in case the shut
 *       down function detects a wake up time in the
 *       past (fp_control does this) or end-of-Linux-
 *       time (time_t LONG_MAX).
 *
 */
int mcom_SetWakeUpTime(char *wtime)
{
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
 * - If a timer start has occurred: the wake up time
 *   used;
 * - In all other cases: 01-01-2000 (earliest possible
 *   in front processor terms as it assumes the century
 *   to be always 20). This value was set when the driver
 *   was initialized.
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
//	dprintk(150, "%s <\n", __func__);
	return 0;
}

/*******************************************************
 *
 * mcom_GetWakeUpMode: get wake up reason.
 *
 */
int mcom_GetWakeUpMode(void)
{
//	dprintk(150, "%s >\n", __func__);

	if (mcom_version[0] == 4)  // 4.x.x  // LED models
	{
		ioctl_data[0] = wakeupreason;
	}
	else if (mcom_version[0] == 5)  // 5.x.x  // VFD models
	{
		ioctl_data[0] = mcom_private[7];
	}
	else
	{
		ioctl_data[0] = 0;
	}
	dprintk(20, "Wakeup reason is %d (%s)\n", ioctl_data[0], wakeupName[ioctl_data[0]]);
//	dprintk(150, "%s <\n", __func__);
	return 0;
}

/*******************************************************
 *
 * mcom_GetVersion: get FP software version.
 *
 */
int mcom_GetVersion(void)
{
//	dprintk(150, "%s >\n", __func__);

	ioctl_data[0] = ((mcom_version[0] & 0x0f) * 100) + ((mcom_version[1] >> 4) * 10) + (mcom_version[1] & 0x0f);

	dprintk(20, "FP software version is %d.%02d\n", ioctl_data[0] / 100, ioctl_data[0] % 100);
//	dprintk(150, "%s <\n", __func__);
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
	char buf[8 + _VAL];

	if (on)
	{
		res = mcom_WriteString(lastdata.data, lastdata.length);
	}
	else
	{
		buf[0] = FP_CMD_DISPLAY;
		buf[1] =  (displayType == _7SEG ? 4 : 8);
		memset(buf + _VAL, 0, buf[1]);  // fill command buffer with 'spaces'
		res = mcom_WriteCommand(buf, _VAL + buf[_LEN], 1);
	}
	return res;
}

#if defined(VFD_TEST)
/*****************************************************************
 *
 * mcom_VfdTest: Send arbitrary bytes to front processor
 *               and read back status, dataflag and if
 *               applicable return bytes (for development
 *               purposes).
 * NOT TESTED YET!
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
	}
	dprintk(150, "%s <\n", __func__);
	return res;
}
#endif

/******************************************************
 *
 * Convert UTF-8 formatted text to display text.
 *
 * Returns corrected length.
 *
 */
int mcom_utf8conv(unsigned char *text, unsigned char len, int displaytype)
{
	int i;
	int dlen;  // display width
	int wlen;
	unsigned char kbuf[32];
	unsigned char *UTF_Char_Table = NULL;

	text[len] = 0x00;  // terminate text

	dprintk(150, "%s > Text: [%s], len = %d\n", __func__, text, len);

	wlen = 0;  // output index
	i = 0;  // input index
	memset(kbuf, 0x20, sizeof(kbuf));  // fill output buffer with spaces

	dlen = (displayType == _7SEG ? 4 : 8);

	while ((i < len && wlen <= dlen))
	{
		if (text[i] == 0x00)
		{
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
						switch (displayType)
						{
							case _7SEG:
							default:
							{
								UTF_Char_Table = VFD_OLD_UTF_C2;  // LED_UTF_C2;
								break;
							}
							case _VFD:
							{
								UTF_Char_Table = VFD_UTF_C2;
								break;
							}
#if 0
							case _VFD_OLD:
							{
								UTF_Char_Table = VFD_OLD_UTF_C2;
								break;
							}
#endif
						}
					}
					break;
				}
				case 0xc3:
				{
					switch (displayType)
					{
						case _7SEG:
						default:
						{
							UTF_Char_Table = VFD_OLD_UTF_C3;  // LED_UTF_C3;
							break;
						}
						case _VFD:
						{
							UTF_Char_Table = VFD_UTF_C3;
							break;
						}
#if 0
						case _VFD_OLD:
						{
							UTF_Char_Table = VFD_OLD_UTF_C3;
							break;
						}
#endif
					}
					break;
				}
				case 0xc4:
				{
					switch (displayType)
					{
						case _7SEG:
						default:
						{
							UTF_Char_Table = VFD_OLD_UTF_C4;  // LED_UTF_C4;
							break;
						}
						case _VFD:
						{
							UTF_Char_Table = VFD_UTF_C4;
							break;
						}
#if 0
						case _VFD_OLD:
						{
							UTF_Char_Table = VFD_OLD_UTF_C4;
							break;
						}
#endif
					}
					break;
				}
				case 0xc5:
				{
					switch (displayType)
					{
						case _7SEG:
						default:
						{
							UTF_Char_Table = VFD_OLD_UTF_C5;  // LED_UTF_C5;
							break;
						}
						case _VFD:
						{
							UTF_Char_Table = VFD_UTF_C5;
							break;
						}
#if 0
						case _VFD_OLD:
						{
							UTF_Char_Table = VFD_OLD_UTF_C5;
							break;
						}
#endif
					}
					break;
				}
#if 0  // Cyrillic currently not supported
				case 0xd0:
				{
					switch (displayType)
					{
						case _7SEG:
						default:
						{
							UTF_Char_Table = VFD_OLD_UTF_D0;  // LED_UTF_D0;
							break;
						}
						case _VFD:
						{
							UTF_Char_Table = VFD_UTF_D0;
							break;
						}
#if 0
						case _VFD_OLD:
						{
							UTF_Char_Table = VFD_OLD_UTF_D0;
							break;
						}
#endif
					}
					break;
				}
				case 0xd1:
				{
					switch (displayType)
					{
						case _7SEG:
						default:
						{
							UTF_Char_Table = VFD_OLD_UTF_D1;  // LED_UTF_D1;
							break;
						}
						case _VFD:
						{
							UTF_Char_Table = VFD_UTF_D1;
							break;
						}
#if 0
						case _VFD_OLD:
						{
							UTF_Char_Table = VFD_OLD_UTF_D1;
							break;
						}
#endif
					}
					break;
				}
#endif  // Cyrillic
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
	memcpy(text, kbuf, wlen);
	text[wlen] = 0;
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
	unsigned char cmdBuf[16 + _VAL];
	unsigned char bBuf[24];
	int i = 0;
	int j = 0;
	int dlen;  // display width
	int payload_len;
	int res = 0;

//	dprintk(150, "%s >\n", __func__);

	if (len == 0)
	{
		return 0;
	}
	if (len > sizeof(bBuf))
	{
		len = sizeof(bBuf) - 1;
	}
	// leave input buffer untouched and work with a copy
	memset(bBuf, 0, sizeof(bBuf));
	strncpy(bBuf, aBuf, len);
	bBuf[len] = 0;
	dprintk(20, "%s Text: [%s]\n", __func__, bBuf);

	len = mcom_utf8conv(bBuf, len, displayType);  // process UTF-8

	dlen = (displayType == _7SEG ? 4 : 8);
	if (len > dlen)
	{
		len = dlen;  // do not display more than DISPLAY_WIDTH characters
	}
	memset(cmdBuf, 0, sizeof(cmdBuf));  // fill command buffer with 'spaces'

	payload_len = 0;
	for (i = 0;  i < len; i++)
	{
		// build command argument
		switch (displayType)
		{
			case _VFD:  // VFD display
			{
				memcpy(cmdBuf + _VAL + payload_len, mcom_ascii_char_14seg[bBuf[i]], 2);
				payload_len += 2;
				break;
			}
			case _7SEG:  // 4 digit 7-Seg display
			default:
			{
				memcpy(cmdBuf + _VAL + payload_len, mcom_ascii_char_7seg[bBuf[i]], 1);
				payload_len++;
				break;
			}
#if 0
			case _VFD_OLD:  // (HD 9600 models)
			{
				cmdBuf[i + _VAL] = mcom_ascii_char_14seg_old[bBuf[i]];
				payload_len++;
				break
			}
#endif
		}
	}
	/* complete command write */
	cmdBuf[0] = FP_CMD_DISPLAY;
	cmdBuf[1] = payload_len;

	/* save last string written to FP */
	memcpy(&lastdata.data, bBuf, len);  // save display text for /dev/vfd read
	lastdata.length = len;  // save length for /dev/vfd read

//	dprintk(50, "%s: len %d\n", __func__, len);
	res = mcom_WriteCommand(cmdBuf, (displayType == _VFD ? dlen << 1 : dlen) + _VAL, 1);
//	dprintk(150, "%s <\n", __func__);
	return res;
}

/****************************************************************
 *
 * mcom_init_func: Initialize the driver.
 *
 */
int mcom_init_func(void)
{
	unsigned char  comm_buf[20];
	unsigned char  i;
	int            res;
	int            bootSendCount;
	unsigned int   wakeupMin;
	unsigned int   wakeupMJD;
	unsigned char  wakeupDays;
	unsigned int   wakeupHours;
	unsigned int   wakeupMins;
	unsigned int   wakeupSecs;
	unsigned short wakeupYear;
	unsigned char  wakeupMonth;
	unsigned char  wakeupDay;

//	dprintk(150, "%s >\n", __func__);
	sema_init(&write_sem, 1);

	bootSendCount = 3;  // allow 3 FP boot attempts
	eb_flag = 0;  // flag wait for version
	preamblecount = 1;  // preset preamble counter

	/*****************************************************************************
	 *
	 * Front panel init seems to be:
	 *
	 * Send BOOT command
	 *  FP response:
     *   a. Version followed by FP time (always seen in practice on VFD and LED);
	 *   b: FP time followed by Version (always seen in practice on VFD_OLD);
	 *   c: private data (never seen in practice on VFD_OLD).
	 * Responses a) and b) must be acknowledged with a C5 command.
	 *
	 * This sequence and accompying checks are performed in the processResponse
	 * routine in cn_micom_main.c.
	 * The code here simply waits for the responses to have occurred
	 * and then processes the data retrieved.
	 *
	 */
	while (bootSendCount)
	{
		// send BOOT command (0xeb)
		comm_buf[_TAG] = FP_CMD_BOOT;
		comm_buf[_LEN] = 1;
		comm_buf[_VAL] = 0x01;  // dummy

		errorOccured = 0;
//		mcom_Dump("## SEND:BOOT ##", comm_buf);
		res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 1);

		if (errorOccured == 1)
		{
			/* error */
			memset(ioctl_data, 0, 8);
			dprintk(1, "%s: Error sending BOOT command\n", __func__);
			msleep(100);
			goto MCOM_RETRY;
		}

		res = 1000;  // time out is one second

		while (res != 0 && eb_flag != 3)  // wait until FP has sent version, time and private data
		{
			msleep(1);
			res --;
		}
		if (res == 0)
		{
			dprintk(1, "%s: Error; timeout on BOOT_CMD\n", __func__);
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

#if 0  // On Atemio TitanNit this is sent
	if (1)  // (MCOM_GetMcuType() > FP_CMD_TYPE_1)
	{
		for (i = 0; i < 3; k++)
		{
			// send "KEYCODE (0xE9)"
			comm_buf[_TAG] = FP_CMD_KEYCODE;
			comm_buf[_LEN] = 4;
			comm_buf[_VAL + 0] = 0x04;
			comm_buf[_VAL + 1] = 0xF3;
			comm_buf[_VAL + 2] = 0x5F;
			comm_buf[_VAL + 3] = 0xA0;

			checksum = mcom_Checksum(comm_buf, 6);

			errorOccured = 0;
//			mcom_Dump("## SEND:KEYCODE ##", comm_buf, 2/*tag+len*/ + n);
			mcom_Dump("## SEND:KEYCODE ##", comm_buf);
//			res = mcom_WriteCommand(comm_buf, 6, 1);  // yields timeout on ack
			res = mcom_WriteCommand(comm_buf, comm_buf[_LEN] + _VAL, 0);
#if 0
			if (errorOccured == 1)
			{
				/* error */
				memset(ioctl_data, 0, 8);
				dprintk(1, "Error sending KEYCODE command\n");

				msleep(100);
				goto MCOM_RECOVER;
			}
			if (res == 0)
			{
				if (checksum == ioctl_data[_VAL])
				{
					dprintk(1, "KEYCODE response: checksum error\n");
					break;
				}
			}
#endif
		}
		if (i >= 3)
		{
			msleep(100);
			goto MCOM_RECOVER;
		}
	}
#endif
	// display boot result data
	dprintk(20, "Front panel version: %X.%02X.%02X\n", mcom_version[0], mcom_version[1], mcom_version[2]);
	dprintk(20, "Front panel time: %02d:%02d:%02d %02d-%02d-20%02d\n", mcom_time[3], mcom_time[4], mcom_time[5], mcom_time[2], mcom_time[1], mcom_time[0]);
//	dprintk(20, "Front panel extra data: %02x %02x %02x\n", mcom_time[6], mcom_time[7], mcom_time[8]);
//	dprintk(20, "Front panel private data: %02x %02x %02x %02x %02x %02x %02x %02x\n", mcom_private[0], mcom_private[1], mcom_private[2], mcom_private[3], mcom_private[4], mcom_private[5], mcom_private[6], mcom_private[7]);

	// Determine wake up reason as follows:
	// if FP time/date is 00:00:00 01-01-2000 -> power up
	// else if FP reports timer start up -> timer
	// all other cases -> from standby
	if (mcom_time[3] == 0
	&&  mcom_time[4] == 0
	&&  mcom_time[5] == 0
	&&  mcom_time[2] == 1
	&&  mcom_time[1] == 1
	&&  mcom_time[0] == 0)
	{
		wakeupreason = PWR_ON;
	}
	else if (mcom_time[8])
	{
		wakeupreason = TIMER;
	}
	else
	{
		wakeupreason = STANDBY;
	}
	dprintk(20, "Wake up reason: %d (%s)\n", wakeupreason, wakeupName[wakeupreason]);

	// calculate and set initial wake up time
	wakeupMin = (mcom_time[6] << 8) + mcom_time[7];
	wakeupDays  = (wakeupMin / 3600) % 24;
	wakeupHours = (wakeupMin / 60) % 60;
	wakeupMins  =  wakeupMin % 60;
	dprintk(20, "Wake up time: %d days, %d hours and %d minutes after FP clock time\n", wakeupDays, wakeupHours, wakeupMins);

	wakeupMJD = mcomYMD2MJD(mcom_time[0] + 2000, mcom_time[1], mcom_time[2]) + wakeupDays;
	mcom_wakeup_time[0] = wakeupMJD >> 8;
	mcom_wakeup_time[1] = wakeupMJD & 0xff;
	mcom_wakeup_time[2] = mcom_time[3] + wakeupHours;  // wake up hour
	while (mcom_wakeup_time[2] > 23)
	{
		for (i = 0; i < wakeupDays; i++)
		{
			mcom_wakeup_time[2] -= 24;
			wakeupMJD++;
		}
	}
	mcom_wakeup_time[3] = mcom_time[4] + wakeupMins;  // wake up minute
	while (mcom_wakeup_time[3] > 59)
	{
		mcom_wakeup_time[3] -= 60;
		mcom_wakeup_time[2]++;
	}
	mcom_wakeup_time[4] = mcom_time[5];  // (usually zero, ignored)
	mcom_MJD2YMD(wakeupMJD, &wakeupYear, &wakeupMonth, &wakeupDay);
	dprintk(20, "Wake up time: %02d:%02d:00 %02d-%02d-%04d\n", mcom_wakeup_time[2], mcom_wakeup_time[3], wakeupDay, wakeupMonth, wakeupYear);

	displayType = mcom_GetDisplayType();  // determine display type

	// Handle initial GMT offset (may be changed by writing to /proc/stb/fp/rtc_offset)
	res = strict_strtol(gmt_offset, 10, (long *)&rtc_offset);
	if (res && gmt_offset[0] == '+')
	{
		res = strict_strtol(gmt_offset + 1, 10, (long *)&rtc_offset);
	}
	dprintk(20, "Frontpanel initialization complete\n");
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
	unsigned char bBuf[8];
	int res = 0;

//	dprintk(150, "%s >\n", __func__);

	memset(bBuf, ' ', sizeof(bBuf));
	res = mcom_WriteString(bBuf, (displayType == _7SEG ? 4 : 8));
//	dprintk(150, "%s <\n", __func__);
}

static ssize_t MCOMdev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	char *kernel_buf;
	int minor;
	int vLoop;
	int res = 0;
	int width;
	int llen;
	int offset = 0;
	char buf[64];
	char *b;

//	dprintk(150, "%s >\n", __func__);
	dprintk(50, "%s len %d, offs %d\n", __func__, len, (int)*off);

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
	dprintk(100, "minor = %d\n", minor);

	/* do not write to the remote control */
	if (minor == FRONTPANEL_MINOR_RC)
	{
		return -EOPNOTSUPP;
	}
	kernel_buf = kmalloc(len, GFP_KERNEL);

	if (kernel_buf == NULL)
	{
		dprintk(1, "%s < Error: return -ENOMEM\n", __func__);
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
	dprintk(20, "Text: [%s]\n", kernel_buf);
	width = (displayType == _7SEG ? 4 : 8);
	if (llen <= width)  // no scroll
	{
//		dprintk(20, "No scroll\n");
		res |= mcom_WriteString(kernel_buf, llen);
//		dprintk(20, "Displayed\n");
	}
	else  // scroll, display string is longer than display length
	{
		// initial display starting at 2nd position to ease reading
//		dprintk(20, "Scroll\n");
		memset(buf, ' ', sizeof(buf));
		offset = 2;
		memcpy(buf + offset, kernel_buf, llen);
		llen += offset;
		buf[llen + width] = '\0';  // terminate string

		// scroll text
		b = buf;
		for (vLoop = 0; vLoop < llen; vLoop++)
		{
			res |= mcom_WriteString(b + vLoop, width);
			// sleep 300 ms
			msleep(300);
		}
		clear_display();

		// final display
//		dprintk(20, "Final display\n");
		res |= mcom_WriteString(kernel_buf, width);
//		dprintk(20, "Displayed\n");
	}
//	dprintk(20, "%s: 1 res= %d\n", __func__, res);
	kfree(kernel_buf);
//	dprintk(20, "%s: 2\n", __func__);
	write_sem_up();
//	dprintk(20, "%s: 3\n", __func__);
//	dprintk(50, "%s < res = %d len = %d\n", __func__, res, len);

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

//	dprintk(150, "%s >\n", __func__);
//	dprintk(100, "%s len=%d, offs=%d\n", len, (int) *off);

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
		dprintk(1, "%s < Error: Bad Minor, return -EUSERS\n", __func__);
		return -EUSERS;
	}
	dprintk(100, "minor = %d\n", minor);

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
//		dprintk(150, "%s <\n", __func__);
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

//	printk(150, "%s >\n", __func__);

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
	struct cn_micom_ioctl_data *mcom = (struct cn_micom_ioctl_data *)arg;
	struct vfd_ioctl_data *data = (struct vfd_ioctl_data *)arg;
	int res = 0;

//	dprintk(150, "%s > 0x%.8x\n", __func__, cmd);

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
			dprintk(10, "Set wake up time: (MJD = %d) - %02d:%02d:%02d (local)\n", (mcom->u.standby.time[0] & 0xff) * 256 + (mcom->u.standby.time[1] & 0xff),
				mcom->u.standby.time[2], mcom->u.standby.time[3], mcom->u.standby.time[4]);
			res = mcom_SetWakeUpTime(mcom->u.standby.time);
			break;
		}
		case VFDGETWAKEUPTIME:
		{
			dprintk(10, "Wake up time: (MJD = %d) - %02d:%02d:%02d (local)\n", (((mcom_wakeup_time[0] & 0xff) << 8) + (mcom_wakeup_time[1] & 0xff)),
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
#if defined(VFD_TEST)
		case VFDTEST:
		{
			res = mcom_VfdTest(mcom->u.test.data);
			res |= copy_to_user((void *)arg, &(mcom->u.test.data), ((mcom->u.test.data[1] != 0) ? 8 : 2));
			break;
		}
#endif
		case VFDGETVERSION:
		{
			unsigned int data[1];

			dprintk(20, "FP version is %d.%02d\n", mcom_version[0], mcom_version[1]);
			data[0] = ((mcom_version[0] & 0x0f) * 100) + ((mcom_version[1] >> 4) * 10) + (mcom_version[1] & 0x0f);
			copy_to_user((void *)arg, &data, sizeof(data));
			break;
		}
		case 0x5305:  // Neutrino sends this, ignore
		{
			break;
		}
		default:
		{
			dprintk(1, "Unknown IOCTL 0x%x\n", cmd);
			break;
		}
	}
	up(&write_sem);
	dprintk(150, "%s <\n", __func__);
	return res;
}

struct file_operations vfd_fops =
{
	.owner   = THIS_MODULE,
	.ioctl   = MCOMdev_ioctl,
	.write   = MCOMdev_write,
	.read    = MCOMdev_read,
	.open    = MCOMdev_open,
	.release = MCOMdev_close
};
// vim:ts=4
