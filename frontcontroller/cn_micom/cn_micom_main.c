/****************************************************************************
 *
 * cn_micon_main.c
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2021 Audioniek: debugged on Atemio520
 *
 * Front panel driver for following CreNova models
 * Atemio AM 520 HD (Miniline B)
 * Sogno HD 800-V3 (Miniline B)
 * Opticum HD 9600 Mini (Miniline A, not tested)
 * Opticum HD 9600 PRIMA (?, not tested)
 * Opticum HD TS 9600 PRIMA (?, not tested)
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
 * 20210915 Audioniek       Initial version.
 * 20211024 Audioniek       Add wait for Tx ready to send routine to prevent
 *                          missing commands and erratic displays.
 * 20211025 Audioniek       Add module parameters waitTime and gmt_offset.
 * 20211027 Audioniek       Handling of RSP_TIME and RSP_VERSION moved from
 *                          cn_micom_file.c.
 * 20211031 Audioniek       Add processing of FP_RSP_PRIVATE preamble.
 * 20211031 Audioniek       Fix bug in processing of FP_RSP_PRIVATE.
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

#include "cn_micom_fp.h"
#include "cn_micom_asc.h"

//----------------------------------------------

short paramDebug = 0;
int dataflag = 0;
int waitTime = 1000;
static unsigned char expectEventData = 0;
static unsigned char expectEventId   = 1;
char *gmt_offset = "3600";  // GMT offset is plus one hour as default
int rtc_offset;

#define ACK_WAIT_TIME msecs_to_jiffies(1000)
#define BUFFERSIZE   256  // must be 2 ^ n

static unsigned char RCVBuffer [BUFFERSIZE];
static int           RCVBufferStart = 0, RCVBufferEnd = 0;

static unsigned char KeyBuffer [BUFFERSIZE];
static int           KeyBufferStart = 0, KeyBufferEnd = 0;

static unsigned char OutBuffer [BUFFERSIZE];
static int           OutBufferStart = 0, OutBufferEnd = 0;

static wait_queue_head_t wq;
static wait_queue_head_t rx_wq;
static wait_queue_head_t ack_wq;
static int dataReady = 0;
static int preambleID;

//----------------------------------------------

/* wait for tx ready */
static inline void wait_4_txready(void)
{
	unsigned long status;

	do
	{
		status = p2_inl(ASCXBaseAddress + ASC_INT_STA);  // get baseaddres + 0x14
	} while (status & ASC_INT_STA_TF);  // and wait for bit 9 (0x200) clear
}

/* queue data ->transmission is done in the IRQ */
void mcom_putc(unsigned char data)
{
	unsigned int *ASC_X_INT_EN = (unsigned int *)(ASCXBaseAddress + ASC_INT_EN);

	wait_4_txready();
	OutBuffer[OutBufferStart] = data;
	OutBufferStart = (OutBufferStart + 1) % BUFFERSIZE;

	/* if irq is not enabled, enable it */
	if (!(*ASC_X_INT_EN & ASC_INT_STA_THE))
	{
		*ASC_X_INT_EN = *ASC_X_INT_EN | ASC_INT_STA_THE;
	}
}

//----------------------------------------------

void ack_sem_up(void)
{
	dataReady = 1;
	wake_up_interruptible(&ack_wq);
}

int ack_sem_down(void)
{
	int err = 0;

	dataReady = 0;

	err  = wait_event_interruptible_timeout(ack_wq, dataReady == 1, ACK_WAIT_TIME);
	if (err == -ERESTARTSYS)
	{
		dprintk(1, "wait_event_interruptible failed\n");
		return err;
	}
	else if (err == 0)
	{
		dprintk(1, "Timeout waiting on ack\n");
	}
	else
	{
		dprintk(150, "Command processed - remaining jiffies %d\n", err);
	}
	dataflag = dataReady;
	return 0;
}
EXPORT_SYMBOL(ack_sem_down);

/***************************************************
 *
 * getLen: either determines length of a message
 *         received or checks it.
 * arg: -1    -> determine and return length
 *      other -> length to check
 * returns length, or 0 in case of an error
 */

