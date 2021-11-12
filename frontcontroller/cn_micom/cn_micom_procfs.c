/*
 * cn_micom_procfs.c
 *
 * (c) 2021 Audioniek
 *
 * procfs for CreNova front panel driver.
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
 * 20210929 Audioniek       Initial version based on nuvoton_procfs.c.
 * 20211001 Audioniek       model_name: add detection of DVB-T tuner.
 * 20211025 Audioniek       Add /proc/stb/info/OEM, brand and model_name.
 * 20211025 Audioniek       /proc/stb/fp/resellerID added.
 * 
 ****************************************************************************/

#include <linux/proc_fs.h>      /* proc fs */
#include <asm/uaccess.h>        /* copy_from_user */
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include "cn_micom_fp.h"

/*
 *  /proc/------
 *             |
 *             +--- progress (rw)           Progress of E2 startup in %
 *
 *  /proc/stb/power
 *             |
 *             +--- vfd (rw)                Front panel display on/off
 *
 *  /proc/stb/fp
 *             |
 *             +--- version (r)             SW version of front processor (hundreds = major, ten/units = minor)
 *             +--- resellerID (r)          resellerID in boot loader (atemio520 only)
 *             +--- rtc (rw)                RTC time (UTC, seconds since Unix epoch))
 *             +--- rtc_offset (rw)         RTC offset in seconds from UTC
 *             +--- wakeup_time (rw)        Next wakeup time (absolute, local, seconds since Unix epoch)
 *             +--- was_timer_wakeup (r)    Wakeup reason (1 = timer, 0 = other)
 *             +--- text (w)                Direct writing of display text
 *
 *  /proc/stb/info
 *             |
 *             +--- OEM (r)                 Name of OEM manufacturer
 *             +--- brand (r)               Name of reseller
 *             +--- model_name (r)          Model name of reseller
 *
 */

/* from e2procfs */
extern int install_e2_procs(char *name, read_proc_t *read_proc, write_proc_t *write_proc, void *data);
extern int remove_e2_procs(char *name, read_proc_t *read_proc, write_proc_t *write_proc);

/* from other cn_micom modules */
extern int mcom_WriteString(char *buf, size_t len);
extern int mcom_SetDisplayOnOff(char level);
extern void clear_display(void);
extern int mcom_GetTime(char *time);
extern int mcom_GetWakeUpMode(int *wakeup_mode);
extern int mcom_SetTime(char *time);

/* Globals */
static int progress = 0;
static int vfd_on = 1;

static int progress_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	char* page;
	char* myString;
	ssize_t ret = -ENOMEM;

	page = (char*)__get_free_page(GFP_KERNEL);

	if (page)
	{
		ret = -EFAULT;
		if (copy_from_user(page, buf, count))
		{
			goto out;
		}
		myString = (char*) kmalloc(count + 1, GFP_KERNEL);
		strncpy(myString, page, count);
		myString[count - 1] = '\0';

		sscanf(myString, "%d", &progress);
		kfree(myString);

		/* always return count to avoid endless loop */
		ret = count;
	}

out:
	free_page((unsigned long)page);
	return ret;
}

static int progress_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;

	if (NULL != page)
	{
		len = sprintf(page,"%d", progress);
	}
	return len;
}	

static int vfd_onoff_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	char* page;
	ssize_t ret = -ENOMEM;
	char* myString;
	int i = 0;

	page = (char*)__get_free_page(GFP_KERNEL);

	if (page)
	{
		ret = -EFAULT;
		if (copy_from_user(page, buf, count))
		{
			goto out;
		}
		myString = (char*) kmalloc(count + 1, GFP_KERNEL);
		strncpy(myString, page, count);
		myString[count - 1] = '\0';

		sscanf(myString, "%d", &vfd_on);
		kfree(myString);

		vfd_on = (vfd_on == 0 ? 0 : 1);
		ret = mcom_SetDisplayOnOff((char)vfd_on);
		if (ret)
		{
			goto out;
		}
		/* always return count to avoid endless loop */
		ret = count;
		goto out;
	}

out:
	free_page((unsigned long)page);
	return ret;
}

