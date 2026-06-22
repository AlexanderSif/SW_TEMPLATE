#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include "xkrnl_vadd.h"
#include "libubuf/libubuf.h"
#include "libc2c/libc2c.h"

enum buf_label {
	LBUF_IN1 = 0,
	LBUF_IN2,
	LBUF_OUT,
	RBUF_IN1,
	RBUF_IN2,
	RBUF_OUT,
	BUF_COUNT,		// total number of buffers
};

// each element is 4 bytes wide
typedef uint32_t elem_t;
// vector length in number of elements and its size in bytes
#define BUF_LEN (1024*1024)
#define BUF_SZ (BUF_LEN*sizeof(elem_t))

int main(int argc, char *argv[])
{
	struct timeval t0, t1;
	double dt, dt_sum;
	int ret, errors;
	elem_t c, c1, c2;

	// generate initial vector words and their sum
	srand(time(NULL));
	c1 = random() & 0x7f;
	c2 = random() & 0x7f;
	elem_t correct = (c1+c2) | (c1+c2)<<8 | (c1+c2)<<16 | (c1+c2)<<24;
	//printf("Debug: c1: %x, c2: %x, correct: %08x\n", c1, c2, correct);
	// initialize buffers
	elem_t *buf[BUF_COUNT];		// pointers to buffers, see enum buf_label
	uint64_t buf_phys[BUF_COUNT];	// physical address of the above
	printf("Initializing buffers: Two sets (HyperRAM and DDR) of 3 x %.1fMi int32 words, total size: %.1f MiB...", BUF_LEN/1048576.0, 6*BUF_SZ/1048576.0);
	fflush(stdout);
	gettimeofday(&t0, NULL);
	for (int i = 0; i < BUF_COUNT; ++i) {
		buf[i] = ubuf_create(BUF_LEN*sizeof(elem_t), i >= 3);
		assert(buf[i] != NULL);
		buf_phys[i] = ubuf_get_phys(buf[i]);
		assert(buf_phys[i] != 0);
		c = 0;
		if (i == LBUF_IN1)
			c = c1;
		if (i == LBUF_IN2)
			c = c2;
		if (i < 3)
			memset(buf[i], c, BUF_SZ);
	}
	ret = ubuf_sync();
	assert(ret == 0);
	gettimeofday(&t1, NULL);
	dt = (t1.tv_sec - t0.tv_sec)*1000.0 + (t1.tv_usec - t0.tv_usec)/1000.0;
	printf(" done. Time: %6.2fms\n", dt);

	printf("Initializing IP cores...\n");
	// initialize copy (no irq)
	c2c_init();

	XKrnl_vadd vadd;
	ret = XKrnl_vadd_Initialize(&vadd, "vadd");
	assert(ret == XST_SUCCESS);

	printf("Scenario 1: Perform vector add on HyperRAM memory.\n");
	for (int len = BUF_LEN; len <= BUF_LEN; len*=2) {
		//printf("\t*** Vector length N = %dMi\n", len >> 20);
		printf("\tExecuting vector add...");
		gettimeofday(&t0, NULL);
		XKrnl_vadd_Set_in1(&vadd, buf_phys[LBUF_IN1]);
		XKrnl_vadd_Set_in2(&vadd, buf_phys[LBUF_IN2]);
		XKrnl_vadd_Set_out_r(&vadd, buf_phys[LBUF_OUT]);
		XKrnl_vadd_Set_N(&vadd, len);
		XKrnl_vadd_Start(&vadd);
		while(XKrnl_vadd_IsDone(&vadd) == 0);
		ret = ubuf_sync();
		assert(ret == 0);
		gettimeofday(&t1, NULL);
		dt = (t1.tv_sec - t0.tv_sec)*1000.0 + (t1.tv_usec - t0.tv_usec)/1000.0;
		printf(" done. Time: %.1fms, compute throughput: %.1fMop/s\n", dt, 1e-3*len/dt);

		printf("\tValidating output vector on HyperRAM...");
		errors = 0;
		for (int i = 0; i < len; ++i) {
			errors += (correct != buf[LBUF_OUT][i]);
		}
		printf(" done. Errors: %d of %d (%.2f%%)\n", errors, len, 100.0*errors/len);
		memset(buf[LBUF_OUT], 0, BUF_SZ);
	}

	printf("Scenario 2: Perform vector add on FPGA DDR memory.\n");
	for (int len = BUF_LEN; len <= BUF_LEN; len*=2) {
		//printf("\t*** Vector length N = %dMi\n", len >> 20);
		printf("\tCopying input from HyperRAM to FPGA DDR...");
		gettimeofday(&t0, NULL);
		c2c_copy(buf[RBUF_IN1], buf[LBUF_IN1], BUF_SZ);
		c2c_copy(buf[RBUF_IN2], buf[LBUF_IN2], BUF_SZ);
		gettimeofday(&t1, NULL);
		dt = (t1.tv_sec - t0.tv_sec)*1000.0 + (t1.tv_usec - t0.tv_usec)/1000.0;
		dt_sum = dt;
		printf(" done. Time: %.1fms, data transfer throughput: %.1fMB/s\n", dt, 2e-3*BUF_SZ/dt);

		printf("\tExecuting vector add on FPGA DDR...");
		gettimeofday(&t0, NULL);
		XKrnl_vadd_Set_in1(&vadd, buf_phys[RBUF_IN1] - 0x180000000);
		XKrnl_vadd_Set_in2(&vadd, buf_phys[RBUF_IN2] - 0x180000000);
		XKrnl_vadd_Set_out_r(&vadd, buf_phys[RBUF_OUT] - 0x180000000);
		XKrnl_vadd_Set_N(&vadd, len);
		XKrnl_vadd_Start(&vadd);
		while(XKrnl_vadd_IsDone(&vadd) == 0);
		ret = ubuf_sync();
		assert(ret == 0);
		gettimeofday(&t1, NULL);
		dt = (t1.tv_sec - t0.tv_sec)*1000.0 + (t1.tv_usec - t0.tv_usec)/1000.0;
		dt_sum += dt;
		printf(" done. Time: %.1fms, compute throughput: %.1fMop/s\n", dt, 1e-3*len/dt);

		printf("\tCopying back output from FPGA DDR to HyperRAM...");
		gettimeofday(&t0, NULL);
		c2c_copy(buf[LBUF_OUT], buf[RBUF_OUT], BUF_SZ);
		ret = ubuf_sync();
		assert(ret == 0);
		gettimeofday(&t1, NULL);
		dt = (t1.tv_sec - t0.tv_sec)*1000.0 + (t1.tv_usec - t0.tv_usec)/1000.0;
		dt_sum += dt;
		printf(" done. Time: %.1fms, data transfer throughput: %.1fMB/s\n", dt, 1e-3*BUF_SZ/dt);
		printf("\tEffective (end-to-end) throughput: %.1fMop/s\n", 1e-3*len/dt_sum);

		//printf("\tValidating output on DDR memory...");
		//gettimeofday(&t0, NULL);
		//errors = 0;
		//for (int i = 0; i < len; ++i) {
		//	errors += (correct != buf[RBUF_OUT][i]);
		//}
		//gettimeofday(&t1, NULL);
		//dt = (t1.tv_sec - t0.tv_sec)*1000.0 + (t1.tv_usec - t0.tv_usec)/1000.0;
		//printf(" done. Time: %.1fms, throughput: %.1fMB/s, errors: %d of %d (%.2f%%)\n",
		//	dt, BUF_SZ/1048576.0/(1e-3*dt), errors, len, 100.0*errors/len);

		printf("\tValidating output vector on HyperRAM...");
		int errors = 0;
		for (int i = 0; i < len; ++i) {
			errors += (correct != buf[LBUF_OUT][i]);
		}
		printf(" done. Errors: %d of %d (%.2f%%)\n", errors, len, 100.0*errors/len);
		memset(buf[LBUF_OUT], 0, BUF_SZ);
		memset(buf[RBUF_OUT], 0, BUF_SZ);
	}

	ret = XKrnl_vadd_Release(&vadd);
	assert(ret == XST_SUCCESS);

	for (int i = 0; i < BUF_COUNT; ++i) {
		ret = ubuf_destroy(buf[i]);
		assert(ret == 0);
	}
	return 0;
}