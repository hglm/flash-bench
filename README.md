flash-bench is a disk and file system benchmark for testing various access
patterns, such as sequential read or write access, random read or write
access, and access patterns stored in a trace. Although specifically
relevant for flash memory-based storage such as SSD drives, USB sticks,
SD cards and other memory cards, it also works with traditional hard-disk
drives, RAM disk or any kind of file system that allows read or write access
to a single file, or is represented as a block device.

It can be used as a storage device, file system or real-world disk access
benchmark, with or without use traces, and with or without the effects of
OS disk caching. It can be used a low-level disk access benchmark when
instructed to minimize OS cache effects or use direct or synchronous access.

The program is best run as superuser, mainly because emptying of the Linux
buffer cache is a priviledged operation. Otherwise, cache effects will
usually skew the results.

It has been developed for Linux and uses the pthreads library.

