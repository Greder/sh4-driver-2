/*
 * opt9600_fp_main.c
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
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
 */

/*
 * Opticum 9600HD S/TS Frontpanel driver.
 *
 */

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

#define EVENT_BTN                  0x51
#define EVENT_RC                   0x63
#define EVENT_STANDBY              0x80

#define EVENT_ANSWER_GETTIME       0x15
#define EVENT_ANSWER_WAKEUP_REASON 0x81
#define EVENT_ANSWER_FRONTINFO     0xe4 /* unsused */
#define EVENT_ANSWER_GETIRCODE     0xa5 /* unsused */
#define EVENT_ANSWER_GETPORT       0xb3 /* unsused */

#define DATA_BTN_EVENT   2

//----------------------------------------------
short paramDebug = 10;

static unsigned char expectEventData = 0;
static unsigned char expectEventId = 1;

#define BUFFERSIZE           256  // must be 2 ^ n
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

/* queue data ->transmission is done in the irq */
void mcom_putc(unsigned char data)
{
	unsigned int *ASC_X_INT_EN = (unsigned int *)(ASCXBaseAddress + ASC_INT_EN);

	OutBuffer [OutBufferStart] = data;
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
		printk("wait_event_interruptible failed\n");
		return err;
	}
	else if (err == 0)
	{
		dprintk(0,"Timeout waiting on ack\n");
	}
	else
	{
		dprintk(20, "Command processed - remaining jiffies %d\n", err);
	}
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
		//printk("rcvLen < 3\n");
		return 0;
	}
	/* get packet data length  */
	len = RCVBuffer[(RCVBufferEnd + 1) % BUFFERSIZE];

	if (rcvLen < len + 2)
	{
		/* incomplete packet received */
		//printk("incomplete packet received\n");
		return 0;
	}

	/* check expected length */
	if ((expectedLen == -1) || (len + 2 == expectedLen))
	{
		return len + 2;
	}
	/* expected length is not matched */
	//printk("expected length is not matched\n");
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

	if (KeyBuffer[i] == _MCU_KEYIN)
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

void handleCopyData(int len)
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
	kfree(data);
}

void dumpData(void)
{
	int i, j, len;

#if 0
	if (paramDebug < 150)
	{
		return;
	}
#endif

	len = getLen(-1);

	if (len == 0)
	{
		return;
	}
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
	dprintk(150, "BuffersStart %d, BufferEnd %d, len %d\n", RCVBufferStart, RCVBufferEnd, getLen(-1));

	if (RCVBufferStart != RCVBufferEnd)
	{
		if (paramDebug >= 50)
		{
			dumpData();
		}
	}
}

