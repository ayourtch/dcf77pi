/*
Copyright (c) 2013 René Ladan. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#include <stdio.h>
#include "decode_time.h"

uint8_t announce = 0; /* save DST change and leap second announcements */
int olderr = 0; /* save error state to determine if DST change might be valid */

uint8_t
getpar(uint8_t *buffer, int start, int stop)
{
	int i;
	uint8_t par = 0;

	for (i = start; i <= stop; i++)
		par += buffer[i];
	return par & 1;
}

uint8_t
getbcd(uint8_t *buffer, int start, int stop)
{
	int i;
	uint8_t val = 0;
	uint8_t mul = 1;

	for (i = start; i <= stop; i++) {
		val += mul * buffer[i];
		mul *= 2;
		if (mul == 16)
			mul = 10;
	}
	return val;
}

/* based on: xx00-02-28 is a Monday if and only if xx00 is a leap year */
int
isleap(struct tm time)
{
	int d, nw, nd;
	int dayinleapyear[12] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

	if (time.tm_year % 4 > 0)
		return 0;
	else if (time.tm_year % 4 == 0 && time.tm_year > 0)
		return 1;
	else {	/* year == 0 */
		/* weekday 1 is a Monday, assume this is a leap year */
		/* if leap, we should reach Monday 02-28 */

		d = dayinleapyear[time.tm_mon - 1] + time.tm_mday;
		if (d < 60) { /* at or before 02-28 (day 59) */
			nw = (59 - d) / 7;
			nd = time.tm_wday == 1 ? 0 : 8 - time.tm_wday;
			return d + (nw * 7) + nd == 59;
		} else { /* after 02-28 (day 59) */
			nw = (d - 59) / 7;
			nd = time.tm_wday == 1 ? 0 : time.tm_wday - 1;
			return d - (nw * 7) - nd == 59;
		}
	}
}

int
lastday(struct tm time)
{
	if (time.tm_mon == 4 || time.tm_mon == 6 || time.tm_mon == 9 || time.tm_mon == 11)
		return 30;
	if (time.tm_mon == 2)
		return 28 + isleap(time);
	return 31;
}

void
add_day(struct tm *time)
{
	if (++time->tm_wday == 8)
		time->tm_wday = 1;
	if (++time->tm_mday > lastday(*time)) {
		time->tm_mday = 1;
		if (++time->tm_mon == 13) {
			time->tm_mon = 1;
			time->tm_year++;
		}
	}
}

int
add_minute(struct tm *time, int flags)
{
	/* time->tm_isdst indicates the old situation */
	if (++time->tm_min == 60) {
		if (announce & ANN_CHDST) {
			if (time->tm_isdst)
				time->tm_hour--; /* will become DST */
			else
				time->tm_hour++; /* will become non-DST */
			flags |= DT_CHDST;	
			announce &= ~ANN_CHDST;
		}
		time->tm_min = 0;
		if (++time->tm_hour == 24) {
			time->tm_hour = 0;
			add_day(time);
		}
	}
	return flags;
}