static int vfd_onoff_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;

	if (NULL != page)
	{
		len = sprintf(page,"%d", lastdata.display_on);
	}
	return len;
}

static int text_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	char *page;
	int ret = -ENOMEM;

	page = (char *)__get_free_page(GFP_KERNEL);

	if (page)
	{
		ret = -EFAULT;
		if (copy_from_user(page, buf, count) == 0)
		{
			ret = mcom_WriteString(page, count);
			if (ret >= 0)
			{
				ret = count;
			}
		}
		free_page((unsigned long)page);
	}
	return ret;
}

/* Time subroutines */
/*
struct tm
{
int	tm_sec      // seconds after the minute 0-61*
int	tm_min      // minutes after the hour   0-59
int	tm_hour     // hours since midnight     0-23
int	tm_mday     // day of the month         1-31
int	tm_mon      // months since January     0-11
int	tm_year     // years since 1900         0-99
int	tm_wday     // days since Sunday        0-6
int	tm_yday     // days since January 1     0-365
}

time_t long seconds //UTC since epoch  
*/

time_t calcGetmcomTime(char *time)
{  // mjd hh:mm:ss -> seconds since epoch
	unsigned int    mjd     = ((time[0] & 0xff) * 256) + (time[1] & 0xff);
	unsigned long   epoch   = ((mjd - 40587) * 86400);
	unsigned int    hour    = time[2] & 0xff;
	unsigned int    min     = time[3] & 0xff;
	unsigned int    sec     = time[4] & 0xff;

	epoch += (hour * 3600 + min * 60 + sec);
	return epoch;
}

#define LEAPYEAR(year) (!((year) % 4) && (((year) % 100) || !((year) % 400)))
#define YEARSIZE(year) (LEAPYEAR(year) ? 366 : 365)
static const int _ytab[2][12] =
{
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

struct tm *gmtime(register const time_t time)
{ // seconds since epoch -> struct tm
	static struct tm fptime;
	register unsigned long dayclock, dayno;
	int year = 70;

	dayclock = (unsigned long)time % 86400;
	dayno = (unsigned long)time / 86400;

	fptime.tm_sec = dayclock % 60;
	fptime.tm_min = (dayclock % 3600) / 60;
	fptime.tm_hour = dayclock / 3600;
	fptime.tm_wday = (dayno + 4) % 7;  // day 0 was a thursday
	while (dayno >= YEARSIZE(year))
	{
		dayno -= YEARSIZE(year);
		year++;
	}
	fptime.tm_year = year;
	fptime.tm_yday = dayno;
	fptime.tm_mon = 0;
	while (dayno >= _ytab[LEAPYEAR(year)][fptime.tm_mon])
	{
		dayno -= _ytab[LEAPYEAR(year)][fptime.tm_mon];
		fptime.tm_mon++;
	}
	fptime.tm_mday = dayno;
//	fptime.tm_isdst = -1;

	return &fptime;
}

int getMJD(struct tm *theTime)
{  // struct tm (date) -> mjd
	int mjd = 0;
	int year;

	year  = theTime->tm_year - 1;  // do not count current year
	while (year >= 70)
	{
		mjd += 365;
		if (LEAPYEAR(year))
		{
			mjd++;
		}
		year--;
	}
	mjd += theTime->tm_yday;
	mjd += 40587;  // mjd starts on midnight 17-11-1858 which is 40587 days before unix epoch

	return mjd;
}

void calcSetmcomTime(time_t theTime, unsigned char *destString)
{  // seconds since epoch -> mjd h:m:s
	struct tm *now_tm;
	int mjd;

//	dprintk(150, "%s > Time to calculate: %d seconds\n", __func__, (int)theTime);
	now_tm = gmtime(theTime);
	mjd = getMJD(now_tm);
	destString[0] = ((mjd >> 8) & 0xff);
	destString[1] = (mjd & 0xff);
	destString[2] = now_tm->tm_hour;
	destString[3] = now_tm->tm_min;
	destString[4] = now_tm->tm_sec;
//	dprintk(150, "%s < Calculated time: MJD=%x%x %02d:%02d:%02d\n", __func__, destString[0], destString[1], destString[2], destString[3], destString[4]);
}

static int read_rtc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	int res;
	long rtc_time;
	char fptime[5];

	res = mcom_GetTime(fptime);  // get front panel clock time (local)
	rtc_time = calcGetmcomTime(fptime) - rtc_offset;  // return UTC

	if (NULL != page)
	{
		len = sprintf(page,"%u\n", (int)(rtc_time));
	}
	return len;
}

