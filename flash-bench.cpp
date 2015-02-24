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
#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <stdint.h>
#include <pthread.h>

#include "cpu-stat.h"
#include "dynamic-array.h"
#include "timer.h"

static const struct option long_options[] = {
	// Option name, argument flag, NULL, equivalent short option character.
	{ "block-device", required_argument, NULL, 'b' },
	{ "direct", no_argument, NULL, 'i' },
	{ "duration", required_argument, NULL, 'd' },
	{ "file", required_argument, NULL, 'f' },
	{ "help", no_argument, NULL, 'h' },
	{ "no-duration", no_argument, NULL, 'n' },
	{ "random-seed", required_argument, NULL, 'o' },
	{ "range", required_argument, NULL, 'r' },
	{ "size", required_argument, NULL, 's' },
	{ "sync", no_argument, NULL, 'y' },
	{ "trace-direct", no_argument, NULL, 'v' },
	{ "trace-duration", required_argument, NULL, 'u' },
	{ NULL, 0, NULL, 0 }
};

#define NU_OPTIONS (sizeof(long_options) / sizeof(long_options[0]))

enum { CMD_READ = 0, CMD_WRITE = 1, CMD_SEQUENTIAL = 0, CMD_RANDOM = 2,
	CMD_READ_SEQUENTIAL = CMD_READ | CMD_SEQUENTIAL,
	CMD_WRITE_SEQUENTIAL = CMD_WRITE | CMD_SEQUENTIAL,
	CMD_READ_RANDOM = CMD_READ | CMD_RANDOM,
	CMD_WRITE_RANDOM = CMD_WRITE | CMD_RANDOM,
	CMD_TRACE = 4
};

class Test {
public :
	char command_ch;
	const char *name;
	const char *description;
	char command_flags;
};

static const Test test[] = {
	{ 'r', "seqrd", "Sequential read", CMD_READ | CMD_SEQUENTIAL },
	{ 'w', "seqwr", "Sequential write", CMD_WRITE | CMD_SEQUENTIAL },
	{ 'R', "rndrd", "Random read", CMD_READ | CMD_RANDOM },
	{ 'W', "rndwr", "Random write", CMD_WRITE | CMD_RANDOM },
	{ ' ', "trace", "Trace", CMD_TRACE }
};

#define NU_TESTS (sizeof(test) / sizeof(test[0]))
#define NU_STANDARD_TESTS (NU_TESTS - 1)

static const char *default_test_filename = "flash-bench.tmp";

#define DEFAULT_TEST_FILE_RANGE (512 * 1024 * 1024)

enum {
	FLAG_BLOCK_DEVICE = 0x1,
	FLAG_ACCESS_MODE_DIRECT = 0x2,
	FLAG_TRACE_DURATION = 0x4,
	FLAG_NO_DURATION = 0x8,
	FLAG_RANDOM_SEED = 0x10,
	FLAG_RANDOM_SEED_TIME = 0x20,
	FLAG_TEST_FILE_RANGE = 0x40,
	FLAG_TOTAL_TRANSACTION_SIZE = 0x80,
	FLAG_ACCESS_MODE_SYNC = 0x100,
	FLAG_TRACE_ACCESS_MODE_DIRECT = 0x200
};

static int operating_flags;

enum { VALUE_TYPE_SIZE, VALUE_TYPE_DURATION, VALUE_TYPE_GENERIC };

static int length_type;
static const char *test_filename;
static int64_t test_file_range;
static int64_t total_transaction_size;
static int nu_blocks;	// The maximum total number of 4K block transactions per test.
static uint32_t duration;
static uint32_t trace_duration;
static uint32_t random_seed;
static int extra_mode_access_flags;
static int extra_mode_access_flags_trace;

static char *buffer;
static int *indices;

class Trace {
public :
	uint8_t *data;
	uint64_t size;
};

TightIntArray commands(4);
CharPointerArray trace_filenames(4);
CastDynamicArray <Trace *, void *, PointerArray> traces(4);

static inline void SetFlag(int flag) {
	operating_flags |= flag;
}

