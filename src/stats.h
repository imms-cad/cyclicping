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

#ifndef __STATS_H__
#define __STATS_H__

#include <stdint.h>

#define NSEC_PER_SEC		(1000000000ULL)
#define TSPEC_TO_NSEC(x)	((uint64_t)x->tv_sec*NSEC_PER_SEC + \
	(uint64_t)x->tv_nsec)

struct cyclicping_cfg;

enum stat_type {
	STAT_SEND=0,
	STAT_RECV,
	STAT_ALL,
};

struct tstats {
	uint32_t *histogram_data;
	uint32_t min;
	uint32_t max;
	double avg;
	uint64_t cnt;
};

struct pdump {
	uint32_t time[STAT_ALL+1];
};

void buffer2tspec(const char *buffer, struct timespec *tspec);
void tspec2buffer(const struct timespec *tspec, char *buffer);
void add_stats(struct cyclicping_cfg *cfg, enum stat_type type,
	const struct timespec *start, const struct timespec *end);
void print_stats(struct cyclicping_cfg *cfg,
	const struct timespec *send, const struct timespec *server,
	const struct timespec *recv);
void print_histogram(struct cyclicping_cfg *cfg, int argc, char *argv[]);
void print_gnuplot_histogram(struct cyclicping_cfg *cfg,
	int argc, char *argv[]);
int write_dump(struct cyclicping_cfg *cfg);

#define GNUPLOT_HEADER "\
set ylabel \"Number of samples\" offset -5,0,0\n\
set yrange [0:log10(ymax)]\n\
unset ytics\n\
bfont=\", 14\"\n\
set xtics font bfont\n\
set ytics font bfont\n\
set xlabel font bfont\n\
set ylabel font bfont\n\
set key font bfont\n\
set ytics 1 add (\"0\" 0, \"1\" 1)\n\
# Add major tics\n\
set for [i=2:log10(ymax)] ytics add (sprintf(\"%g\",10**(i-1)) i)\n\
# Add minor tics\n\
set for [i=1:log10(ymax)] for [j=2:9] ytics add (\"\" log10(10**i*j) 1)\n\
set for [j=1:9] ytics add (\"\" j/10. 1) # Add minor tics between 0 and 1\n\
"

#define GNUPLOT_SINGLE_PLOT "\
plot $histogram using ($2 < 1 ? $2 : log10($2)+1) with boxes title plotname1\n\
"

#define GNUPLOT_MULTI_PLOT "\
plot $histogram using ($2 < 1 ? $2 : log10($2)+1) with boxes title \
plotname1, \\\n\
$histogram using ($3 < 1 ? $3 : log10($3)+1) with boxes title plotname2, \\\n\
$histogram using ($4 < 1 ? $4 : log10($4)+1) with boxes title plotname3 \n\
"

#endif
