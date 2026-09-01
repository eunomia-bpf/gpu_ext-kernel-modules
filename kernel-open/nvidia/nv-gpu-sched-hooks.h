/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * GPU Scheduler eBPF Hook Functions
 *
 * These functions are compiled via Kbuild and can be traced with kprobe/bpftrace.
 * They serve as hook points for eBPF-based GPU scheduling decisions.
 *
 * This file also provides BPF struct_ops interface for GPU scheduling policy
 * customization, allowing BPF programs to:
 *   - Customize TSG scheduling parameters (timeslice, interleave)
 *   - Implement admission control for GPU scheduling
 *   - Track TSG lifecycle events
 */

#ifndef _NV_GPU_SCHED_HOOKS_H_
#define _NV_GPU_SCHED_HOOKS_H_

#include <linux/types.h>
#include "nv-gpu-transition-validator.h"

/*
 * Context structures for hook functions
 * These mirror the bpf_gpu_*_ctx structures from the design document
 */

/* Hook 2: bind context - TSG bind to hardware runlist */
struct nv_gpu_bind_ctx {
    u64 tsg_id;               /* TSG ID */
    u32 runlist_id;           /* Runlist ID */
    u32 channel_count;        /* Number of channels in TSG */
    u64 timeslice_us;         /* Current timeslice */
    u32 interleave_level;     /* Current interleave level */

    /* Output field */
    u32 allow;                /* 1 = allow, 0 = reject (NV_ERR_BUSY_RETRY) */
};

/* Hook 3: token_request context - Work submit token request (for sync) */
struct nv_gpu_token_request_ctx {
    u32 channel_id;           /* Channel ID */
    u64 tsg_id;               /* TSG ID */
    u32 token;                /* Work submit token */
};

/* Hook 4: task_destroy context - TSG destruction */
struct nv_gpu_task_destroy_ctx {
    u64 tsg_id;               /* TSG ID */
};

/*
 * Hook function declarations
 *
 * These functions are intentionally NOT inlined and are compiled via Kbuild,
 * making them traceable with kprobe/bpftrace.
 *
 * Usage with bpftrace:
 *   sudo bpftrace -e 'kprobe:nv_gpu_sched_task_init {
 *       printf("TSG %llu created, timeslice=%llu\n",
 *              ((struct nv_gpu_task_init_ctx *)arg0)->tsg_id,
 *              ((struct nv_gpu_task_init_ctx *)arg0)->default_timeslice);
 *   }'
 */

/* Hook 1: Called when a TSG (Task/Channel Group) is created */
void nv_gpu_sched_task_init(struct nv_gpu_task_init_ctx *ctx);

/* Hook 2: Called when a TSG is bound to hardware runlist */
void nv_gpu_sched_bind(struct nv_gpu_bind_ctx *ctx);

/* Hook 3: Called when user requests a work submit token (typically for sync) */
void nv_gpu_sched_token_request(struct nv_gpu_token_request_ctx *ctx);

/* Hook 4: Called when a TSG is destroyed */
void nv_gpu_sched_task_destroy(struct nv_gpu_task_destroy_ctx *ctx);

/*
 * ============================================================================
 * BPF struct_ops Interface
 * ============================================================================
 */

/*
 * GPU Scheduler struct_ops definition
 *
 * BPF programs implement these callbacks to customize GPU scheduling behavior.
 * Each callback receives the same context structures as the kprobe hooks.
 */
struct nv_gpu_sched_ops {
    /*
     * on_task_init - Called when a TSG is created
     *
     * @ctx: Task init context with TSG info
     *
     * BPF can request timeslice and interleave changes through the registered
     * kfuncs. Direct context writes are rejected by the verifier.
     *
     * Return: ignored; setter kfunc calls carry decisions
     */
    int (*on_task_init)(struct nv_gpu_task_init_ctx *ctx);

    /*
     * on_bind - Called when a TSG is bound to hardware runlist
     *
     * @ctx: Bind context with TSG info
     *
     * BPF can reject binding through bpf_nv_gpu_reject_bind(). Direct context
     * writes are rejected by the verifier.
     * This is the admission control point.
     *
     * Return: ignored; bpf_nv_gpu_reject_bind() carries rejection
     */
    int (*on_bind)(struct nv_gpu_bind_ctx *ctx);

    /*
     * on_task_destroy - Called when a TSG is destroyed
     *
     * @ctx: Task destroy context with TSG ID
     *
     * BPF can use this for cleanup of any per-TSG state in BPF maps.
     *
     * Return: ignored
     */
    int (*on_task_destroy)(struct nv_gpu_task_destroy_ctx *ctx);
};

/*
 * struct_ops initialization/cleanup functions
 */
int nv_gpu_sched_struct_ops_init(void);
void nv_gpu_sched_struct_ops_exit(void);

#endif /* _NV_GPU_SCHED_HOOKS_H_ */