static bool FlagIsSet(int flag) {
	return (operating_flags & flag) != 0;
}

static void Message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

static int RoundToMB(int64_t nu_bytes) {
	return (nu_bytes + 512 * 1024 - 1) >> 20;
}

static void Usage() {
	Message("flash-bench v%d.%d\n", VERSION_MAJOR, VERSION_MINOR);
	Message("Usage: flash-bench [OPTIONS] [TESTNAME]|[TESTSHORTHAND] [TESTNAME]...\n\n");

	Message("Options:\n");
	for (int i = 0;; i++) {
		if (long_options[i].name == NULL)
			break;
		const char *value_str = " [VALUE]";
		if (long_options[i].has_arg)
			Message("    -%c%s, --%s%s, --%s=%s\n", long_options[i].val, value_str,
				long_options[i].name, value_str, long_options[i].name, &value_str[1]);
		else
			Message("    -%c, --%s\n", long_options[i].val, long_options[i].name);
	}

	Message("\nAvailable benchmark tests:\n"
		"    Short  Name              Description\n");
	for (int i = 0; i < NU_TESTS; i++)
		Message("      %-3c  %-16s  %s\n", test[i].command_ch, test[i].name, test[i].description);
	Message("           trace=[FILENAME]  Replay a trace file\n");

	Message(" \nExample: flash-bench\n"
		"    Run all tests (sequential read and write, random read and write)\n"
		"    using a default range/size of 512MB, creating a test file of size\n"
		"    512 MB if not already present and a default target duration of 60\n"
		"    seconds per test.\n");

	Message("\nExample: flash-bench --size 128M --range 512M rndrd rndwr\n"
		"    Run random access tests with 128MB worth of data, using\n"
		"    128MB of the default test file, creating it if required.\n"
		"    The default maximum target duration of 60 seconds is enforced.\n");

	Message("\nExample: flash-bench --duration 15s --size 512M rwRW\n"
		"    Run sequential and random access tests (total four tests)\n"
		"    with 512MB worth of data, using %dMB (default range) of the\n"
		"    default test file, with a maximum target duration of 15s.\n",
		RoundToMB(DEFAULT_TEST_FILE_RANGE));
}

static void __attribute__((noreturn)) FatalError(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	fflush(stdout);
	va_end(args);
	exit(1);
}

static int64_t ParseValue(char *arg, int *type) {
	int length = strlen(arg);
	int unit = arg[length - 1];
	bool no_unit;
	if (isdigit(unit))
		no_unit = true;
	else
		no_unit = false;
	if (unit != 'K' && unit != 'M' && unit != 'G' && unit != 's' && unit != 'm')
		FatalError("Expected unit K, M, G (transaction size) or s or m (duration) "
			"for length argument.\n");
	if (length < 2)
		FatalError("Size expected before unit for length argument.\n");
	if (no_unit && length < 1)
		FatalError("No value specified.\n");
	char *s = strdup(arg);
	if (!no_unit)
		s[length - 1] = '\0';
	int64_t size = atoi(s);
	free(s);
	if (size < 1 || size > (1 << 28))
		FatalError("Invalid number specified for size before unit for length argument.\n");
	if (no_unit) {
		*type = VALUE_TYPE_GENERIC;
		return size;
	}
	int t = VALUE_TYPE_SIZE;
	switch (unit) {
	case 'K' : size *= 1024; break;
	case 'M' : size *= 1024 * 1024; break;
	case 'G' : size *= 1024 * 1024 * 1024; break;
	case 'm' : size *= 60;
	case 's' : t = VALUE_TYPE_DURATION; break;
	}
	*type = t;
	return size;
}

