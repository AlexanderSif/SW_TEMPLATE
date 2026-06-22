#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "xkrnl_copy.h"
#include "libc2c.h"
#include "libubuf/libubuf.h"

#define ASSERT(condition)                                                   \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "Assertion failed: (%s), libc2c::%s at %s:%d.\n", \
                    #condition, __func__, __FILE__, __LINE__);              \
            abort();                                                        \
        }                                                                   \
    } while (0)


static XKrnl_copy copy;

int c2c_irq_init()
{
	ASSERT(XKrnl_copy_IsReady(&copy));
	XKrnl_copy_InterruptGlobalEnable(&copy);
	XKrnl_copy_InterruptEnable(&copy, 0x1);
	return 0;
}

u32 c2c_irq_status(void)
{
	return XKrnl_copy_InterruptGetStatus(&copy);
}

int c2c_irq_wait(void)
{
	ASSERT(XKrnl_copy_WFI(&copy) == XST_SUCCESS);
	XKrnl_copy_InterruptClear(&copy, 0x1);
	return 0;
}

int c2c_irq_release()
{
	ASSERT(XKrnl_copy_IsReady(&copy));
	XKrnl_copy_InterruptDisable(&copy, 0x1);
	XKrnl_copy_InterruptGlobalDisable(&copy);
	return 0;
}

int c2c_init(void)
{
	int ret = XKrnl_copy_Initialize(&copy, "copy");
	ASSERT(ret == XST_SUCCESS);
	return ret;
}

int c2c_release()
{
	ASSERT(XKrnl_copy_IsReady(&copy));
	ASSERT(XKrnl_copy_Release(&copy) == XST_SUCCESS);
	return 0;
}

int c2c_copy(void *dst, void *src, size_t n)
{
	uintptr_t dst_paddr = ubuf_get_phys(dst);
	ASSERT(dst_paddr);
	uintptr_t src_paddr = ubuf_get_phys(src);
	ASSERT(dst_paddr);
	return c2c_copy_phys(dst_paddr, src_paddr, n);
}

int c2c_copy_async(void *dst, void *src, size_t n)
{
	uintptr_t dst_paddr = ubuf_get_phys(dst);
	ASSERT(dst_paddr);
	uintptr_t src_paddr = ubuf_get_phys(src);
	ASSERT(dst_paddr);
	return c2c_copy_phys_async(dst_paddr, src_paddr, n);
}

int c2c_copy_phys_async(uintptr_t dst_paddr, uintptr_t src_paddr, size_t n)
{
	ASSERT(n <= UINT32_MAX);
	ASSERT(XKrnl_copy_IsReady(&copy));
	ASSERT(XKrnl_copy_IsIdle(&copy));
	XKrnl_copy_Set_X(&copy, (uint64_t)src_paddr);
	XKrnl_copy_Set_Y(&copy, (uint64_t)dst_paddr);
	XKrnl_copy_Set_N(&copy, (uint32_t)((n+8-1)/8));	// ceil(n, sizeof(uint64_t))
	XKrnl_copy_Start(&copy);
	return 0;
}

int c2c_copy_phys(uintptr_t dst_paddr, uintptr_t src_paddr, size_t n)
{
	c2c_copy_phys_async(dst_paddr, src_paddr, n);
	while (!XKrnl_copy_IsDone(&copy));
	return 0;
}