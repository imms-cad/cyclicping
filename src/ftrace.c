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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define SYS	"/sys/kernel/debug/tracing/"

static FILE *ftraceOnFile=NULL;

int write_to_tracefile(char *file, char *value)
{
	FILE *f;
	char path[256];

	snprintf(path, 256, "%s%s", SYS, file);

	f=fopen(path, "w");
	if(f==NULL) {
		perror("fopen tracefile");
		return 1;
	}

	fprintf(f, "%s", value);
	fclose(f);

	return 0;
}

void start_ftrace(void)
{
	if(ftraceOnFile) {
		fprintf(ftraceOnFile, "1\n");
		fflush(ftraceOnFile);
	}
}

void stop_ftrace(void)
{
	if(ftraceOnFile) {
		fprintf(ftraceOnFile, "0\n");
		fflush(ftraceOnFile);
	}
}

int setup_ftrace(void)
{
	char pid[16];
	int ret=0;

	ftraceOnFile=fopen(SYS "tracing_on", "w");
	if(ftraceOnFile==NULL) {
		perror("fopen trace file");
		return 1;
	}

	stop_ftrace();

	ret|=write_to_tracefile("trace", "\n");
	ret|=write_to_tracefile("current_tracer", "function_graph");

	ret|=write_to_tracefile("set_ftrace_notrace",
		"*spin_* *rcu_* preempt_count*");

	snprintf(pid, 16, "%d\n", getpid());
	ret|=write_to_tracefile("set_ftrace_pid", pid);

	return ret;
}