int getLen(int expectedLen)
{
	int len, rcvLen;

	/* get received length of RCVBuffer */
	if (RCVBufferEnd == RCVBufferStart)
	{
		return 0;
	}
	else if (RCVBufferEnd < RCVBufferStart)
	{
		rcvLen = RCVBufferStart - RCVBufferEnd;
	}
	else
	{
		rcvLen = BUFFERSIZE - RCVBufferEnd + RCVBufferStart;
	}
	/* get packet data length  */
	len = RCVBuffer[(RCVBufferEnd + 1) % BUFFERSIZE];

	if ((rcvLen < expectedLen) && (rcvLen < len + 2))
	{
		/* preamble (1st byte + length) received */
		return 2;
	}
	if ((rcvLen < len + 2) && (expectedLen != -1))
	{
		/* incomplete packet received */
		dprintk(1, "%s Error: incorrect packet received (rcvLen = %d, expected %d)\n", __func__, rcvLen + 1, expectedLen);
		return 0; //rcvLen + 1;
	}

	/* check expected length */
	if ((expectedLen == -1) || (len + 2 == expectedLen))
	{
		return len + 2;
	}
	/* expected length is not matched */
	dprintk(1, "%s Error: expected length mismatch: expected: %d, received: %d\n", expectedLen, len + 2);
	return 0;
}

/*****************************************************************
 *
 * getRCData:
 *
 */
void getRCData(unsigned char *data, int *len)
{
	int i, j;

	*len = 0;

	while (KeyBufferStart == KeyBufferEnd)
	{
		dprintk(200, "%s %d - %d\n", __func__, KeyBufferStart, KeyBufferEnd);

		if (wait_event_interruptible(wq, KeyBufferStart != KeyBufferEnd))
		{
			dprintk(1, "wait_event_interruptible failed\n");
			return;
		}
	}

	i = KeyBufferEnd;

	if (KeyBuffer[i] == FP_RSP_KEYIN)
	{
		*len = cPackageSizeKeyIn;
	}
	if (*len <= 0)
	{
		*len = 0;
		return;
	}

	i = 0;
	j = KeyBufferEnd;

	while (i != *len)
	{
		data[i] = KeyBuffer[j];
		j++;
		i++;

		if (j >= BUFFERSIZE)
		{
			j = 0;
		}
		if (j == KeyBufferStart)
		{
			break;
		}
	}
	KeyBufferEnd = (KeyBufferEnd + *len) % BUFFERSIZE;
//	dprintk(150, "%s < len %d, End %d\n", __func__, *len, KeyBufferEnd);
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

//	dprintk(150, "%s >\n", __func__);

	response[_TAG] = FP_CMD_RESPONSE;  // 0xc5
	response[_LEN] = 1;  // data length
	response[_VAL] = mcom_Checksum(buf, _VAL + buf[_LEN]);  // checksum of last command's IOCTL data answer

	mcom_WriteCommand((char *)response, 3, 0);
	return 0;
}

void handleCopyData(int len, int responseflag)
{
	int i, j;
	unsigned char *data = kmalloc(len, GFP_KERNEL);

	i = 0;
	j = RCVBufferEnd;

	while (i != len)
	{
		data[i] = RCVBuffer[j];
		j++;
		i++;

		if (j >= BUFFERSIZE)
		{
			j = 0;
		}
		if (j == RCVBufferStart)
		{
			break;
		}
	}
	copyData(data, len);
	if (responseflag)
	{
//		dprintk(50, "%s Send response\n", __func__);
		j = mcom_SendResponse(data);
	}
	kfree(data);
}

void dumpData(void)
{
	int i, j, len;

	if (paramDebug < 150)
	{
		return;
	}

	len = getLen(-1);

	if (len == 0)
	{
		return;
	}
	dprintk(0, "Dumping %d received bytes:\n", len);
	i = RCVBufferEnd;
	for (j = 0; j < len; j++)
	{
		printk("0x%02x ", RCVBuffer[i]);

		i++;

		if (i >= BUFFERSIZE)
		{
			i = 0;
		}

		if (i == RCVBufferStart)
		{
			i = -1;
			break;
		}
	}
	printk("\n");
}

