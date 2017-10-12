/******************************************************************************
* Copyright (C) 2016-2017 IMMS GmbH, Thomas Elste <thomas.elste@imms.de>

* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
* USA.
******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <sys/utsname.h>

#include <cyclicping.h>
#include <ftrace.h>

/**
 * Convert serialized timespec struct from buffer back.
 *
 * \param buffer Input buffer for conversion.
 * \param tspec Resulting time.
 */
void buffer2tspec(const char *buffer, struct timespec *tspec)
{
	uint64_t cp[2];

	cp[0]=*(uint64_t*)(buffer);
	cp[1]=*(uint64_t*)(buffer+sizeof(uint64_t));

	tspec->tv_sec=(time_t)cp[0];
	tspec->tv_nsec=(long)cp[1];
}

/**
 * Serialize timespec to buffer.
 *
 * \param tspec Timespec to convert.
 * \param buffer Result is stored here.
 */
void tspec2buffer(const struct timespec *tspec, char *buffer)
{
	uint64_t cp[2];

	cp[0]=(uint64_t)tspec->tv_sec;
	cp[1]=(uint64_t)tspec->tv_nsec;

	memcpy(buffer, &cp, 2*sizeof(uint64_t));
}

/**
 * Add packet time data to statistics.
 *
 * \param cfg Cyclicping config data.
 * \param type Type of statistic (send, recv, all).
 * \param start Start time.
 * \param end End time.
 * \return 0 on success, else 1.
 */
int add_stats(struct cyclicping_cfg *cfg, enum stat_type type,
	const struct timespec *start, const struct timespec *end)
{
	int64_t ndelta;

	/* calculate delta in ns */
	ndelta=TSPEC_TO_NSEC(end)-TSPEC_TO_NSEC(start);

	/* sanity check delta value */
	if(ndelta<=0 || ndelta>NSEC_PER_SEC) {
		if(ndelta<=0)
			fprintf(stderr, "packet receive time equal or before "
				"transmit time\n");

		if(ndelta>NSEC_PER_SEC)
			fprintf(stderr, "packet round trip time to large\n");

		if(type!=STAT_ALL) {
			fprintf(stderr, "check time synchronization between "
				"client and server\n");
		}

		return 1;
	}

	/* convert to us or ms as requested */
	ndelta/=cfg->opts.ms?1000000:1000;

	/* if breaktrace is requested and rtt is greater stop the
	 * running trace */
	if(type==STAT_ALL && cfg->opts.breaktrace) {
		if(ndelta>cfg->opts.breaktrace) {
			stop_ftrace();
			cfg->opts.breaktrace=0;
		}
	}

	/* increment histogram bin */
	if(cfg->opts.histogram) {
		if(ndelta>=cfg->opts.histogram)
			cfg->stat[type].histogram_data[cfg->opts.histogram-1]++;
		else
			cfg->stat[type].histogram_data[ndelta]++;
	}

	/* new max or min? */
	if(ndelta<cfg->stat[type].min)
		cfg->stat[type].min=ndelta;
	if(ndelta>cfg->stat[type].max)
		cfg->stat[type].max=ndelta;

	/* store to dump space if requested */
	if(cfg->dump)
		cfg->dump[cfg->stat[type].cnt].time[type]=ndelta;

	cfg->stat[type].cnt++;
	cfg->stat[type].avg+=(double)ndelta;

	return 0;
}

/**
 * Runtime statistic.
 *
 * \param cfg Cyclicping config data.
 * \param send Client Send timestamp.
 * \param server Server receive timestamp.
 * \param recv Client receive timestamp.
 */
