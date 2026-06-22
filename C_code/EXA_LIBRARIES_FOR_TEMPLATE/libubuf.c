#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "libubuf.h"
#include "ubuf/ubuf_ioctl.h"
#include <stdio.h>
#define PHYS_MASK 0x3ffffffff

static int fd = -1;


void *ubuf_create(size_t size, int pool)
{
	if (fd < 0) {
		fd = open("/dev/ubuf", O_RDWR | O_SYNC);
		if (fd < 0) {
			return NULL;
		}
	}
	void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, pool*4096);
	if (buf == MAP_FAILED) {
		return NULL;
	}
	return buf;
}


uintptr_t ubuf_get_phys(void *buf)
{
	if (fd < 0 || buf == NULL) {
		return 0;
	}
	uint64_t arg = (uint64_t)buf;
	int ret = ioctl(fd, UBUF_GET_PHYS, &arg);
	if (ret != 0) {
		return 0;
	}
	return arg & PHYS_MASK;
}


size_t ubuf_get_size(void *buf)
{
	if (fd < 0 || buf == NULL) {
		return 0;
	}
	uint64_t arg = (uint64_t)buf;
	int ret = ioctl(fd, UBUF_GET_SIZE, &arg);
	if (ret != 0) {
		return 0;
	}
	return (size_t)arg;
}


int ubuf_destroy(void *buf)
{
	if (buf == NULL) {
		return -EINVAL;
	}
	size_t size = ubuf_get_size(buf);
	return munmap(buf, size);
}


int ubuf_sync(void)
{
	if (fd < 0) {
		return -EINVAL;
	}
	return ioctl(fd, UBUF_SYNC, NULL);
}
