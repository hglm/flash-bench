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

// Return current system time/date in cumulative microseconds

inline uint64_t GetCurrentTimeUSec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// Return current system time/date in seconds (double floating point format)

inline double GetCurrentTime() {
    return (double)GetCurrentTimeUSec() * 0.000001d;
}

// Simple time measurement between two moments.

class Timer {
private :
	uint64_t start_time;
public :
	void Start() {
		start_time = GetCurrentTimeUSec();
	}
	uint64_t ElapsedUSec() {
		uint64_t end_time = GetCurrentTimeUSec();
		uint64_t diff_time = end_time - start_time;
		start_time = end_time;
		return diff_time;
	}
	double Elapsed() {
		return (double)ElapsedUSec() * 0.000001d;
	}
};

// Threaded time-out

class ThreadedTimeout {
private :
	bool *stop_signalled;
	uint64_t timeout_period;
	pthread_t thread;
	
	static void *Thread(void *p) {
		ThreadedTimeout *tt = (ThreadedTimeout *)p;
		int secs = tt->timeout_period / 1000000;
		if (secs > 0)
			sleep(secs);
		int usecs = tt->timeout_period % 1000000;
		if (usecs > 0)
			usleep(usecs);
		*tt->stop_signalled = true;
	}
public :
	ThreadedTimeout() {
		stop_signalled = new bool[1];
	}
	~ThreadedTimeout() {
		Stop();
		delete [] stop_signalled;
	}
	void Start(uint64_t timeout_in_usec) {
		timeout_period = timeout_in_usec;
		*stop_signalled = false;
		void * (*thread_func)(void *);
		thread_func = &ThreadedTimeout::Thread;
		pthread_create(&thread, NULL, thread_func, this);
	}
	void Stop() {
		if (!(*stop_signalled))
			pthread_cancel(thread);
		pthread_join(thread, NULL);
	}
	bool StopSignalled() const {
		return *stop_signalled;
	}
};