void print_stats(struct cyclicping_cfg *cfg,
	const struct timespec *send, const struct timespec *server,
	const struct timespec *recv)
{
	uint64_t tdelta;

	if(cfg->opts.quiet)
		return;

	tdelta=TSPEC_TO_NSEC(recv)-TSPEC_TO_NSEC(send);
	tdelta/=cfg->opts.ms?1000000:1000;
	printf("Cnt:%8" PRIu64 " (all)  Min:%8u Act:%10u Avg:%10u Max:%10u\n",
		cfg->stat[STAT_ALL].cnt, cfg->stat[STAT_ALL].min,
		(uint32_t)tdelta, (uint32_t)(cfg->stat[STAT_ALL].avg/
		(double)cfg->stat[STAT_ALL].cnt), cfg->stat[STAT_ALL].max);

	if(cfg->opts.two_way) {
		tdelta=TSPEC_TO_NSEC(server)-TSPEC_TO_NSEC(send);
		tdelta/=cfg->opts.ms?1000000:1000;
		printf("             (send) Min:%8u Act:%10u Avg:%10u "
			"Max:%10u\n", cfg->stat[STAT_SEND].min,
			(uint32_t)tdelta, (uint32_t)(cfg->stat[STAT_SEND].avg/
			(double)cfg->stat[STAT_SEND].cnt),
			cfg->stat[STAT_SEND].max);

		tdelta=TSPEC_TO_NSEC(recv)-TSPEC_TO_NSEC(server);
		tdelta/=cfg->opts.ms?1000000:1000;
		printf("             (recv) Min:%8u Act:%10u Avg:%10u "
			"Max:%10u\n", cfg->stat[STAT_RECV].min,
			(uint32_t)tdelta, (uint32_t)(cfg->stat[STAT_RECV].avg/
			(double)cfg->stat[STAT_RECV].cnt),
			cfg->stat[STAT_RECV].max);

		printf("\033[%dA", 3);
	} else {
		printf("\033[%dA", 1);
	}
}

/**
 * Convert timeval to string.
 *
 * \param tv Time to convert.
 * \param buffer Destination buffer.
 */
void tv_to_str(struct timeval tv, char *buffer)
{
	int millisec;
	struct tm* tm_info;

	/* Round to nearest millisec */
	millisec = lrint(tv.tv_usec/1000.0);

	/* Allow for rounding up to nearest second */
	if (millisec>=1000) {
		millisec -=1000;
		tv.tv_sec++;
	}

	tm_info = localtime(&tv.tv_sec);

	strftime(buffer, 26, "%Y:%m:%d %H:%M:%S", tm_info);
	sprintf(buffer, "%s.%03d", buffer, millisec);
}

/**
 * Print histogram header containing all kinds of information of the current
 * RTT measurement.
 *
 * \param cfg Cyclicping config data.
 * \param argc Main argument count.
 * \param argv Main arguments.
 */
void print_histogram_header(struct cyclicping_cfg *cfg, int argc, char *argv[])
{
	struct cyclicping_opts *opts=&cfg->opts;
	char tstr[26];
	int i;
	struct utsname uts;

	uname (&uts);

	printf("# cyclicping %s histogram data\n", VERSION);
	printf("# cmdline: ");

	for(i=1; i<argc; i++)
		printf("%s ", argv[i]);

	printf("\n# machine: %s\n", uts.machine);
	printf("# kernel: %s (%s)\n", uts.release, uts.version);
	tv_to_str(cfg->test_start, tstr);
	printf("# start: %s\n", tstr);
	tv_to_str(cfg->test_end, tstr);
	printf("# end: %s\n", tstr);
	printf("# interface: %s\n", cfg->current_mod->name);
	printf("# packet interval (us): %d\n", opts->interval);
	printf("# packet length (bytes): %d\n", opts->length);
	printf("# unit: %s\n", opts->ms?"ms":"us");
	printf("# packet count: %" PRIu64 "\n", cfg->stat[STAT_ALL].cnt);
	printf("# two-way mode: %d\n", cfg->opts.two_way);
	if(cfg->opts.two_way) {
		printf("# minimum rtt: %d %d %d\n", cfg->stat[STAT_ALL].min,
			cfg->stat[STAT_SEND].min, cfg->stat[STAT_RECV].min);
		printf("# average rtt: %d %d %d\n",
			(uint32_t)(cfg->stat[STAT_ALL].avg/
				(double)cfg->stat[STAT_ALL].cnt),
			(uint32_t)(cfg->stat[STAT_SEND].avg/
				(double)cfg->stat[STAT_SEND].cnt),
			(uint32_t)(cfg->stat[STAT_RECV].avg/
				(double)cfg->stat[STAT_RECV].cnt));
		printf("# maximum rtt: %d %d %d\n", cfg->stat[STAT_ALL].max,
			cfg->stat[STAT_SEND].max, cfg->stat[STAT_RECV].max);
	} else {
		printf("# minimum rtt: %d\n", cfg->stat[STAT_ALL].min);
		printf("# average rtt: %d\n",
				(uint32_t)(cfg->stat[STAT_ALL].avg/
				(double)cfg->stat[STAT_ALL].cnt));
		printf("# maximum rtt: %d\n", cfg->stat[STAT_ALL].max);
	}
	printf("\n");
}

