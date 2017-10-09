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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

#include <cyclicping.h>
#include <opts.h>

void help(struct cyclicping_cfg *cfg)
{
	int i=0;

	printf("cyclicping - RT round trip time measuring tool\n\n");
	printf("Usage:\n");
	printf("  cyclicping <options>\n\n");
	printf("Options:\n");
	printf("-2      --two-way       Collect additional receive and send "
		"time statistics\n");
	printf("                        (hosts have to be time "
		"synchronized).\n");
	printf("-a <nr> --affinity <nr> Run on processor <nr>.\n");
	printf("-b <t>  --breaktrace    Abort ftrace if latency is "
		"greater <t>.\n");
	printf("-c      --client        Run in client mode.\n");
	printf("-C <c>  --clock <c>     Select clock (0 MONOTONIC, "
		"1 REALTIME).\n");
	printf("-d <f>  --dump <f>      Dump packet times to file <f>.\n");
	printf("-f      --ftrace        Enable ftrace.\n");
	printf("-g      --gnuplot       Ouput gnuplot script with histogram.\n");
	printf("-h      --help          Displays this information.\n");
	printf("-H <h>  --histogram <h> Generate histogram width "
		"depth <h>.\n");
	printf("-i <i>  --interval <i>  Packet interval in us "
		"(default: %d).\n", DEFAULT_INTERVAL);
	printf("-l <l>  --loops <l>     Send <l> packets, then quit.\n");
	printf("-L <l>  --length <l>    Packet length in bytes "
		"(default: %d)\n", DEFAULT_LENGTH);
	printf("-m      --mlockall      Lock process memory.\n");
	printf("-M      --ms            Use ms as output time unit "
		"(default: us).\n");
	printf("-p <p>  --prio <p>      Process priority.\n");
	printf("-P <p>  --so-prio <p>   Socket priority.\n");
	printf("-q      --quiet         Don't print current statistic.\n");
	printf("-s      --server        Run in server mode.\n");
	printf("-u mod  --use mod       Use input/output interface <mod>.\n");
	printf("-v      --verbose       Verbose mode on.\n");
	printf("-V      --version       Displays cyclicpings version "
		"number.\n");

	printf("\nThe following interfaces are available:\n");

	while(cfg->modules[i].name!=NULL) {
		cfg->modules[i].usage();
		i++;
	}

	exit(1);
}

void find_module(struct cyclicping_cfg *cfg)
{
	int i=0;

	while(cfg->modules[i].name!=NULL) {
		if(strncmp(cfg->modules[i].name, cfg->opts.opt_mod,
			strlen(cfg->modules[i].name))==0) {
			cfg->current_mod=&cfg->modules[i];
			break;
		}
		i++;
	}

	if(cfg->current_mod==NULL) {
		fprintf(stderr, "no such interface\n");
		exit(1);
	}
}

int sanitize_cfg(struct cyclicping_cfg *cfg)
{
	struct cyclicping_opts *opts=&cfg->opts;

	if(opts->version) {
		fprintf(stderr, "%s\n", VERSION);
		exit(0);
	}

	if(opts->client && opts->server) {
		fprintf(stderr,
			"can't be client and server at the same time\n");
		exit(1);
	}

	if(!(opts->client || opts->server)) {
		fprintf(stderr,
			"either client or server mode has to be set\n");
		exit(1);
	}

	if(!opts->opt_mod) {
		fprintf(stderr,
			"please choose an output/input interface via -u\n");
		exit(1);
	}

	if(opts->interval==0)
		opts->interval=DEFAULT_INTERVAL;

	if(opts->interval<0) {
		fprintf(stderr, "invalid interval\n");
		exit(1);
	}

	if(opts->number<0) {
		fprintf(stderr, "invalid packet number\n");
		exit(1);
	}

	if(opts->length==0)
		opts->length=DEFAULT_LENGTH;

	if(opts->length<2*sizeof(struct timespec) || opts->length>1<<20) {
		fprintf(stderr, "invalid packet length\n");
		exit(1);
	}

	if(opts->priority<0 || opts->priority>99) {
		fprintf(stderr, "invalid priority\n");
		exit(1);
	}

	if(opts->sopriority<0 || opts->sopriority>255) {
		fprintf(stderr, "invalid socket priority\n");
		exit(1);
	}

	if(opts->opt_affinity && opts->affinity<0) {
		fprintf(stderr, "invalid affinity\n");
		exit(1);
	}

	if(opts->histogram<0 || opts->histogram>1000000) {
		fprintf(stderr, "invalid histogram size\n");
		exit(1);
	}

	if(opts->clock>1 || opts->clock<0) {
		fprintf(stderr, "invalid clock setting\n");
		exit(1);
	}

	if(opts->clock==0)
		opts->clock=CLOCK_MONOTONIC;
	else
		opts->clock=CLOCK_REALTIME;

	if(opts->two_way) {
		if(opts->clock==CLOCK_MONOTONIC) {
			if(!opts->quiet) {
				fprintf(stderr, "switching to CLOCK_REALTIME "
					"for two-way mode\n");
			}
			opts->clock=CLOCK_REALTIME;
		}
		if(!opts->quiet && opts->client) {
			fprintf(stderr, "two-way mode: the server has to use "
				"CLOCK_REALTIME and peers have to be\n"
				"time-synchronized\n");
		}
	}

	if(opts->dumpfile && !opts->number) {
		fprintf(stderr, "packet dump requires loop count (-l).\n");
		exit(1);
	}

	if(opts->breaktrace<0) {
		fprintf(stderr, "invalid value for breaktraceņ.\n");
		exit(1);
	}

	if(opts->breaktrace) {
		opts->ftrace=1;
	}

	return 0;
}

