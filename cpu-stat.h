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

class ProcessStat;

class CPUStat {
public :
	int pid;
private :
	ProcessStat *process_stat;
public :
	CPUStat(int pid);
	~CPUStat();
	void Update();
	void GetTotalUsage(const CPUStat *cpust_previous, double *ucpu_usage, double *scpu_usage,
		double *thread_ucpu_usage, double *thread_scpu_usage) const;
	void GetTotalUsage(const CPUStat *cpust_previous, double *ucpu_usage, double *scpu_usage) const {
		GetTotalUsage(cpust_previous, ucpu_usage, scpu_usage, NULL, NULL);
        }
	void GetUsageFrom(const CPUStat *cpust_previous, double *ucpu_usage, double *scpu_usage,
		double *thread_ucpu_usage, double *thread_scpu_usage) const;
	void GetUsageFrom(const CPUStat *cpust_previous, double *ucpu_usage, double *scpu_usage) const {
		GetUsageFrom(cpust_previous, ucpu_usage, scpu_usage, NULL, NULL);
	}
};

CPUStat *AllocateCPUStat(int pid);

void CalculateCPUUsage(const CPUStat *cpust_current, const CPUStat *cpust_previous,
	double *ucpu_usage, double *scpu_usage,	double *thread_ucpu_usage, double *thread_scpu_usage);