static void ParseOptions(int argc, char **argv) {
	operating_flags = 0;
	duration = 60;
	test_filename = default_test_filename;
	int value_type;
	
	while (true) {
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		int c = getopt_long(argc, argv, "b:id:f:hno:r:s:yvu:", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'b' :	// -b, --block-device
			SetFlag(FLAG_BLOCK_DEVICE);
			test_filename = strdup(optarg);
			break;
		case 'i' :	// -i. --direct
			SetFlag(FLAG_ACCESS_MODE_DIRECT);
			break;
		case 'd' :	// -d, --duration
			duration = ParseValue(optarg, &value_type);
			break;
		case 'f' :	// -f, --file
			test_filename = strdup(optarg);
			break;
		case 'h' :	// -h, --help
			Usage();
			exit(0);
		case 'n' :	// -n, --no-duration
			SetFlag(FLAG_NO_DURATION);
			break;
		case 'o' :	// -o, --random-seed
			if (strcmp(optarg, "time") == 0) {
				SetFlag(FLAG_RANDOM_SEED_TIME);
			}
			else {
				SetFlag(FLAG_RANDOM_SEED);
				random_seed = ParseValue(optarg, &value_type);
			}
			break;
		case 'r' :	// -r, --range
			SetFlag(FLAG_TEST_FILE_RANGE);
			test_file_range = ParseValue(optarg, &value_type);
			break;
		case 's' :	// -s, --size
			SetFlag(FLAG_TOTAL_TRANSACTION_SIZE);
			total_transaction_size = ParseValue(optarg, &value_type);
			break;
		case 'y' :	// -y, --sync
			SetFlag(FLAG_ACCESS_MODE_SYNC);
			break;
		case 'v' :	// -v, --trace-direct
			SetFlag(FLAG_TRACE_ACCESS_MODE_DIRECT);
			break;
		case 'u' :	// -u, --trace-duration
			SetFlag(FLAG_TRACE_DURATION);
			trace_duration = ParseValue(optarg, &value_type);
			break;
		default :
			FatalError("");
			break;
		}
	}

	if (optind < argc) {
		for (int i = optind; i < argc; i++) {
			if (strcmp(argv[i], "trace=") == 0) {
				trace_filenames.Add(strdup(&argv[i][6]));
				commands.Add(CMD_TRACE);
				break;
			}
			int t = - 1;
			for (int j = 0; j < NU_STANDARD_TESTS; j++)
				if (strcmp(argv[i], test[j].name) == 0) {
					t = j;
					break;
				}
			if (t >= 0)
				commands.Add(t);
			else {
				// Check for shorthand argument.
				int n = strlen(argv[i]);
				int count = 0;
				for (int k = 0; k < n; k++) {
					int j;
					for (j = 0; j < NU_STANDARD_TESTS; j++)
						if (argv[i][k] == test[j].command_ch) {
							commands.Add(j);
							count++;
							break;
						}
				}
				// If not all characters were recognized as test shorthands,
				// report an error.
				if (count < n)
					FatalError("Unrecognized benchmark test name %s.\n", argv[i]);
			}
		}
	}

	if (commands.Size() == 0) {
		// No test names or traces specified. Perform all standard tests.
		for (int i = 0; i < NU_STANDARD_TESTS; i++)
			commands.Add(i);
	}
}

static void CreateBuffer() {
	buffer = new char[4096];
	for (int i = 0; i < 4096; i++) {
		buffer[i] = i & 0xFF;
	}
}

static void CreateIndices() {
	indices = new int[nu_blocks];
}

static void SetRandomIndices() {
	for (int i = 0; i < nu_blocks; i++)
		indices[i] = i;
	// Traverse the array from start to end and swap indices randomly.
	for (int i = 0; i < nu_blocks; i++) {
		int j = rand() % nu_blocks;
		int index_i = indices[i];
		indices[i] = indices[j];
		indices[j] = index_i;
	}
}

static void DestroyBuffer() {
	delete [] buffer;
}

static const char *empty_environment[] = { NULL };

static void ExecuteSync() {
	// Create a child process
	int child_pid = fork();
	if (child_pid == 0)
		execle("/bin/sync", "/bin/sync", (char *)NULL, empty_environment);
	int status;
	waitpid(child_pid, &status, 0);
}

static const char char_three = '3';