int parse_cfg(int argc, char *argv[], struct cyclicping_cfg *cfg)
{
	struct cyclicping_opts *opts=&cfg->opts;
	int next_option;
	const char* const short_options = "2a:b:cC:d:fghH:i:l:L:mMp:P:qsu:vV";
	const struct option long_options[] = {
		{ "two-way", 0, NULL, '2' },
		{ "affinity", 1, NULL, 'a' },
		{ "breaktrace", 1, NULL, 'b' },
		{ "client", 0, NULL, 'c' },
		{ "clock", 1, NULL, 'C' },
		{ "dump", 0, NULL, 'd' },
		{ "ftrace", 0, NULL, 'f' },
		{ "gnuplot", 0, NULL, 'g' },
		{ "help", 0, NULL, 'h' },
		{ "histogram", 1, NULL, 'H' },
		{ "loops", 1, NULL, 'l' },
		{ "length", 1, NULL, 'L' },
		{ "interval", 1, NULL, 'i' },
		{ "mlockall", 0, NULL, 'm' },
		{ "ms", 0, NULL, 'M' },
		{ "prio", 1, NULL, 'p' },
		{ "so-prio", 1, NULL, 'P' },
		{ "quiet", 0, NULL, 'q' },
		{ "server", 0, NULL, 's' },
		{ "use", 0, NULL, 'u' },
		{ "verbose", 0, NULL, 'v' },
		{ "version", 0, NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	while (1) {
		next_option = getopt_long (argc, argv, short_options,
			long_options, NULL);

		if (next_option == -1)
			break;

		switch (next_option) {
			case '2' :
				opts->two_way=1;
				break;
			case 'a' :
				opts->opt_affinity=optarg;
				opts->affinity=atoi(opts->opt_affinity);
				break;
			case 'b' :
				opts->opt_breaktrace=optarg;
				opts->breaktrace=atoi(opts->opt_breaktrace);
				break;
			case 'c' :
				opts->client=1;
				break;
			case 'C' :
				opts->opt_clock=optarg;
				opts->clock=atoi(opts->opt_clock);
				break;
			case 'd' :
				opts->opt_dumpfile=optarg;
				opts->dumpfile=(char *)malloc(sizeof(char) *
					(strlen(opts->opt_dumpfile)+1));
				strcpy(opts->dumpfile,opts->opt_dumpfile);
				break;
			case 'f' :
				opts->ftrace=1;
				break;
			case 'g' :
				opts->gnuplot=1;
				break;
			case 'h' :
				help(cfg);
				break;
			case 'H' :
				opts->opt_histogram=optarg;
				opts->histogram=atoi(opts->opt_histogram);
				break;
			case 'i' :
				opts->opt_interval=optarg;
				opts->interval=atoi(opts->opt_interval);
				break;
			case 'l' :
				opts->opt_number=optarg;
				opts->number=atoi(opts->opt_number);
				break;
			case 'L' :
				opts->opt_length=optarg;
				opts->length=atoi(opts->opt_length);
				break;
			case 'm' :
				opts->mlock=1;
				break;
			case 'M' :
				opts->ms=1;
				break;
			case 'p' :
				opts->opt_priority=optarg;
				opts->priority=atoi(opts->opt_priority);
				break;
			case 'P' :
				opts->opt_sopriority=optarg;
				opts->sopriority=atoi(opts->opt_sopriority);
				break;
			case 'q' :
				opts->quiet=1;
				break;
			case 's' :
				opts->server=1;
				break;
			case 'u' :
				opts->opt_mod=optarg;
				break;
			case 'v' :
				opts->verbose=1;
				break;
			case 'V' :
				opts->version=1;
				break;
			case '?' :
				help(cfg);
			case -1 :
				break;
			default :
				return(1);
		}
	}

	/* Iterate over rest arguments called argv[optind] */
	while (optind < argc) {
		optind++;
	}

	sanitize_cfg(cfg);
	find_module(cfg);

	return 0;
}
