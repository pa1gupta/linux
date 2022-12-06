// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Intel Corporation
 *
 * This test caches some chosen MSRs, does a suspend cycle and reports failure
 * if the MSRs are not restored to the values before suspend.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/sysinfo.h>

#include "../kselftest.h"

#define MSR_IA32_SPEC_CTRL		0x00000048
#define MSR_IA32_TSX_CTRL		0x00000122
#define MSR_TSX_FORCE_ABORT		0x0000010F
#define MSR_IA32_MCU_OPT_CTRL		0x00000123
#define MSR_AMD64_LS_CFG		0xc0011020
#define MSR_AMD64_DE_CFG		0xc0011029

struct msr_cache {
	unsigned int msr_id;
	unsigned long long msr_val;
	bool valid;
};

int suspend(void)
{
	struct itimerspec spec = {};
	int power_state_fd;
	int timerfd;
	int ret = -1;

	power_state_fd = open("/sys/power/state", O_RDWR);
	if (power_state_fd < 0) {
		ksft_test_result_error("open(\"/sys/power/state\") failed %s)\n", strerror(errno));
		goto exit;
	}

	timerfd = timerfd_create(CLOCK_BOOTTIME_ALARM, 0);
	if (timerfd < 0) {
		ksft_test_result_error("timerfd_create() failed\n");
		goto cleanup_power_state;
	}

	spec.it_value.tv_sec = 5;
	ret = timerfd_settime(timerfd, 0, &spec, NULL);
	if (ret < 0) {
		ksft_test_result_error("timerfd_settime() failed\n");
		goto cleanup_timer;
	}

	if (write(power_state_fd, "mem", strlen("mem")) != strlen("mem")) {
		ksft_test_result_error("Failed to enter Suspend state\n");
		ret = -1;
		goto cleanup_timer;
	}

	ret = 0;

cleanup_timer:
	close(timerfd);
cleanup_power_state:
	close(power_state_fd);
exit:
	return ret;
}

int msr_read(int cpu, unsigned int msr_id, unsigned long long *msr_val)
{
	char msr_file_name[32];
	int fd, ret;

	snprintf(msr_file_name, 32, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);

	if (fd == -1) {
		perror("Failed to open");
		return -ENODEV;
	}

	ret = pread(fd, msr_val, sizeof(long long), msr_id);
	close(fd);

	return ret != sizeof(long long);
}

int main(void)
{
	/* List of MSRs to tests */
	unsigned int msr_id[] = {
		MSR_IA32_SPEC_CTRL,
		MSR_IA32_TSX_CTRL,
		MSR_TSX_FORCE_ABORT,
		MSR_IA32_MCU_OPT_CTRL,
		MSR_AMD64_LS_CFG,
		MSR_AMD64_DE_CFG,
	};
	int i, cpu, err, max_cpus, msr_idx, total_tests = 0;
	int num_msrs = ARRAY_SIZE(msr_id);
	unsigned long long msr_val;
	cpu_set_t available_cpus;
	struct msr_cache *msrs;
	bool succeeded = true;

	ksft_print_header();

	if (getuid() != 0)
		ksft_exit_fail_msg("Please re-run the test as root\n");

	err = sched_getaffinity(0, sizeof(available_cpus), &available_cpus);
	if (err < 0)
		ksft_exit_fail_msg("sched_getaffinity() failed\n");

	max_cpus = get_nprocs_conf();

	msrs = calloc(sizeof(struct msr_cache), max_cpus * num_msrs);
	if (!msrs)
		ksft_exit_fail_msg("Memory allocation failed\n");

	for (cpu = 0; cpu < max_cpus; cpu++) {
		if (!CPU_ISSET(cpu, &available_cpus))
			continue;
		msr_idx = cpu * num_msrs;
		for (i = 0; i < num_msrs; i++, msr_idx++) {
			if (!msr_read(cpu, msr_id[i], &msr_val)) {
				msrs[msr_idx].valid = true;
				msrs[msr_idx].msr_id = msr_id[i];
				msrs[msr_idx].msr_val = msr_val;
				total_tests++;
			}
		}
	}

	ksft_set_plan(total_tests);

	if (suspend()) {
		succeeded = false;
		goto cleanup;
	}

	for (cpu = 0; cpu < max_cpus; cpu++) {
		if (!CPU_ISSET(cpu, &available_cpus))
			continue;

		msr_idx = cpu * num_msrs;
		for (i = 0; i < num_msrs; i++, msr_idx++) {
			if (!msrs[msr_idx].valid)
				continue;
			if (msr_read(cpu, msr_id[i], &msr_val)) {
				ksft_test_result_fail("Not able to read msr=0x%x on CPU=%d\n",
						      msr_id[i], cpu);
				succeeded = false;
				continue;
			}
			if (msrs[msr_idx].msr_val != msr_val) {
				ksft_test_result_fail("CPU%d: msr=0x%x value after resume=[0x%llx] != suspend=[0x%llx]\n",
						      cpu, msr_id[i], msr_val, msrs[msr_idx].msr_val);
				succeeded = false;
				continue;
			}

			ksft_test_result_pass("CPU%d: MSR[0x%x] restored to 0x%llx\n",
					      cpu, msr_id[i], msr_val);
		}
	}

cleanup:
	free(msrs);

	if (succeeded)
		ksft_exit_pass();
	else
		ksft_exit_fail();
}