static void processResponse(void)
{
	int len, i;

	//dumpData();
	len = getLen(-1);

	if (len < cMinimumSize)
	{
		return;
	}
	//dumpData();

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
			case _MCU_KEYIN:
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
				dprintk(1, "_MCU_KEYIN complete\n");

				if (paramDebug >= 50)
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
				//printk("_MCU_KEYIN complete - %02x\n", RCVBuffer[(RCVBufferEnd+2)%BUFFERSIZE]);

				wake_up_interruptible(&wq);

				RCVBufferEnd = (RCVBufferEnd + cPackageSizeKeyIn) % BUFFERSIZE;
				break;
			}
			case _MCU_VERSION:
			{
				len = getLen(cGetVersionSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetVersionSize)
				{
					goto out_switch;
				}
				handleCopyData(len);
				dprintk(20, "Pos. response received\n");
				errorOccured = 0;
				ack_sem_up();

				RCVBufferEnd = (RCVBufferEnd + cGetVersionSize) % BUFFERSIZE;
				break;
			}
			case _MCU_TIME:
			{
				len = getLen(cGetTimeSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetTimeSize)
				{
					goto out_switch;
				}
				handleCopyData(len);
				dprintk(20, "Pos. response received\n");
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetTimeSize) % BUFFERSIZE;
				break;
			}
			case _MCU_WAKEUPREASON:
			{
				len = getLen(cGetWakeupReasonSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetWakeupReasonSize)
				{
					goto out_switch;
				}
				handleCopyData(len);
				dprintk(1, "Pos. response received\n");
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetWakeupReasonSize) % BUFFERSIZE;
				break;
			}
			case _MCU_RESPONSE:
			{
				len = getLen(cGetResponseSize);

				if (len == 0)
				{
					goto out_switch;
				}
				if (len < cGetResponseSize)
				{
					goto out_switch;
				}
				handleCopyData(len);
				dprintk(1, "Pos. response received\n");
				errorOccured = 0;
				ack_sem_up();
				RCVBufferEnd = (RCVBufferEnd + cGetResponseSize) % BUFFERSIZE;
				break;
			}
			default: // Ignore Response
			{
				dprintk(1, "Invalid Response %02x\n", expectEventData);
				dprintk(1, "start %d end %d\n",  RCVBufferStart,  RCVBufferEnd);
				dumpData();

				/* discard all data, because this happens currently
				 * sometimes. dont know the problem here.
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
	unsigned int *ASC_X_INT_EN = (unsigned int *)(ASCXBaseAddress + ASC_INT_EN);
	char         *ASC_X_RX_BUFF = (char *)(ASCXBaseAddress + ASC_RX_BUFF);
	char         *ASC_X_TX_BUFF = (char *)(ASCXBaseAddress + ASC_TX_BUFF);
	int          dataArrived = 0;

	if (paramDebug > 100)
	{
		dprintk(0, "i - ");
	}
	while (*ASC_X_INT_STA & ASC_INT_STA_RBF)
	{
		RCVBuffer [RCVBufferStart] = *ASC_X_RX_BUFF;
//        printk("%02x ", RCVBuffer [RCVBufferStart]);
		RCVBufferStart = (RCVBufferStart + 1) % BUFFERSIZE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
		// We are too fast, let us make a break
		udelay(0);
#endif
		dataArrived = 1;

		if (RCVBufferStart == RCVBufferEnd)
		{
			printk("FP: RCV buffer overflow!!! (%d - %d)\n", RCVBufferStart, RCVBufferEnd);
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
		udelay(0);
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
			//dprintk(0, "event!\n");
			processResponse();

			if (RCVBufferStart == RCVBufferEnd)
			{
				dataAvailable = 0;
			}
			dprintk(150, "start %d end %d\n",  RCVBufferStart,  RCVBufferEnd);
		}
	}
	printk("mcomTask died!\n");
	return 0;
}

//----------------------------------------------

static int __init mcom_init_module(void)
{
	int i = 0;

	// Address for Interrupt enable/disable
	unsigned int *ASC_X_INT_EN = (unsigned int *)(ASCXBaseAddress + ASC_INT_EN);
	// Address for FiFo enable/disable
	unsigned int *ASC_X_CTRL   = (unsigned int *)(ASCXBaseAddress + ASC_CTRL);
	dprintk(5, "%s >\n", __func__);

	//Disable all ASC 2 interrupts
	*ASC_X_INT_EN = *ASC_X_INT_EN & ~0x000001ff;

	serial_init();

	init_waitqueue_head(&wq);
	init_waitqueue_head(&rx_wq);
	init_waitqueue_head(&ack_wq);

	for (i = 0; i < LASTMINOR; i++)
	{
		sema_init(&FrontPanelOpen[i].sem, 1);
	}
	kernel_thread(mcomTask, NULL, 0);

	//Enable the FIFO
	*ASC_X_CTRL = *ASC_X_CTRL | ASC_CTRL_FIFO_EN;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
	i = request_irq(InterruptLine, (void *)FP_interrupt, IRQF_DISABLED, "FP_serial", NULL);
#else
	i = request_irq(InterruptLine, (void *)FP_interrupt, SA_INTERRUPT, "FP_serial", NULL);
#endif

	if (!i)
	{
		*ASC_X_INT_EN = *ASC_X_INT_EN | 0x00000001;
	}
	else
	{
		dprintk(1, "FP: Cannot get irq\n");
	}
	msleep(1000);
	mcom_init_func();

	if (register_chrdev(VFD_MAJOR, "VFD", &vfd_fops))
	{
		dprintk(1, "unable to get major %d for VFD/MCOM\n", VFD_MAJOR);
	}
	dprintk(10, "%s <\n", __func__);
	return 0;
}

static void __exit mcom_cleanup_module(void)
{
	printk("Opticum 9600HD frontcontroller module unloading\n");

	unregister_chrdev(VFD_MAJOR, "VFD");

	free_irq(InterruptLine, NULL);
}

//----------------------------------------------

module_init(mcom_init_module);
module_exit(mcom_cleanup_module);

MODULE_DESCRIPTION("Opticum 9600HD frontcontroller module");
MODULE_AUTHOR("Open Vision developers");
MODULE_LICENSE("GPL");

module_param(paramDebug, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(paramDebug, "Debug Output 0=disabled >0=enabled(debuglevel)");
// vim:ts=4
