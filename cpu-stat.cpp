/*

Copyright (c) 2014 Harm Hanemaaijer <fgenfb@yahoo.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <dirent.h>
#include <stdint.h>

#include "cpu-stat.h"

/*
 * CPU usage calculation module.
 */

static const bool thread_info_enabled = true;
static uint64_t cached_sc_clk_tck = 0;

struct thread_stats_t {
public :
	int pid;
	char name[32];
	uint64_t utime_ticks;
	int64_t cutime_ticks;
	uint64_t stime_ticks;
	int64_t cstime_ticks;
	uint64_t vsize;		// virtual memory size in bytes
	uint64_t rss;		//Resident  Set  Size in bytes
};

class ProcessStat {
public :
	struct thread_stats_t process_stats;
	int num_threads;
	int max_thread_stats;
	struct thread_stats_t *thread_stats;
	uint64_t cpu_total_time;
};

static void init_pstat(ProcessStat *p) {
	p->num_threads = 0;
	p->max_thread_stats = 0;
	strcpy(p->process_stats.name, "Undefined");
}

static void clear_thread_stats(struct thread_stats_t *thread_stats) {
	thread_stats->utime_ticks = 0;
	thread_stats->cutime_ticks = 0;
	thread_stats->stime_ticks = 0;
	thread_stats->cstime_ticks = 0;
	thread_stats->vsize = 0;
	thread_stats->rss = 0;
}

static void free_thread_stats(ProcessStat *p) {
	if (p->max_thread_stats > 0)
		delete [] p->thread_stats;
}

/*
 * read /proc data into the passed ProcessStat
 * returns 0 on success, -1 on error
 */
static int get_usage(const pid_t pid, ProcessStat *result)
{
	//convert  pid to string
	char pid_s[20];
	snprintf(pid_s, sizeof(pid_s), "%d", pid);
	char stat_filepath[30] = "/proc/";
	strncat(stat_filepath, pid_s,
		sizeof(stat_filepath) - strlen(stat_filepath) - 1);
	char tasks_filepath[64];
	strcpy(tasks_filepath, stat_filepath);
	strncat(stat_filepath, "/stat", sizeof(stat_filepath) -
		strlen(stat_filepath) - 1);

	FILE *fpstat = fopen(stat_filepath, "r");
	if (fpstat == NULL) {
		printf("CPUStat: Couldn't open %s.", stat_filepath);
		return -1;
	}

	FILE *fstat = fopen("/proc/stat", "r");
	if (fstat == NULL) {
		printf("CPUStat: Couldn't open /proc/stat.\n");
		fclose(fstat);
		return -1;
	}
	// Read values from /proc/pid/stat.
	clear_thread_stats(&result->process_stats);
	result->process_stats.pid = pid;
	int64_t rss;
	if (fscanf
	    (fpstat,
	     "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
	     "%lu %ld %ld %*d %*d %d %*d %*u %lu %ld",
	     &result->process_stats.utime_ticks,
	     &result->process_stats.stime_ticks,
	     &result->process_stats.cutime_ticks,
	     &result->process_stats.cstime_ticks, &result->num_threads,
	     &result->process_stats.vsize, &rss) == EOF) {
		fclose(fpstat);
		return -1;
	}
	fclose(fpstat);
	result->process_stats.rss = rss * getpagesize();

	//read+calc cpu total time from /proc/stat
	uint64_t cpu_time[10];
	bzero(cpu_time, sizeof(cpu_time));
	if (fscanf(fstat, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
		   &cpu_time[0], &cpu_time[1], &cpu_time[2], &cpu_time[3],
		   &cpu_time[4], &cpu_time[5], &cpu_time[6], &cpu_time[7],
		   &cpu_time[8], &cpu_time[9]) == EOF) {
		fclose(fstat);
		return -1;
	}
	fclose(fstat);

	result->cpu_total_time = 0;
	for (int i = 0; i < 10; i++)
		result->cpu_total_time += cpu_time[i];

	if (!thread_info_enabled) {
		result->num_threads = 0;
		return 0;
	}

	// Read thread info.
	if (result->max_thread_stats < result->num_threads) {
		if (result->max_thread_stats > 0)
			free(result->thread_stats);
		result->thread_stats = new struct thread_stats_t[result->num_threads];
		result->max_thread_stats = result->num_threads;
	}

	strncat(tasks_filepath, "/task/", 64 - strlen(tasks_filepath) - 1);
	DIR *tasks_dir = opendir(tasks_filepath);

	for (int i = - 2; i < result->num_threads; i++) {
		struct dirent *dir_entry = readdir(tasks_dir);
		if (dir_entry == NULL)
			break;
		if (i < 0)
			continue;

		clear_thread_stats(&result->thread_stats[i]);
		char pid_str[16];
		strncpy(pid_str, dir_entry->d_name, 16);
		int j;
		for (j = 0; pid_str[j] >= '0' && pid_str[j] <= '9'; j++);
			pid_str[j] = '\0';
		result->thread_stats[i].pid = atoi(pid_str);

		char thread_stat_filepath[64];
		strcpy(thread_stat_filepath, tasks_filepath);
		strncat(thread_stat_filepath, pid_str,
			sizeof(thread_stat_filepath)
			- strlen(thread_stat_filepath) - 1);
		strcat(thread_stat_filepath, "/stat");
		FILE *ftstat = fopen(thread_stat_filepath, "rb");
		if (ftstat == NULL) {
			printf("CPUStat: Couldn't open %s.", thread_stat_filepath);
			return -1;
		}
		int64_t rss;
		char s[1024];
		fread(s, 1024, 1, ftstat);
		fflush(stdout);
		int k = 0;
		while (s[k] != '(')
			k++;
		char *name = &s[k];
		while (s[k] != ')')
			k++;
		s[k + 1] = '\0';
		strncpy(result->thread_stats[i].name, name, 32);
		result->thread_stats[i].name[31] = '\0';
		if (sscanf(&s[k + 2],
		     "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
		     "%lu %ld %ld %*d %*d %d %*d %*u %lu %ld",
		     &result->thread_stats[i].utime_ticks,
		     &result->thread_stats[i].stime_ticks,
		     &result->thread_stats[i].cutime_ticks,
		     &result->thread_stats[i].cstime_ticks,
		     &result->num_threads, &result->thread_stats[i].vsize,
		     &rss) == EOF) {
			printf("Error reading %s.\n", thread_stat_filepath);
//			fclose(ftstat);
//			return -1;
		}
		fclose(ftstat);
		result->thread_stats[i].rss = rss * getpagesize();
	}
	closedir(tasks_dir);

	return 0;
}