static void DropCaches() {
	ExecuteSync();
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY | O_SYNC);
	int r = write(fd, &char_three, 1);
	close(fd);
	if (r != 1)
		Message("Warning: Cache flushing unsuccesful. Permission problem?\n"
			"flash-bench should be run as superuser for best effects.\n");
}

static void Sync() {
	ExecuteSync();
}

// File I/O wrappers.

static void CheckFDError(int fd) {
	if (fd < 0)
		FatalError("Error opening file.\n");
}

static void read_with_check(int fd, void *buffer, size_t size) {
	ssize_t size_read = read(fd, buffer, size);
	if (size_read != size)
		FatalError("Error during read operation.\n");
}

static void write_with_check(int fd, void *buffer, size_t size) {
	ssize_t size_written = write(fd, buffer, size);
	if (size_written != size)
		FatalError("Error during write operation.\n");
}


static void CreateTestFile() {
	int fd = open(test_filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0)
		FatalError("Error - could not create test file %s (permission problem?).\n",
			test_filename);
	for (int i = 0; i < (test_file_range + 4095) / 4096; i++)
		write_with_check(fd, buffer, 4096);
	close(fd);
}

static void CheckTestFile() {
	if (!FlagIsSet(FLAG_TEST_FILE_RANGE))
		test_file_range = DEFAULT_TEST_FILE_RANGE;
	struct stat sb;
	int r = stat(test_filename, &sb);
	if (FlagIsSet(FLAG_BLOCK_DEVICE)) {
		if (!S_ISBLK(sb.st_mode))
			FatalError("Device file %s does not appear to be a block device.\n",
				test_filename);
		if (sb.st_size < test_file_range)
			FatalError("Block device size is smaller than test file range.\n");
		return;
	}
	bool ready = false;
	if (r != - 1) {
		// Have to check whether the file is a regular filesystem file, not a device.
		if (!S_ISREG(sb.st_mode))
			FatalError("Error: Specified test filename is not a regular file. "
				"Use --block-device option to use a block device for disk access.\n");
		// Check whether the file is large enough.
		if (sb.st_size >= test_file_range) {
			ready = true;
			if (!FlagIsSet(FLAG_TEST_FILE_RANGE)) {
				// If no test file range was specified, and the test file is at least
				// larger than the standard size of 512 MB, use a range covering the
				// whole file.
				test_file_range = sb.st_size;
				Message("Using test file %s (using all %dMB).\n", test_filename,
					RoundToMB(test_file_range));
			}
			else
				Message("Reusing test file %s (using %dMB of %dMB).\n",
					test_filename, RoundToMB(test_file_range), RoundToMB(sb.st_size));
		}
		else
			Message("Existing test file smaller than %dMB, creating new file.\n",
				RoundToMB(test_file_range));
	}
	if (!ready) {
		Message("Creating test file %s of size %dMB.\n", test_filename, RoundToMB(test_file_range));
		CreateTestFile();
	}
}

static void PrepareTraces() {
//	traces.Init();
	for (int i = 0; i < trace_filenames.Size(); i++) {
		char *filename = trace_filenames.Get(i);
		struct stat sb;
		int r = stat(filename, &sb);
		// Load the trace data exactly as it is stored in the trace file.
		Message("Loading trace file %s (%dMB).\n", RoundToMB(sb.st_size));
		uint8_t *tracep = new uint8_t[sb.st_size];
		FILE *f = fopen(filename, "rb");
		if (f = NULL)
			FatalError("Could not open trace file %s.\n", filename);
		ssize_t size = fread(tracep, 1, sb.st_size, f);
		if (size < sb.st_size)
			FatalError("Error loading trace file %s.\n", filename);
		fclose(f);
		Trace *trace = new Trace;
		trace->data = tracep;
		trace->size = size;
		traces.Add(trace);
	}
}

// Duration-limited tests.

static int SequentialRead(ThreadedTimeout *tt) {
	int fd = open(test_filename, O_RDONLY | extra_mode_access_flags);
	CheckFDError(fd);
	int blocks_processed = 0;
	for (int i = 0; i < nu_blocks; i++) {
		read_with_check(fd, buffer, 4096);
		blocks_processed++;
		if (tt->StopSignalled())
			break;
	}
	close(fd);
	return blocks_processed;
}

