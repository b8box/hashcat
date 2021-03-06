/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#ifndef _STATUS_H
#define _STATUS_H

#include <stdio.h>
#include <inttypes.h>

#define STATUS            0
#define STATUS_TIMER      10
#define MACHINE_READABLE  0

double get_avg_exec_time (hc_device_param_t *device_param, const int last_num_entries);

void status_display_machine_readable (opencl_ctx_t *opencl_ctx, const hashes_t *hashes);
void status_display (opencl_ctx_t *opencl_ctx, const hashconfig_t *hashconfig, const hashes_t *hashes);
void status_benchmark_automate (opencl_ctx_t *opencl_ctx, const hashconfig_t *hashconfig);
void status_benchmark (opencl_ctx_t *opencl_ctx, const hashconfig_t *hashconfig);

#endif // _STATUS_H