/*
 * Calculate the elapsed CPU usage between two measuring points, in percent.
 */
static void calc_cpu_usage_pct(const ProcessStat *cur_usage,
			       const ProcessStat *last_usage,
			       double *ucpu_usage, double *scpu_usage,
				double *thread_ucpu_usage, double *thread_scpu_usage)
{
	const uint64_t total_time_diff = cur_usage->cpu_total_time -
	    last_usage->cpu_total_time;

	*ucpu_usage = 100 * (((cur_usage->process_stats.utime_ticks +
			       cur_usage->process_stats.cutime_ticks)
			      - (last_usage->process_stats.utime_ticks +
				 last_usage->process_stats.cutime_ticks))
			     / (double)total_time_diff);

	*scpu_usage =
	    100 *
	    ((((cur_usage->process_stats.stime_ticks +
		cur_usage->process_stats.cstime_ticks)
	       - (last_usage->process_stats.stime_ticks +
		  last_usage->process_stats.cstime_ticks))) /
	     (double)total_time_diff);

	if (thread_info_enabled && (thread_ucpu_usage != NULL || thread_scpu_usage != NULL)) {
		int j = 0;
		for (int i = 0; i < cur_usage->num_threads; i++) {
			fflush(stdout);
			if (j >= last_usage->num_threads ||
			last_usage->thread_stats[j].pid > cur_usage->thread_stats[i].pid) {
				thread_ucpu_usage[i] = - 1.0;
				thread_scpu_usage[i] = - 1.0;
				continue;
			}
			for (;;) {
				fflush(stdout);
				if (cur_usage->thread_stats[i].pid == last_usage->thread_stats[j].pid)
					break;
				j++;
				if (j >= last_usage->num_threads ||
				last_usage->thread_stats[j].pid > cur_usage->thread_stats[i].pid) {
					thread_ucpu_usage[i] = - 1.0;
					thread_scpu_usage[i] = - 1.0;
					goto next;
				}
			}
			fflush(stdout);
			if (thread_ucpu_usage != NULL) {
				thread_ucpu_usage[i] = 100 * ((
				(cur_usage->thread_stats[i].utime_ticks +
			       cur_usage->thread_stats[i].cutime_ticks)
			      - (last_usage->thread_stats[j].utime_ticks +
				 last_usage->thread_stats[j].cutime_ticks))
			     / (double)total_time_diff);
			}
			if (thread_scpu_usage != NULL) {
				thread_scpu_usage[i] = 100 * ((
				(cur_usage->thread_stats[i].stime_ticks +
			       cur_usage->thread_stats[i].cstime_ticks)
			      - (last_usage->thread_stats[j].stime_ticks +
				 last_usage->thread_stats[j].cstime_ticks))
			     / (double)total_time_diff);
			}
			j++;
next:			;
		}
	}
}