void dumpValues(void)
{
	dprintk(150, "BufferStart %d, BufferEnd %d, len %d\n", RCVBufferStart, RCVBufferEnd, getLen(-1));

	if (RCVBufferStart != RCVBufferEnd)
	{
		if (paramDebug >= 50)
		{
			dumpData();
		}
	}
}

/**********************************************************
 *
 * processResponse: process responses received from the
 * front processor.
 *
 * This routine is called by the interrupt handler of
 * the front processor communication. An interrupt will
 * occur each time the front processor sends data.
 *
 * In the CreNova implemention the front processor is
 * initialised by sending it a boot command (eb 01 01).
 * The front processor will respond by sending up to six
 * responses:
 * 1. FP_RSP_VERSION with a length of 2. This signals the
 *    next response will be FP_RSP_VERSION as well, but
 *    that response will have version data and is to be
 *    processed by this routine.
 * 2. FP_RSP_VERSION with a length of 6. The response
 *    has the version data and must be acknowledged with
 *    a FP_CMD_RESPONSE command. This action will trigger:
 * 3. FP_RSP_TIME with a length of 2. This signals the
 *    next response will be FP_RSP_TIME as well, but
 *    that response will have time data and is to be
 *    processed by this routine.
 * 4. FP_RSP_TIME with a length of 11. The response has
 *    the time/date data and must be acknowledged with
 *    a FP_CMD_RESPONSE command. This action will trigger:
 * 5. FP_RSP_PRIVATE with a length of 2. This signals the
 *    next response will be FP_RSP_PRIVATE as well, but
 *    that response will have private data and is to be
 *    processed by this routine.
 * 6. FP_RSP_PRIVATE with a length of 10. The response has
 *    the private data and must not be acknowledged with
 *    FP_CMD_RESPONSE command. Processing this response
 *    finishes processing the boot command.
 * The initial responses with a length of 2 are called
 * preambles in the code below. They are counted and
 * their correct identity is checked for upon reception
 * of the next response in the bootsequence.
 *
 */