/*
 * Print histogram data.
 *
 * \param cfg Cyclicping config data.
 */
void print_histogram_data(struct cyclicping_cfg *cfg)
{
	int i;
	struct cyclicping_opts *opts=&cfg->opts;

	printf("#  rtt  number of packets (sum, send, recv)\n");

	for(i=0; i<opts->histogram; i++) {
		printf("% 6d: %6d", i,
			cfg->stat[STAT_ALL].histogram_data[i]);
		if(cfg->opts.two_way) {
			printf(" %6d %6d\n",
			cfg->stat[STAT_SEND].histogram_data[i],
			cfg->stat[STAT_RECV].histogram_data[i]);
		} else {
			printf("\n");
		}
	}
}

/*
 * Print histogram with header and data.
 *
 * \param cfg Cyclicping config data.
 * \param argc Main argument count.
 * \param argv Main arguments.
 */
void print_histogram(struct cyclicping_cfg *cfg, int argc, char *argv[]) {
	print_histogram_header(cfg, argc, argv);
	print_histogram_data(cfg);
}

/**
 * Print histogram with gnuplot script appended.
 *
 * \param cfg Cyclicping config data.
 * \param argc Main argument count.
 * \param argv Main arguments.
 */
void print_gnuplot_histogram(struct cyclicping_cfg *cfg, int argc, char *argv[])
{
	char tstr[26];
	uint64_t ymax=(uint64_t)pow(10.0f,
		1+floor(log10((double)cfg->stat[STAT_ALL].cnt)));

	print_histogram_header(cfg, argc, argv);

	printf("$histogram << EOD\n");

	print_histogram_data(cfg);

	printf("EOD\n");

	tv_to_str(cfg->test_start, tstr);

	printf("ymax=%" PRIu64 "\n", ymax);
	printf("plotname1=\"%s Latency\"\n", cfg->current_mod->name);
	printf("plotname2=\"%s Latency (send)\"\n", cfg->current_mod->name);
	printf("plotname3=\"%s Latency (receive)\"\n", cfg->current_mod->name);
	printf("set title \"cyclicping latency plot - %s\"\n", tstr);
	printf("set xlabel \"Latency (%s)\"\n", cfg->opts.ms?"ms":"us");
	printf("set xrange [0:%d]\n", cfg->opts.histogram);
	printf("%s", GNUPLOT_HEADER);
	printf("%s", cfg->opts.two_way?GNUPLOT_MULTI_PLOT:GNUPLOT_SINGLE_PLOT);
	printf("pause -1\n");
}

/**
 * Write packet dump to file.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int write_dump(struct cyclicping_cfg *cfg)
{
	FILE *f;
	uint64_t i;

	f=fopen(cfg->opts.dumpfile, "w");
	if(f==NULL) {
		perror("fopen dump file");
		return 1;
	}

	if(cfg->opts.two_way) {
		for(i=0; i<cfg->stat[STAT_ALL].cnt; i++) {
			fprintf(f, "%8" PRIu64 ", %8u, %8u, %8u\n", i,
				cfg->dump[i].time[STAT_ALL],
				cfg->dump[i].time[STAT_SEND],
				cfg->dump[i].time[STAT_RECV]);
		}
	} else {
		for(i=0; i<cfg->stat[STAT_ALL].cnt; i++) {
			fprintf(f, "%8" PRIu64 ", %8u\n", i,
				cfg->dump[i].time[STAT_ALL]);
		}
	}

	fclose(f);

	return 0;
}