static int SequentialWrite(ThreadedTimeout *tt) {
	int fd = open(test_filename, O_WRONLY | extra_mode_access_flags);
	CheckFDError(fd);
	int blocks_processed = 0;
	for (int i = 0; i < nu_blocks; i++) {
		write_with_check(fd, buffer, 4096);
		blocks_processed++;
		if (tt->StopSignalled())
			break;
	}
	close(fd);
	return blocks_processed;
}

static int RandomRead(ThreadedTimeout *tt) {
	int fd = open(test_filename, O_RDONLY | extra_mode_access_flags);
	CheckFDError(fd);
	int blocks_processed = 0;
	for (int i = 0; i < nu_blocks; i++) {
		int block_index = indices[i];
		lseek(fd, block_index * 4096, SEEK_SET);
		read_with_check(fd, buffer, 4096);
		blocks_processed++;
		if (tt->StopSignalled())
			break;
	}
	close(fd);
	return blocks_processed;
}

static int RandomWrite(ThreadedTimeout *tt) {
	int fd = open(test_filename, O_WRONLY | extra_mode_access_flags);
	CheckFDError(fd);
	int blocks_processed = 0;
	for (int i = 0; i < nu_blocks; i++) {
		int block_index = indices[i];
		lseek(fd, block_index * 4096, SEEK_SET);
		write_with_check(fd, buffer, 4096);
		blocks_processed++;
		if (tt->StopSignalled())
			break;
	}
	close(fd);
	return blocks_processed;
}

// Tests with a set number of 4K blocks.

static int SequentialRead() {
	int fd = open(test_filename, O_RDONLY | extra_mode_access_flags);
	CheckFDError(fd);
	for (int i = 0; i < nu_blocks; i++) {
		read_with_check(fd, buffer, 4096);
	}
	close(fd);
	return nu_blocks;
}

static int SequentialWrite() {
	int fd = open(test_filename, O_WRONLY | extra_mode_access_flags);
	CheckFDError(fd);
	for (int i = 0; i < nu_blocks; i++)
		write_with_check(fd, buffer, 4096);
	close(fd);
	return nu_blocks;
}

static int RandomRead() {
	int fd = open(test_filename, O_RDONLY | extra_mode_access_flags);
	CheckFDError(fd);
	for (int i = 0; i < nu_blocks; i++) {
		int block_index = indices[i];
		lseek(fd, (off_t)block_index * 4096, SEEK_SET);
		read_with_check(fd, buffer, 4096);
	}
	close(fd);
	return nu_blocks;
}

static int RandomWrite() {
	int fd = open(test_filename, O_WRONLY | extra_mode_access_flags);
	CheckFDError(fd);
	for (int i = 0; i < nu_blocks; i++) {
		int block_index = indices[i];
		lseek(fd, (off_t)block_index * 4096, SEEK_SET);
		write_with_check(fd, buffer, 4096);
	}
	close(fd);
	return nu_blocks;
}