// Return the total CPU usage for the process.

static void get_total_usage(const ProcessStat *pstat_current,
double *ucpu_usage, double *scpu_usage, double *thread_ucpu_usage, double *thread_scpu_usage) {
  	ProcessStat process_stat_zero;
	init_pstat(&process_stat_zero);
	clear_thread_stats(&process_stat_zero.process_stats);
	process_stat_zero.num_threads = 0;
	if (thread_info_enabled && (thread_ucpu_usage != NULL || thread_scpu_usage != NULL)) {
		process_stat_zero.thread_stats = new struct thread_stats_t[pstat_current->num_threads];
		process_stat_zero.num_threads = pstat_current->num_threads;
		process_stat_zero.max_thread_stats = pstat_current->num_threads;
		for (int i = 0; i < pstat_current->num_threads; i++)
			clear_thread_stats(&process_stat_zero.thread_stats[i]);
	}
	calc_cpu_usage_pct(pstat_current, &process_stat_zero,
		ucpu_usage, scpu_usage,	thread_ucpu_usage, thread_scpu_usage);
	if (thread_info_enabled && (thread_ucpu_usage != NULL || thread_scpu_usage != NULL))
		free_thread_stats(&process_stat_zero);
}

static double calc_time_taken(ProcessStat *current, ProcessStat *previous) {
	uint64_t total_time_diff = current->cpu_total_time -
		previous->cpu_total_time;
	if (cached_sc_clk_tck == 0)
		cached_sc_clk_tck = sysconf(_SC_CLK_TCK);
	return (double)total_time_diff / cached_sc_clk_tck;
}

CPUStat::CPUStat(int _pid) {
	pid = _pid;
	process_stat = new ProcessStat;
}

CPUStat::~CPUStat() {
	free_thread_stats(process_stat);
	delete process_stat;
}

void CPUStat::Update() {
	ProcessStat *usage = process_stat;
	get_usage(pid, usage);
}

CPUStat *AllocateCPUStat(int pid) {
	CPUStat *st = new CPUStat(pid);
	return st;
}

void CPUStat::GetTotalUsage(const CPUStat *st_previous, double *ucpu_usage, double *scpu_usage,
double *thread_ucpu_usage, double *thread_scpu_usage) const {
	get_total_usage(process_stat, ucpu_usage, scpu_usage, thread_ucpu_usage, thread_scpu_usage);
}

void CPUStat::GetUsageFrom(const CPUStat *st_previous, double *ucpu_usage, double *scpu_usage,
double *thread_ucpu_usage, double *thread_scpu_usage) const {
	calc_cpu_usage_pct(process_stat, st_previous->process_stat,
		ucpu_usage, scpu_usage,	thread_ucpu_usage, thread_scpu_usage);
}

// Calculate the user and system CPU time spent by the process. If either thread_ucpu_usage or
// thread_scpu_usage is not NULL, also calculate CPU time stats for all threads of the process.
// The results are stored as doubles at the double pointers provided by the arguments.

void CalculateCPUUsage(const CPUStat *st_current, const CPUStat *st_previous,
double *ucpu_usage, double *scpu_usage,	double *thread_ucpu_usage, double *thread_scpu_usage)
{
	st_current->GetUsageFrom(st_previous, ucpu_usage, scpu_usage,
		thread_ucpu_usage, thread_scpu_usage);
}

