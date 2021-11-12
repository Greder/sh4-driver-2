/****************************************************************************
 *
 * opt9600_fp_main.c
 *
 * Opticum HD 9600 (TS) Frontpanel driver.
 *
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2020 Audioniek: ported to Opticum HD 9600 (TS)
 *
 * Largely based on cn_micom, enhanced and ported to Opticum HD 9600 (TS)
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
 * 20201222 Audioniek       Initial version, based on cn_micom.
 * 20210929 Audioniek       Handling of TIME and VERSION moved from
 *                          opt9600_file.c.
 * 20210929 Audioniek       Add wait for Tx ready to send routine to prevent
 *                          missing commands and erratic displays
 * 20210930 Audioniek       Add module parameters waitTime and gmt_offset.
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

#include "opt9600_fp.h"
#include "opt9600_fp_asc.h"

//----------------------------------------------

short paramDebug = 0;
int waitTime = 1000;
int dataflag = 0;
static unsigned char expectEventData = 0;
static unsigned char expectEventId = 1;
char *gmt_offset = "3600";  // GMT offset is plus one hour as default
int rtc_offset;

#define BUFFERSIZE 256  // must be 2 ^ n
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

/* queue data ->transmission is done in the irq */
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
		dprintk(101, "Command processed - remaining jiffies %d\n", err);
	}
	dataflag = dataReady;
	return 0;
}
EXPORT_SYMBOL(ack_sem_down);

//------------------------------------------------------------------

int getLen(int expectedLen)
{
	int len, rcvLen;

	/* get received length of RCVBuffer  */
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
	if (rcvLen < 3)
	{
		return 0;
	}
	/* get packet data length  */
	len = RCVBuffer[(RCVBufferEnd + 1) % BUFFERSIZE];

	if (rcvLen < len + 2)
	{
		/* incomplete packet received */
//		dprintk(20, "Incomplete packet received (shorter than %d)\n", len);
		return 0;
	}

	/* check expected length */
	if ((expectedLen == -1) || (len + 2 == expectedLen))
	{
		return len + 2;
	}
	/* expected length is not matched */
	dprintk(20, "Expected length is not right (rcvd = %d, expected = %d)\n", len + 2, expectedLen);
	return 0;
}