static int ExecuteTrace(Trace *trace, ThreadedTimeout *tt) {
	int trace_bindex = 0;	// Index into trace data in bytes.
	int fd = open(test_filename, O_RDWR | extra_mode_access_flags_trace);
	CheckFDError(fd);
	int nu_blocks_processed = 0;
	uint64_t total_size = 0;
	for (;;) {
		if (trace_bindex >= trace->size)
			break;
		uint32_t first_word = *(uint32_t *)(&trace->data[trace_bindex]);
		uint32_t second_word = *(uint32_t *)(&trace->data[trace_bindex + 4]);
		// Optionally, the transaction may not be aligned at 4KB block boundaries.
		int head_size = 0;
		int tail_size = 0;
		uint64_t size_in_blocks;
		uint64_t location;
		int write_transaction;
		if ((first_word & 0x80000000) == 0) {
			// Format 1: 8 bytes, 4K block units.
			int write_transaction = (first_word & 0x40000000) >> 30;
			int size_in_blocks = first_word & 0x3FFFFFFF;
			location = (uint64_t)second_word * 4096;
			trace_bindex += 8;
		}
		else if ((first_word & 0x40000000) == 0) {
			// Format 2: 8 bytes, location in blocks, size in bytes.
			int write_transaction = (first_word & 0x20000000) >> 29;
			location = (uint64_t)(first_word & 0x1FFFFFFF) * 4096;
			size_in_blocks = second_word / 4096;
			tail_size = second_word - size_in_blocks * 4096;
			trace_bindex += 8;
		}
		else {
			// Format 3: 16 bytes, location and size in bytes.
			uint64_t size = first_word | ((uint64_t)second_word << 32);
			uint32_t third_word = *(uint32_t *)(&trace->data[trace_bindex + 8]);
			uint32_t fourth_word = *(uint32_t *)(&trace->data[trace_bindex + 12]);
			location = third_word | ((uint64_t)fourth_word << 32);
			if ((location & 0xFFF) != 0) {
				head_size = 4096 - (location & 0xFFF);
				if (head_size > size)
					head_size = size;
				size -= head_size;
			}
			size_in_blocks = size / 4096;
			tail_size = size & 0xFFF;
		}
		total_size += size_in_blocks * 4096 + head_size + tail_size;
		lseek(fd, (off_t)location, SEEK_SET);
		// Handle head.
		if (head_size > 0) {
			if (write_transaction)
				write_with_check(fd, buffer, head_size);
			else
				read_with_check(fd, buffer, head_size);
			nu_blocks_processed++;
		}
		// Handle main part (block-aligned).
		for (int i = 0; i < size_in_blocks; i++)
			if (write_transaction)
				write_with_check(fd, buffer, 4096);
			else
				read_with_check(fd, buffer, 4096);
		nu_blocks_processed += size_in_blocks;
		// Handle tail.
		if (tail_size > 0) {
			if (write_transaction)
				write_with_check(fd, buffer, tail_size);
			else
				read_with_check(fd, buffer, tail_size);
			nu_blocks_processed++;
		}
		// When there is a set trace duration, check it.
		if (FlagIsSet(FLAG_TRACE_DURATION))
			if (tt->StopSignalled())
				break;
	}
	close(fd);
	// We can report nu_blocks_processed (which counts every head or tail, even a few bytes,
	// as one 4K block, or use the total transaction size in bytes divided by the block size
	// (which we do).
	return total_size / 4096;
}

