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
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "input.h"
#include "decode_time.h"
#include "decode_alarm.h"

int
main(int argc, char *argv[])
{
	uint8_t indata[40];
	uint8_t civbuf[40];
	struct tm time, oldtime;
	uint8_t civ1 = 0, civ2 = 0;
	int dt, bit, bitpos, minlen = 0, init = 1, init2 = 1;
	int res;
	int opt;
	int verbose = 0;
	char *infilename, *logfilename;

	infilename = logfilename = NULL;
	while ((opt = getopt(argc, argv, "f:l:v")) != -1) {
		switch (opt) {
		case 'f' :
			infilename = strdup(optarg);
			if (infilename == NULL) {
				perror("infilename");
				return errno;
			}
			break;
		case 'l' :
			logfilename = strdup(optarg);
			if (logfilename == NULL) {
				perror("logfilename");
				return errno;
			}
			break;
		case 'v' :
			verbose = 1;
			break;
		default:
			printf("usage: %s [-f infile] [-l logfile] [-v]\n", argv[0]);
			return EX_USAGE;
		}
	}

	res = set_mode(verbose, infilename, logfilename);
	if (res) {
		/* something went wrong */
		cleanup();
		return 0;
	}

	/* no weird values please */
	bzero(indata, sizeof(indata));
	bzero(&time, sizeof(time));

	for (;;) {
		bit = get_bit();
		while (bit & GETBIT_IGNORE)
			bit = get_bit(); /* when reading from log file */
		if (bit & GETBIT_EOD)
			break;
		if (bit & GETBIT_XMIT)
			printf("x");
		if (bit & GETBIT_RECV)
			printf("r");

		bitpos = get_bitpos();
		if (bit & GETBIT_EOM) {
			minlen = bitpos;
			if (infilename == NULL) {
				minlen++;
				display_bit();
				/* handle the missing minute marker */
			}
		} else
			display_bit();
		fflush(stdout);

		if (!init) {
			switch (time.tm_min % 3) {
			case 0:
				/* copy civil warning data */
				if (bitpos > 1 && bitpos < 8)
					indata[bitpos - 2] = bit & GETBIT_ONE;
					/* 2..7 -> 0..5 */
				if (bitpos > 8 && bitpos < 15)
					indata[bitpos - 3] = bit & GETBIT_ONE;
					/* 9..14 -> 6..11 */

				/* copy civil warning flags */
				if (bitpos == 1)
					civ1 = bit & GETBIT_ONE;
				if (bitpos == 8)
					civ2 = bit & GETBIT_ONE;
				break;
			case 1:
				/* copy civil warning data */
				if (bitpos > 0 && bitpos < 15)
					indata[bitpos + 11] = bit & GETBIT_ONE;
					/* 1..14 -> 11..24 */
			case 2:
				/* copy civil warning data */
				if (bitpos > 0 && bitpos < 15)
					indata[bitpos + 25] = bit & GETBIT_ONE;
					/* 1..14 -> 25..39 */
				if (bitpos == 15)
					memcpy(civbuf, indata, sizeof(civbuf));
					/* take snapshot of civil warning buffer */
				break;
			}
		}

		bit = next_bit();
		if (bit & GETBIT_TOOLONG)
			printf(" >");

		if (bit & GETBIT_EOM) {
			dt = decode_time(init2, minlen, get_buffer(), &time);
			printf(" %d %c\n", minlen, dt & DT_LONG ? '>' :
			    dt & DT_SHORT ? '<' : ' ');

			if (time.tm_min % 3 == 0) {
				if (civ1 == 1 && civ2 == 1)
					display_alarm(civbuf);
				if (civ1 != civ2)
					printf("Civil warning error\n");
			}

			if (!init)
				dt = add_minute(&oldtime, dt);
			display_time(init2, dt, oldtime, time);
			printf("\n");

			memcpy((void *)&oldtime, (const void *)&time,
			    sizeof(struct tm));
			if (!init && init2)
				init2 = 0;
			if (init)
				init = 0;
		}
	}

	cleanup();
	return 0;
}