static void processResponse(void)
{
	int len, i;
	int preambleLength = -1;
	struct timespec tp;
	struct tm *sTime;

	dprintk(150, "%s >\n", __func__);
//	dumpData();
//	if (preamblecount == 0)
//	{
		len = getLen(-1);
//	}
//	else
//	{
//		len = getLen(preambleLength + 2);
//	}

	if (len == 2 && preamblecount)  // if preamble
	{
		preamblecount++;  // count preambles
		preambleID = RCVBuffer[RCVBufferEnd];
		if (preamblecount > 4)  // if already 3 preambles processed
		{
			dprintk(1, "%s Error: too many preambles received (ID = 0x%02x)\n", __func__, preambleID);
			preamblecount = 0;
			return;
		}

		if (RCVBufferEnd != BUFFERSIZE)
		{
			preambleLength = RCVBuffer[RCVBufferEnd + 1];
		}
		else
		{
			RCVBuffer[0];
		}
		dprintk(70, "Preamble (0x%02x) received, count = %d\n", preambleID, preamblecount - 1);
		return;
	}
	if ((len < cMinimumSize) && RCVBuffer[RCVBufferEnd] != FP_RSP_RESPONSE)
	{
		dprintk(1, "%s: Error: packet too small (rcvLen = %d (< %d))\n", __func__, len, cMinimumSize);
		return;
	}
//	dumpData();

	if (expectEventId)
	{
		expectEventData = RCVBuffer[RCVBufferEnd];
		expectEventId = 0;
	}

	dprintk(100, "event 0x%02x %d %d\n", expectEventData, RCVBufferStart, RCVBufferEnd);

	if (expectEventData)
	{
		switch (expectEventData)
		{
			case FP_RSP_KEYIN:  // handles both front panel keys as well as remote control
			{
				len = getLen(cPackageSizeKeyIn);

				if (len == 0)
				{
					goto out_switch;
				}

				if (len < cPackageSizeKeyIn)
				{
					goto out_switch;
				}
				dprintk(50, "FP_RSP_KEYIN processed\n");

				if (paramDebug >= 150)
				{
					dumpData();
				}
				/* copy data */
				for (i = 0; i < cPackageSizeKeyIn; i++)
				{
					int from, to;

					from = (RCVBufferEnd + i) % BUFFERSIZE;
					to = KeyBufferStart % BUFFERSIZE;

					KeyBuffer[to] = RCVBuffer[from];

					KeyBufferStart = (KeyBufferStart + 1) % BUFFERSIZE;
				}
				//printk("FP_RSP_KEYIN complete - %02x\n", RCVBuffer[(RCVBufferEnd + 2) % BUFFERSIZE]);

				wake_up_interruptible(&wq);

				RCVBufferEnd = (RCVBufferEnd + cPackageSizeKeyIn) % BUFFERSIZE;
				break;
			}
			case FP_RSP_RESPONSE:
			{
				dprintk(70, "FP_RSP_RESPONSE (0x%02x) received\n", FP_RSP_RESPONSE);
				len = getLen(cGetResponseSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetResponseSize)
				{
					goto out_switch;
				}
				handleCopyData(len, 0);
				// verify checksum
				if (rsp_cksum != ioctl_data[_VAL])
				{
					dprintk(1, "%s: Checksum error in FP_RSP_RESPONSE: 0x%02x (should be 0x%02x)\n", __func__, ioctl_data[_VAL], rsp_cksum);
//					goto out_switch;
				}
				dprintk(100, "RESPONSE: ack received\n");
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetResponseSize) % BUFFERSIZE;
				break;
			}
			case FP_RSP_VERSION:
			{
				len = getLen(cGetVersionSize);
				if (preamblecount && (preambleID != FP_RSP_VERSION))
				{
					dprintk(1, "%s Error: wrong Boot response 0x%02x received\n", __func__, preambleID);
					break;
				}

				if (len == 0)
				{
					goto out_switch;
				}

				if (len < cGetVersionSize)
				{
					goto out_switch;
				}
				dprintk(70, "FP_RSP_VERSION (0x%02x) received\n", FP_RSP_VERSION);
				if (eb_flag == 0)  // if version answer on boot
				{
					handleCopyData(len, 1);  // send ACK
					memcpy(mcom_version, ioctl_data + _VAL, ioctl_data[_LEN]);
					eb_flag++; // flag 1st answer received
				}
				else
				{
					handleCopyData(len, 0);
					dprintk(100, "FP_RSP_VERSION (0x%02x) processed\n", FP_RSP_VERSION);
				}
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetVersionSize) % BUFFERSIZE;
				break;
			}
			case FP_RSP_TIME:
			{
				if (preamblecount
				&&  preambleID != FP_RSP_TIME)
				{
					dprintk(1, "%s Error: wrong Boot response 0x%02x received\n", __func__, preambleID);
					break;
				}
				len = getLen(cGetTimeSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetTimeSize)
				{
					goto out_switch;
				}
				dprintk(70, "FP_RSP_TIME (0x%02x) received\n", FP_RSP_TIME);
				if (eb_flag == 1)
				{
					handleCopyData(len, 1);  // get data and send ACK
					memcpy(mcom_time, ioctl_data + _VAL, ioctl_data[_LEN]);
					// get system time to be able to simulate the FP clock advancing correctly
					// TODO: this will fail when system time is changed!
					tp = current_kernel_time();
					mcom_time_set_time = tp.tv_sec;
					sTime = gmtime(mcom_time_set_time);
					dprintk(70, "mcom_time_set_time = %02d:%02d:%02d %02d-%02d-%04d\n", sTime->tm_hour, sTime->tm_min, sTime->tm_sec,
						sTime->tm_mday, sTime->tm_mon + 1, sTime->tm_year + 1900);
					eb_flag++;  // flag 2nd response received
				}
				else
				{
					handleCopyData(len, 0);
					dprintk(100, "FP_RSP_TIME (0x%02x) processed\n", FP_RSP_TIME);
				}

				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetTimeSize) % BUFFERSIZE;
				break;
			}
			case FP_RSP_PRIVATE:
			{
				len = getLen(cGetPrivateSize);
				if (preamblecount == 3
				&&  preambleID != FP_RSP_PRIVATE
				&&  eb_flag == 2)
				{
					dprintk(1, "%s Error: wrong Boot response 0x%02x received\n", __func__, preambleID);
					break;
				}
				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetPrivateSize)
				{
					goto out_switch;
				}
				dprintk(70, "FP_RSP_PRIVATE (0x%02x) received\n", FP_RSP_PRIVATE);
				if (preamblecount)
				{
					handleCopyData(len, 0);
					preamblecount = 0;
					memcpy(mcom_private, ioctl_data + _VAL, ioctl_data[_LEN]);
					eb_flag++;  // flag 3rd response received
				}
				else
				{
					handleCopyData(len, 1);
				}
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetPrivateSize) % BUFFERSIZE;
				preamblecount = 0;  // flag BOOT command responses finished
				break;
			}
			default:  // Ignore unknown response
			{
				dprintk(1, "Unknown FP response %02x received, discarding\n", expectEventData);
				dprintk(1, "start %d end %d\n",  RCVBufferStart,  RCVBufferEnd);
				dumpData();

				/* discard all data, because this happens currently
				 * sometimes. Do not know the problem here.
				 */
				RCVBufferEnd = RCVBufferStart;
				break;
			}
		}
	}

