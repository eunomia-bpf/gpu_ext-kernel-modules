/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * GPU Scheduler eBPF Hook Functions - Declarations for src/nvidia
 *
 * These functions are implemented in kernel-open/nvidia/nv-gpu-sched-hooks.c
 * and are traceable with kprobe/bpftrace.
 *
 * ============================================================================
 * Hook Overview
 * ============================================================================
 *
 * These hooks are TSG (Time-Slice Group / Channel Group) lifecycle events,
 * NOT per-kernel-launch events. They are called during:
 *
 *   1. nv_gpu_sched_task_init     - TSG creation (once per TSG)
 *   2. nv_gpu_sched_bind          - TSG bind to hardware runlist (once per TSG)
 *   3. nv_gpu_sched_token_request - Work submit token request (for sync, via ioctl)
 *   4. nv_gpu_sched_task_destroy  - TSG destruction (once per TSG)
 *
 * Note: GPU kernel launches go directly through GPFIFO and do NOT trigger
 * these hooks. These are control-plane events, not data-plane events.
 */

#ifndef _NV_GPU_SCHED_HOOKS_H_
#define _NV_GPU_SCHED_HOOKS_H_

#include "nvtypes.h"

/*
 * ============================================================================
 * Context Structures
 * ============================================================================
 *
 * These use NvU64/NvU32 to match the NVIDIA driver conventions.
 * They must match the layout of the structures in kernel-open/nvidia/nv-gpu-sched-hooks.h
 */

/*
 * Hook 1: task_init context - TSG creation
 *
 * Called from: kchangrpInit_IMPL() in kernel_channel_group.c
 * Timing: Once when a TSG (channel group) is created
 *
 * This is the point where scheduling parameters are initially set.
 * BPF can modify timeslice and interleave_level to customize scheduling.
 */
struct nv_gpu_task_init_ctx {
    NvU64 tsg_id;               /* TSG ID (grpID) */
    NvU32 engine_type;          /* Engine type (GRAPHICS=0, COPY=1, NVDEC=2, NVENC=3, NVJPEG=4) */
    NvU64 default_timeslice;    /* Default timeslice in microseconds */
    NvU32 default_interleave;   /* Default interleave level (LOW=1, MEDIUM=2, HIGH=3) */
    NvU32 runlist_id;           /* Runlist ID */
    NvU64 timeslice;            /* Output: New timeslice (0 = no change) */
    NvU32 interleave_level;     /* Output: New interleave level (0 = no change) */
};

/*
 * Hook 2: bind context - TSG bind to hardware runlist
 *
 * Called from: kchangrpapiCtrlCmdGpFifoSchedule_IMPL() in kernel_channel_group_api.c
 * Timing: Once after objects are allocated on channels, or after NVA06C_CTRL_CMD_BIND
 *
 * This is a ONE-TIME operation that binds a TSG into the hardware's scheduling runlist.
 * After this, kernel launches go directly through GPFIFO without calling this hook.
 *
 * BPF can set allow=0 to reject binding (returns NV_ERR_BUSY_RETRY).
 * This is useful for admission control (e.g., limiting which processes can use GPU),
 * but NOT for per-kernel rate limiting.
 */
struct nv_gpu_bind_ctx {
    NvU64 tsg_id;               /* TSG ID */
    NvU32 runlist_id;           /* Runlist ID */
    NvU32 channel_count;        /* Number of channels in TSG */
    NvU64 timeslice_us;         /* Current timeslice */
    NvU32 interleave_level;     /* Current interleave level */
    NvU32 allow;                /* Output: 1 = allow, 0 = reject (NV_ERR_BUSY_RETRY) */
};

/*
 * Hook 3: token_request context - Work submit token request
 *
 * Called from: kchannelNotifyWorkSubmitToken_IMPL() in kernel_channel.c
 * Timing: When userspace requests a work submit token via ioctl
 *         (NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN)
 *
 * Work Submit Token:
 *   An opaque 32-bit token used to write to the doorbell register to finish
 *   submitting work. Userspace calls this ioctl to get the token, then writes
 *   it to the doorbell to notify the GPU that new work is ready in GPFIFO.
 *
 * This is NOT called on every kernel launch. It is called when userspace
 * explicitly requests a token via ioctl. The actual work submission happens
 * when userspace writes this token to the doorbell register (bypassing kernel).
 *
 * Useful for tracking when userspace sets up doorbell-based submission.
 */
struct nv_gpu_token_request_ctx {
    NvU32 channel_id;           /* Channel ID */
    NvU64 tsg_id;               /* TSG ID */
    NvU32 token;                /* Work submit token (for doorbell register) */
};

/*
 * Hook 4: task_destroy context - TSG destruction
 *
 * Called from: kchangrpDestruct_IMPL() in kernel_channel_group.c
 * Timing: Once when a TSG is destroyed
 *
 * Useful for cleanup of BPF map entries associated with the TSG.
 */
struct nv_gpu_task_destroy_ctx {
    NvU64 tsg_id;               /* TSG ID */
};

/*
 * ============================================================================
 * Hook Function Declarations
 * ============================================================================
 *
 * Implemented in kernel-open/nvidia/nv-gpu-sched-hooks.c
 * These are noinline functions traceable with kprobe/bpftrace.
 */

/* Called when a TSG is created */
extern void nv_gpu_sched_task_init(struct nv_gpu_task_init_ctx *ctx);

/* Called when a TSG is bound to hardware runlist (one-time setup) */
extern void nv_gpu_sched_bind(struct nv_gpu_bind_ctx *ctx);

/* Called when userspace requests a work submit token (for sync) */
extern void nv_gpu_sched_token_request(struct nv_gpu_token_request_ctx *ctx);

/* Called when a TSG is destroyed */
extern void nv_gpu_sched_task_destroy(struct nv_gpu_task_destroy_ctx *ctx);

#endif /* _NV_GPU_SCHED_HOOKS_H_ */
