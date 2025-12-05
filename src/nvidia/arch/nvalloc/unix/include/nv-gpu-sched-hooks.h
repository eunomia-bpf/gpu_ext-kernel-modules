/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * GPU Scheduler eBPF Hook Functions - Declarations for src/nvidia
 *
 * These functions are implemented in kernel-open/nvidia/nv-gpu-sched-hooks.c
 * and are traceable with kprobe/bpftrace.
 */

#ifndef _NV_GPU_SCHED_HOOKS_H_
#define _NV_GPU_SCHED_HOOKS_H_

#include "nvtypes.h"

/*
 * Context structures for hook functions
 * These use NvU64/NvU32 to match the NVIDIA driver conventions.
 * They must match the layout of the structures in kernel-open/nvidia/nv-gpu-sched-hooks.h
 */

/* Hook 1: task_init context - TSG creation */
struct nv_gpu_task_init_ctx {
    NvU64 tsg_id;               /* TSG ID (grpID) */
    NvU32 engine_type;          /* Engine type */
    NvU64 default_timeslice;    /* Default timeslice in microseconds */
    NvU32 default_interleave;   /* Default interleave level */
    NvU32 runlist_id;           /* Runlist ID */
    NvU64 timeslice;            /* Output: New timeslice (0 = no change) */
    NvU32 interleave_level;     /* Output: New interleave level (0 = no change) */
};

/* Hook 2: schedule context - Task scheduling */
struct nv_gpu_schedule_ctx {
    NvU64 tsg_id;               /* TSG ID */
    NvU32 runlist_id;           /* Runlist ID */
    NvU32 channel_count;        /* Number of channels in TSG */
    NvU64 timeslice_us;         /* Current timeslice */
    NvU32 interleave_level;     /* Current interleave level */
    NvU32 allow_schedule;       /* Output: 1 = allow, 0 = reject */
};

/* Hook 3: token_request context - Work submit token request (for sync) */
struct nv_gpu_token_request_ctx {
    NvU32 channel_id;           /* Channel ID */
    NvU64 tsg_id;               /* TSG ID */
    NvU32 token;                /* Work submit token */
};

/* Hook 4: task_destroy context - TSG destruction */
struct nv_gpu_task_destroy_ctx {
    NvU64 tsg_id;               /* TSG ID */
};

/*
 * Hook function declarations
 * Implemented in kernel-open/nvidia/nv-gpu-sched-hooks.c
 */
extern void nv_gpu_sched_task_init(struct nv_gpu_task_init_ctx *ctx);
extern void nv_gpu_sched_schedule(struct nv_gpu_schedule_ctx *ctx);
extern void nv_gpu_sched_token_request(struct nv_gpu_token_request_ctx *ctx);
extern void nv_gpu_sched_task_destroy(struct nv_gpu_task_destroy_ctx *ctx);

#endif /* _NV_GPU_SCHED_HOOKS_H_ */