out_switch:
	expectEventId = 1;
	expectEventData = 0;
}

static irqreturn_t FP_interrupt(int irq, void *dev_id)
{
	unsigned int *ASC_X_INT_STA = (unsigned int *)(ASCXBaseAddress + ASC_INT_STA);
	unsigned int *ASC_X_INT_EN  = (unsigned int *)(ASCXBaseAddress + ASC_INT_EN);
	char         *ASC_X_RX_BUFF = (char *)(ASCXBaseAddress + ASC_RX_BUFF);
	char         *ASC_X_TX_BUFF = (char *)(ASCXBaseAddress + ASC_TX_BUFF);
	int          dataArrived = 0;

	if (paramDebug > 200)
	{
		printk("i - ");
	}
	while (*ASC_X_INT_STA & ASC_INT_STA_RBF)
	{
		RCVBuffer [RCVBufferStart] = *ASC_X_RX_BUFF;
//        printk("%02x ", RCVBuffer [RCVBufferStart]);
		RCVBufferStart = (RCVBufferStart + 1) % BUFFERSIZE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
		// We are too fast, let us make a break
		udelay(1);
#endif
		dataArrived = 1;

		if (RCVBufferStart == RCVBufferEnd)
		{
			dprintk(1, "%s: ASC3 RCV buffer overflow!!! (%d - %d)\n", __func__, RCVBufferStart, RCVBufferEnd);
		}
	}
	if (dataArrived)
	{
		wake_up_interruptible(&rx_wq);
	}
//    printk("\n");

	while ((*ASC_X_INT_STA & ASC_INT_STA_THE)
	&&     (*ASC_X_INT_EN & ASC_INT_STA_THE)
	&&     (OutBufferStart != OutBufferEnd))
	{
		*ASC_X_TX_BUFF = OutBuffer[OutBufferEnd];
		OutBufferEnd = (OutBufferEnd + 1) % BUFFERSIZE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
		// We are too fast, let us make a break
		udelay(1);
#endif
	}

	/* if all the data is transmitted disable irq, otherwise
	 * system is overflowed with irq's
	 */
	if (OutBufferStart == OutBufferEnd)
	{
		if (*ASC_X_INT_EN & ASC_INT_STA_THE)
		{
			*ASC_X_INT_EN &= ~ASC_INT_STA_THE;
		}
	}
	return IRQ_HANDLED;
}

/***************************************************
 *
 * Task to handle responses from front processor.
 *
 */