void getRCData(unsigned char *data, int *len)
{
	int i, j;

	*len = 0;

	while (KeyBufferStart == KeyBufferEnd)
	{
		dprintk(200, "%s %d - %d\n", __func__, KeyBufferStart, KeyBufferEnd);

		if (wait_event_interruptible(wq, KeyBufferStart != KeyBufferEnd))
		{
			printk("wait_event_interruptible failed\n");
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
	dprintk(150, "%s < len %d, End %d\n", __func__, *len, KeyBufferEnd);
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
	copyData(data, len);  // put received data in ioctl_data[]
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
	i = RCVBufferEnd;
	dprintk(0, "%s: ", __func__);
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

/*******************************************
 *
 * Process responses from front processor.
 *
 */
static void processResponse(void)
{
	int len, i;
	struct timespec tp;

	//dumpData();
	len = getLen(-1);

	if (len < cMinimumSize)
	{
//		dprintk(1, "Length %d below minimum (%d)\n", len, cMinimumSize);
//		dprintk(200, "event 0x%02x bufferstart=%d bufferend=%d\n", expectEventData, RCVBufferStart, RCVBufferEnd);
		return;
	}
	//dumpData();

	if (expectEventId)
	{
		expectEventData = RCVBuffer[RCVBufferEnd];
		expectEventId = 0;
	}
	dprintk(150, "event 0x%02x bufferstart=%d bufferend=%d, len=%d\n", expectEventData, RCVBufferStart, RCVBufferEnd, len);

	if (expectEventData)
	{
		switch (expectEventData)
		{
			case FP_RSP_KEYIN:  // handles both front panel keys as well as remote control
			{
				dprintk(70, "FP_RSP_KEYIN (0x%02x) received\n", FP_RSP_KEYIN);
				len = getLen(cPackageSizeKeyIn);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cPackageSizeKeyIn)
				{
					goto out_switch;
				}

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
				wake_up_interruptible(&wq);
				RCVBufferEnd = (RCVBufferEnd + cPackageSizeKeyIn) % BUFFERSIZE;
				break;
			}
			case FP_RSP_RESPONSE:  // 0xc5
			{
				unsigned char cmd_buf[3];
				int i, j;

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
				if (paramDebug > 150)
				{
					dumpData();
				}
				handleCopyData(len, 0);
				// check checksum
				if (rsp_cksum != ioctl_data[_VAL])
				{
					dprintk(1, "%s: Checksum error in FP_RSP_RESPONSE: 0x%02x (should be 0x%02x)\n", __func__, ioctl_data[_VAL], rsp_cksum);
//					goto out_switch;
				}
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetResponseSize) % BUFFERSIZE;
				break;
			}
			case FP_RSP_VERSION:
			{
				dprintk(70, "FP_RSP_VERSION (0x%02x) received\n", FP_RSP_VERSION);
				len = getLen(cGetVersionSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetVersionSize)
				{
					goto out_switch;
				}
				if (paramDebug > 150)
				{
					dumpData();
				}
				if (eb_flag == 1)  // if 2nd answer on boot
				{
					handleCopyData(len, 1);  // send ACK
					memcpy(mcom_version, ioctl_data + _VAL, ioctl_data[_LEN]);
					dprintk(20, "Front panel version = %X.%02X.%02X\n", mcom_version[0], mcom_version[1], mcom_version[2]);
					eb_flag++; // flag 2nd answer received
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
				dprintk(70, "FP_RSP_TIME (0x%02x) received\n", FP_RSP_TIME);
				len = getLen(cGetTimeSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetTimeSize)
				{
					goto out_switch;
				}
				if (paramDebug >= 150)
				{
					dumpData();
				}
				if (eb_flag == 0)
				{
					handleCopyData(len, 1); // get data and send ACK
					memcpy(mcom_time, ioctl_data + _VAL, ioctl_data[_LEN]);
					dprintk(20, "Front panel time %02d:%02d:%02d %02d-%02d-20%02d\n", mcom_time[3], mcom_time[4], mcom_time[5], mcom_time[2], mcom_time[1], mcom_time[0]);
					eb_flag++;  // flag 1st response received
				}
				else
				{
					handleCopyData(len, 0);
					dprintk(100, "FP_RSP_TIME (0x%02x) processed\n", FP_RSP_TIME);
				}
				// get system time to be able to simulate the FP clock advancing correctly
				// TODO: this will fail when system time is changed!
				tp = current_kernel_time();
				mcom_time_set_time = tp.tv_sec;
				dprintk(70, "mcom_time_set_time = %u\n", mcom_time_set_time);

				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetTimeSize) % BUFFERSIZE;
				break;
			}
			case FP_RSP_WAKEUP_REASON:
			{
				dprintk(70, "FP_RSP_WAKEUP_REASON (0x%02x) received\n", FP_RSP_WAKEUP_REASON);
				len = getLen(cGetWakeupReasonSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetWakeupReasonSize)
				{
					goto out_switch;
				}
				if (paramDebug >= 150)
				{
					dumpData();
				}
				handleCopyData(len, 1);
				dprintk(70, "FP_RSP_WAKEUP_REASON (0x%02x) processed\n", FP_RSP_WAKEUP_REASON);
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetWakeupReasonSize) % BUFFERSIZE;
				break;
			}
			default:  // Ignore unknown response
			{
				dprintk(1, "Unknown FP response %02x received, discarding\n", expectEventData);
				dprintk(1, "start %d end %d\n",  RCVBufferStart,  RCVBufferEnd);
				dumpData();

				/* discard all data, because this happens currently
				 * sometimes. do not know the problem here.
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

	dprintk(200, "%s >\n", __func__);

	while (*ASC_X_INT_STA & ASC_INT_STA_RBF)
	{
		RCVBuffer [RCVBufferStart] = *ASC_X_RX_BUFF;
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

/*******************************************
 *
 * Start task to handle responses from
 * front processor.
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
	dprintk(1, "mcomTask died!\n");
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

	dprintk(150, "%s >\n", __func__);
	printk("Opticum HD 9600 frontcontroller module\n");

	// Disable all ASC 3 interrupts
	*ASC_X_INT_EN = *ASC_X_INT_EN & ~0x000001ff;

	// Setup ASC3
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

	dprintk(150, "%s Requesting IRQ %d for ASC3\n", __func__, InterruptLine);
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
		dprintk(1, "%s: Cannot get IRQ\n", __func__);
	}
	msleep(waitTime);

	mcom_init_func();  // initialize driver

	if (register_chrdev(VFD_MAJOR, "VFD", &vfd_fops))
	{
		dprintk(1, "unable to get major %d for VFD/MICOM\n", VFD_MAJOR);
	}

	create_proc_fp();

	dprintk(150, "%s <\n", __func__);
	return 0;
}

static void __exit mcom_cleanup_module(void)
{
	printk("Opticum HD 9600 frontcontroller module unloading\n");

	remove_proc_fp();
	unregister_chrdev(VFD_MAJOR, "VFD");
	free_irq(InterruptLine, NULL);
}

//----------------------------------------------

module_init(mcom_init_module);
module_exit(mcom_cleanup_module);

MODULE_DESCRIPTION("Opticum HD 9600 frontcontroller module");
MODULE_AUTHOR("Open Vision developers");
MODULE_LICENSE("GPL");

module_param(paramDebug, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(paramDebug, "Debug Output 0=disabled >0=enabled(debuglevel)");

module_param(waitTime, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(waitTime, "Wait before init in ms (default=1000)");

module_param(gmt_offset, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(gmt_offset, "GMT offset (default 3600");
// vim:ts=4