static int write_rtc(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char *page = NULL;
	ssize_t ret = -ENOMEM;
	time_t argument = 0;
	int test = -1;
	char *myString = kmalloc(count + 1, GFP_KERNEL);
	unsigned char time[5];

	page = (char *)__get_free_page(GFP_KERNEL);

	if (page)
	{
		ret = -EFAULT;

		if (copy_from_user(page, buffer, count))
		{
			goto out;
		}
		strncpy(myString, page, count);
		myString[count] = '\0';

		test = sscanf (myString, "%u", (unsigned int *)&argument);

		if (test > 0)
		{
			calcSetmcomTime((argument + rtc_offset), time);
			ret = mcom_SetTime(time); // update front panel clock in local time
		}
		/* always return count to avoid endless loop */
		ret = count;
	}

out:
	free_page((unsigned long)page);
	kfree(myString);
	return ret;
}

static int read_rtc_offset(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;

	if (NULL != page)
	{
		len = sprintf(page, "%d\n", rtc_offset);
	}
	return len;
}

static int write_rtc_offset(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char *page = NULL;
	ssize_t ret = -ENOMEM;
	int test = -1;
	char *myString = kmalloc(count + 1, GFP_KERNEL);

	page = (char *)__get_free_page(GFP_KERNEL);

	if (page)
	{
		ret = -EFAULT;
		if (copy_from_user(page, buffer, count))
		{
			goto out;
		}
		strncpy(myString, page, count);
		myString[count] = '\0';

		test = sscanf (myString, "%d", &rtc_offset);

		/* always return count to avoid endless loop */
		ret = count;
	}

out:
	free_page((unsigned long)page);
	kfree(myString);
	return ret;
}

static int wakeup_time_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char *page = NULL;
	ssize_t ret = -ENOMEM;
	time_t wakeup_time = 0;
	int test = -1;
	char *myString = kmalloc(count + 1, GFP_KERNEL);
	unsigned char wtime[5];

	page = (char *)__get_free_page(GFP_KERNEL);

	if (page)
	{
		ret = -EFAULT;

		if (copy_from_user(page, buffer, count))
		{
			goto out;
		}
		strncpy(myString, page, count);
		myString[count] = '\0';

		test = sscanf(myString, "%u", (unsigned int *)&wakeup_time);

		if (0 < test)
		{
			calcSetmcomTime((wakeup_time + rtc_offset), mcom_wakeup_time);  // set FP wake up time (local)
		}
		/* always return count to avoid endless loop */
		ret = count;
	}

out:
	free_page((unsigned long)page);
	kfree(myString);
	return ret;
}

static int wakeup_time_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	int	w_time;

	if (NULL != page)
	{
		w_time = calcGetmcomTime(mcom_wakeup_time) - rtc_offset;  // UTC

		len = sprintf(page, "%u\n", w_time);
	}
	return len;
}

static int was_timer_wakeup_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	int wakeup_mode = -1;

	if (NULL != page)
	{
		if (wakeupreason == TIMER)
		{
			wakeup_mode = 1;
		}
		else
		{
			wakeup_mode = 0;
		}
		len = sprintf(page,"%d\n", wakeup_mode);
	}
	return len;
}

static int fp_version_read(char *page, char **start, off_t off, int count, int *eof, void *data_unused)
{
	int len = 0;

	if (NULL != page)
	{
		dprintk(20, "Version is %X.%02X\n", mcom_version[0], mcom_version[1]);
		len = sprintf(page, "%d\n", (int)(((mcom_version[0] & 0x0f) * 100) + (((mcom_version[1] >> 4) & 0x0f) * 10) + (mcom_version[1] & 0x0f)));
	}
	return len;
}