int
decode_time(int init2, int minlen, uint8_t *buffer, struct tm *time)
{
	int rval = 0, generr = 0, p1 = 0, p2 = 0, p3 = 0;
	unsigned int tmp, tmp1, tmp2, tmp3;

	if (minlen < 59)
		rval |= DT_SHORT;
	if (minlen > 60)
		rval |= DT_LONG;

	if (buffer[0] == 1)
		rval |= DT_B0;
	if (buffer[20] == 0)
		rval |= DT_B20;

	if (buffer[17] == buffer[18])
		rval |= DT_DSTERR;

	generr = rval; /* do not decode if set */

	if (buffer[15] == 1)
		rval |= DT_XMIT;

	p1 = getpar(buffer, 21, 28);
	tmp = getbcd(buffer, 21, 27);
	if (p1 || tmp > 59) {
		rval |= DT_MIN;
		p1 = 1;
	}
	if (p1 || generr)
		time->tm_min = (time->tm_min + 1) % 60;
	else
		time->tm_min = tmp;

	p2 = getpar(buffer, 29, 35);
	tmp = getbcd(buffer, 29, 34);
	if (p2 || tmp > 23) {
		rval |= DT_HOUR;
		p2 = 1;
	}
	if (p2 || generr) {
		if (time->tm_min == 0)
			time->tm_hour = (time->tm_hour + 1) % 24;
	} else
		time->tm_hour = tmp;

	p3 = getpar(buffer, 36, 58);
	tmp = getbcd(buffer, 36, 41);
	tmp1 = getbcd(buffer, 42, 44);
	tmp2 = getbcd(buffer, 45, 49);
	tmp3 = getbcd(buffer, 50, 57);
	if (p3 || tmp == 0 || tmp > 31 || tmp1 == 0 || tmp2 == 0 || tmp2 > 12 || tmp3 > 99) {
		rval |= DT_DATE;
		p3 = 1;
	}
	if (p3 || generr) {
		if (time->tm_min == 0 && time->tm_hour == 0)
			add_day(time);
	} else {
		time->tm_mday = tmp;
		time->tm_wday = tmp1;
		time->tm_mon = tmp2;
		time->tm_year = tmp3;
	}

	/* these flags are saved between invocations: */
	if (buffer[16] == 1 && generr == 0) /* sz->wz -> h==2 .. wz->sz -> h==1 */
		announce |= ANN_CHDST;
	if (buffer[19] == 1 && generr == 0) /* h==0 (UTC) */
		announce |= ANN_LEAP;

	if (minlen == 59) {
		if ((announce & ANN_LEAP) && (time->tm_min == 0)) {
			announce &= ~ANN_LEAP;
			rval |= DT_LEAP | DT_SHORT;
			/* leap second processed, but missing */	
		}
	}
	if (minlen == 60) {
		if ((announce & ANN_LEAP) && (time->tm_min == 0)) {
			announce &= ~ANN_LEAP;
			rval |= DT_LEAP;
			/* leap second processed */
			if (buffer[59] == 1)
				rval |= DT_LEAPONE;
		} else
			rval |= DT_LONG;
	}

	if (buffer[17] != time->tm_isdst) {
		/* Time offset change is OK if:
		 * there was an error but not any more
		 * initial state
		 * actually announced and minute = 0
		 */
		if ((olderr && !generr && !p1 && !p2 && !p3) || init2 || ((announce & ANN_CHDST) && time->tm_min == 0)) {
			olderr = 0;
			time->tm_isdst = buffer[17]; /* expected change */
		} else
			rval |= DT_DSTERR; /* sudden change */
	}
	time->tm_gmtoff = time->tm_isdst ? 7200 : 3600;

	if (generr || p1 || p2 || p3)
		olderr = 1;
	return rval;
}

void
display_time(int init2, int dt, struct tm oldtime, struct tm time)
{
	char *wday[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

	printf("%s %02d-%02d-%02d %s %02d:%02d",
	    time.tm_isdst ? "summer" : "winter", time.tm_year, time.tm_mon,
	    time.tm_mday, wday[time.tm_wday-1], time.tm_hour, time.tm_min);
	if (dt & DT_DSTERR)
		printf("Time offset error\n");
	if (dt & DT_MIN)
		printf("Minute parity/value error\n");
	if (!init2 && oldtime.tm_min != time.tm_min)
		printf("Minute value jump\n");
	if (dt & DT_HOUR)
		printf("Hour parity/value error\n");
	if (!init2 && oldtime.tm_hour != time.tm_hour)
		printf("Hour value jump\n");
	if (dt & DT_DATE)
		printf("Date parity/value error\n");
	if (!init2 && oldtime.tm_wday != time.tm_wday)
		printf("Day-of-week value jump\n");
	if (!init2 && oldtime.tm_mday != time.tm_mday)
		printf("Day-of-month value jump\n");
	if (!init2 && oldtime.tm_mon != time.tm_mon)
		printf("Month value jump\n");
	if (!init2 && oldtime.tm_year != time.tm_year)
		printf("Year value jump\n");
	if (dt & DT_B0)
		printf("Minute marker error\n");
	if (dt & DT_B20)
		printf("Date/time start marker error\n");
	if (dt & DT_XMIT)
		printf("Transmitter call bit set\n");
	if (announce & ANN_CHDST)
		printf("Time offset change announced\n");
	if (announce & ANN_LEAP)
		printf("Leap second announced\n");
	if (dt & DT_CHDST)
		printf("Time offset changed\n");
	if (dt & DT_LEAP) {
		printf("Leap second processed");
		if (dt & DT_LEAPONE)
			printf(", value is one instead of zero");
		printf("\n");
	}
}