int main(int argc, char *argv[]) {
#if 0
	// Running with no arguments should invoke running the default tests
	// with default parameters, not usage information.
	if (argc == 1) {
		Usage();
		exit(0);
	}
#endif
	ParseOptions(argc, argv);

	// Reset the random number generator
	if (FlagIsSet(FLAG_RANDOM_SEED))
		srandom(random_seed);
	else if (FlagIsSet(FLAG_RANDOM_SEED_TIME))
		srandom((uint32_t)(GetCurrentTime() * 1000.0));
	else
		// By default, the random number patttern is deterministic and random access
		// benchmarks are repeatable (same access pattern).
		srandom(0);

	CreateBuffer();
	CheckTestFile();

	// Validate tests (make sure the amount of transactions does not exceed
	// the size of the test file).
	if (FlagIsSet(FLAG_TOTAL_TRANSACTION_SIZE) &&
	total_transaction_size > test_file_range) {
		Message("Adjusting transaction size downward to test file range.\n");
		total_transaction_size = test_file_range;
	}
	// For only duration-limited tests, also set a limit for the total transaction size
	// at the total test file size.
	if (!FlagIsSet(FLAG_TOTAL_TRANSACTION_SIZE) && !FlagIsSet(FLAG_NO_DURATION))
		total_transaction_size = test_file_range;
	nu_blocks = total_transaction_size >> 12;

	// Set file access mode variables.
	int extra_mode_access_flags = 0;
	int extra_mode_access_flags_trace = 0;
	if (FlagIsSet(FLAG_ACCESS_MODE_SYNC)) {
		extra_mode_access_flags = O_SYNC;
		extra_mode_access_flags_trace = O_SYNC;
	}
	if (FlagIsSet(FLAG_ACCESS_MODE_DIRECT)) {
		extra_mode_access_flags |= O_DIRECT;
	}
	if (FlagIsSet(FLAG_TRACE_ACCESS_MODE_DIRECT)) {
		extra_mode_access_flags_trace |= O_DIRECT;
	}

	CreateIndices();
	SetRandomIndices();

	// Prepare traces.
	PrepareTraces();

	int trace_index = 0;
	int pid = getpid();
	CPUStat *cpustat_before = AllocateCPUStat(pid);
	CPUStat *cpustat_after = AllocateCPUStat(pid);
	for (int i = 0; i < commands.Size(); i++) {
		DropCaches();
		int com = commands.Get(i);
		Message("Benchmark: %s", test[com].description);
		// Print limits determining how long the test will be run.
		uint32_t timeout_secs = 0;
		int64_t tr_size;
		if (test[com].command_flags & CMD_TRACE) {
			Message(" %s", trace_filenames.Get(trace_index));
			if (FlagIsSet(FLAG_TRACE_DURATION))
				timeout_secs = trace_duration;
			tr_size = 0;
		}
		else {
			if (!FlagIsSet(FLAG_NO_DURATION))
				timeout_secs = duration;
			tr_size = total_transaction_size;
		}
		if (timeout_secs == 0 && tr_size == 0)
			Message("  No limits");
		else {
			Message("  Limits: ");
			if (tr_size != 0)
				Message("Total size: %dMB", RoundToMB(tr_size));
			if (timeout_secs != 0)
				Message(" Duration: %ds", timeout_secs);
		}
		Message("\n");

		ThreadedTimeout *tt;
		if (timeout_secs > 0) {
			tt = new ThreadedTimeout();
			tt->Start((uint64_t)timeout_secs * 1000000);
		}
		cpustat_before->Update();
		int blocks_processed;
		Timer timer;
		timer.Start();
		if (test[com].command_flags & CMD_TRACE) {
			blocks_processed = ExecuteTrace(traces.Get(trace_index), tt);
			trace_index++;
		}
		else if (FlagIsSet(FLAG_NO_DURATION)) {
			blocks_processed = nu_blocks;
			switch (test[com].command_flags) {
			case CMD_READ_SEQUENTIAL :
				SequentialRead();
				break;
			case CMD_WRITE_SEQUENTIAL :
				SequentialWrite();
				break;
			case CMD_READ_RANDOM :
				RandomRead();
				break;
			case CMD_WRITE_RANDOM :
				RandomWrite();
				break;
			default :
				Message("Benchmark test unimplemented.\n");
				blocks_processed = 0;
				break;
			}
		}
		else {
			switch (test[com].command_flags) {
			case CMD_READ_SEQUENTIAL :
				blocks_processed = SequentialRead(tt);
				break;
			case CMD_WRITE_SEQUENTIAL :
				blocks_processed = SequentialWrite(tt);
				break;
			case CMD_READ_RANDOM :
				blocks_processed = RandomRead(tt);
				break;
			case CMD_WRITE_RANDOM :
				blocks_processed = RandomWrite(tt);
				break;
			default :
				blocks_processed = 0;
				Message("Benchmark test unimplemented.\n");
				break;
			}
		}
		Sync();
		double elapsed_time = timer.Elapsed();
		cpustat_after->Update();
		if (timeout_secs > 0)
			delete tt;
		double ucpu, scpu;
		cpustat_after->GetUsageFrom(cpustat_before, &ucpu, &scpu, NULL, NULL);
		double processed_MB = (double)((int64_t)blocks_processed * 4096) / (1024 * 1024);
		double bandwidth_MB = processed_MB / elapsed_time;
		Message("%.1lfMB processed in %.2lfs (%.2lfMB/s), CPU: user %.2lf%%, sys %.2lf%%\n",
			processed_MB, elapsed_time, bandwidth_MB, ucpu, scpu);
	}

	DestroyBuffer();
}