#if defined(ATEMIO520) || defined(ATEMIO530)
static int fp_reseller_read(char *page, char **start, off_t off, int count, int *eof, void *data_unused)
{
	int len = 0;

	if (NULL != page)
	{
		len = sprintf(page, "%s\n", "252902A5");
	}
	return len;
}
#endif

static int oem_name_read(char *page, char **start, off_t off, int count, int *eof, void *data_unused)
{
	int len = 0;

	if (NULL != page)
	{
		len = sprintf(page, "CreNova\n");
	}
	return len;
}

int opt9600_ts_detect(void)
{
	int ret;
	unsigned char buf;

	// DVB-tuner is at address 0x40 on I2C bus 1
	struct i2c_msg msg = { .addr = 0x40, I2C_M_RD, .buf = &buf, .len = 1 };
	struct i2c_adapter *i2c_adap = i2c_get_adapter(1);

	ret = i2c_transfer(i2c_adap, &msg, 1);
	return ret;
}

static int brand_name_read(char *page, char **start, off_t off, int count, int *eof, void *data_unused)
{
	int len = 0;
	int ts_model = 0;

	if (NULL != page)
	{
#if defined(ATEMIO520) || defined(ATEMIO530)
		len = sprintf(page, "%s\n", "Atemio");
#else
		len = sprintf(page, "%s\n", "Opticum/Orton");
#endif
	}
	return len;
}

static int model_name_read(char *page, char **start, off_t off, int count, int *eof, void *data_unused)
{
	int len = 0;
#if defined(OPT9600PRIMA)
	int ts_model = 0;

	// detect DVB-T tuner on TS models
	ts_model = opt9600_ts_detect();
#endif

	if (NULL != page)
	{
#if defined(ATEMIO520)
		len = sprintf(page, "%s\n", "AM 520 HD");
#elif defined(ATEMIO530)
		len = sprintf(page, "%s\n", "AM 530 HD");
#elif defined(OPT9600MINI)
		len = sprintf(page, "%s\n", "HD 9600 Mini");
#else
		len = sprintf(page, "%s%s%s\n", "HD ", (ts_model == 1 ? "TS " : ""), "9600 PRIMA");
#endif
	}	
	dprintk(50, "%s < %d\n", __func__, len);
	return len;
}

/*
static int null_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	return count;
}
*/

struct fp_procs
{
	char *name;
	read_proc_t *read_proc;
	write_proc_t *write_proc;
} fp_procs[] =
{
	{ "progress", progress_read, progress_write },
#if defined(ATEMIO520) || defined(ATEMIO530)
	{ "stb/fp/resellerID", fp_reseller_read, NULL },
#endif
	{ "stb/fp/rtc", read_rtc, write_rtc },
	{ "stb/fp/rtc_offset", read_rtc_offset, write_rtc_offset },
	{ "stb/fp/version", fp_version_read, NULL },
	{ "stb/power/vfd", vfd_onoff_read, vfd_onoff_write },
	{ "stb/fp/text", NULL, text_write },
	{ "stb/fp/wakeup_time", wakeup_time_read, wakeup_time_write },
	{ "stb/fp/was_timer_wakeup", was_timer_wakeup_read, NULL },
	{ "stb/info/OEM", oem_name_read, NULL },
	{ "stb/info/brand", brand_name_read, NULL },
	{ "stb/info/model_name", model_name_read, NULL },
};

void create_proc_fp(void)
{
	int i;

	for (i = 0; i < sizeof(fp_procs)/sizeof(fp_procs[0]); i++)
	{
		install_e2_procs(fp_procs[i].name, fp_procs[i].read_proc, fp_procs[i].write_proc, NULL);
	}
}

void remove_proc_fp(void)
{
	int i;

	for (i = sizeof(fp_procs)/sizeof(fp_procs[0]) - 1; i >= 0; i--)
	{
		remove_e2_procs(fp_procs[i].name, fp_procs[i].read_proc, fp_procs[i].write_proc);
	}
}
// vim:ts=4