int mcomTask(void *dummy)
{
	daemonize("mcomTask");

	allow_signal(SIGTERM);

	while (1)
	{
		int dataAvailable = 0;

		//dprintk(0, "wait_event!\n");

		if (wait_event_interruptible(rx_wq, (RCVBufferStart != RCVBufferEnd)))
		{
			dprintk(1, "wait_event_interruptible failed\n");
			continue;
		}

		//dprintk(0, "start %d end %d\n",  RCVBufferStart,  RCVBufferEnd);
		if (RCVBufferStart != RCVBufferEnd)
		{
			dataAvailable = 1;
		}
		while (dataAvailable)
		{
			// dprintk(0, "event!\n");
			processResponse();

			if (RCVBufferStart == RCVBufferEnd)
			{
				dataAvailable = 0;
			}
			dprintk(150, "start %d end %d\n",  RCVBufferStart,  RCVBufferEnd);
		}
	}
	dprintk(1, "%s < Error: mcomTask died!\n", __func__);
	return 0;
}

extern void create_proc_fp(void);
extern void remove_proc_fp(void);

/*******************************************
 *
 * Initialize module.
 *
 */
static int __init mcom_init_module(void)
{
	int i = 0;

	// set up ASC communication with the front controller

	// Address for Interrupt enable/disable
	unsigned int *ASC_X_INT_EN = (unsigned int *)(ASCXBaseAddress + ASC_INT_EN);
	// Address for FiFo enable/disable
	unsigned int *ASC_X_CTRL   = (unsigned int *)(ASCXBaseAddress + ASC_CTRL);

//	dprintk(150, "%s >\n", __func__);
	printk("CreNova frontcontroller module loading\n");

	// Disable all ASC interrupts
	*ASC_X_INT_EN = *ASC_X_INT_EN & ~0x000001ff;

	// Setup ASC
	serial_init();

	init_waitqueue_head(&wq);
	init_waitqueue_head(&rx_wq);
	init_waitqueue_head(&ack_wq);

	for (i = 0; i < LASTMINOR; i++)
	{
		sema_init(&FrontPanelOpen[i].sem, 1);
	}
	kernel_thread(mcomTask, NULL, 0);

	// Enable the ASC FIFO
	*ASC_X_CTRL = *ASC_X_CTRL | ASC_CTRL_FIFO_EN;

	dprintk(150, "%s Requesting IRQ %d for ASC\n", __func__, InterruptLine);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
	i = request_irq(InterruptLine, (void *)FP_interrupt, IRQF_DISABLED, "FP_serial", NULL);
#else
	i = request_irq(InterruptLine, (void *)FP_interrupt, SA_INTERRUPT, "FP_serial", NULL);
#endif
	dprintk(150, "%s IRQ %d requested\n", __func__, InterruptLine);

	if (!i)
	{
		*ASC_X_INT_EN = *ASC_X_INT_EN | 0x00000001;
	}
	else
	{
		dprintk(1, "%s: Error, cannot get IRQ\n", __func__);
	}
	msleep(waitTime);

	mcom_init_func();  // initialize driver

	if (register_chrdev(VFD_MAJOR, "VFD", &vfd_fops))
	{
		dprintk(1, "ERROR: Unable to get major %d for VFD/MCOM\n", VFD_MAJOR);
	}

	create_proc_fp();

	printk("CreNova frontcontroller module loaded\n");
//	dprintk(150, "%s <\n", __func__);
	return 0;
}

static void __exit mcom_cleanup_module(void)
{
	printk("CreNova frontcontroller module unloading\n");

	remove_proc_fp();
	unregister_chrdev(VFD_MAJOR, "VFD");
	free_irq(InterruptLine, NULL);
}

//----------------------------------------------

module_init(mcom_init_module);
module_exit(mcom_cleanup_module);

MODULE_DESCRIPTION("CreNova frontcontroller module");
MODULE_AUTHOR("Open Vision developers");
MODULE_LICENSE("GPL");

module_param(paramDebug, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(paramDebug, "Debug Output 0=disabled >0=enabled(debuglevel)");

module_param(waitTime, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(waitTime, "Wait before init in ms (default=1000)");

module_param(gmt_offset, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(gmt_offset, "GMT offset (default 3600");
// vim:ts=4
