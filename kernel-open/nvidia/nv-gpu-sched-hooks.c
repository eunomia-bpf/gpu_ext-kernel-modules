/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * GPU Scheduler eBPF Hook Functions Implementation
 *
 * These functions are compiled via Kbuild and can be traced with kprobe/bpftrace.
 * They are intentionally simple (noinline, no optimization) to ensure they
 * remain as stable kprobe attachment points.
 *
 * The actual scheduling logic is implemented in eBPF programs that attach
 * to these kprobes and can read/modify the context structures.
 */

#include "nv-linux.h"
#include "nv-gpu-sched-hooks.h"

/*
 * Memory barrier to prevent compiler from reordering or eliminating
 * the function body. This ensures the function is not optimized away.
 */
#define NV_SCHED_HOOK_BARRIER() barrier()

/*
 * Hook 1: nv_gpu_sched_task_init
 *
 * Called when a TSG (Task/Channel Group) is being created.
 * This is the ideal point for eBPF to make scheduling decisions:
 *   - Set custom timeslice based on task type
 *   - Set interleave level (LOW/MEDIUM/HIGH)
 *   - Record task creation in eBPF maps
 *
 * Called from: kchangrpInit_IMPL (kernel_channel_group.c)
 *
 * Example bpftrace usage:
 *   kprobe:nv_gpu_sched_task_init {
 *       $ctx = (struct nv_gpu_task_init_ctx *)arg0;
 *       printf("TSG %llu init: engine=%u timeslice=%llu runlist=%u\n",
 *              $ctx->tsg_id, $ctx->engine_type,
 *              $ctx->default_timeslice, $ctx->runlist_id);
 *   }
 */
noinline void nv_gpu_sched_task_init(struct nv_gpu_task_init_ctx *ctx)
{
    /*
     * This function body is intentionally minimal.
     * The actual work is done by eBPF programs attached via kprobe.
     *
     * The barrier ensures:
     * 1. The function is not optimized away
     * 2. The ctx pointer access is not reordered
     */
    if (ctx) {
        NV_SCHED_HOOK_BARRIER();
    }
}

/*
 * Hook 2: nv_gpu_sched_schedule
 *
 * Called when a TSG is about to be scheduled (enabled for execution).
 * This is the admission control point where eBPF can:
 *   - Accept or reject the scheduling request
 *   - Implement rate limiting or quotas
 *   - Track scheduling events
 *
 * Called from: kchangrpapiCtrlCmdGpFifoSchedule_IMPL (kernel_channel_group_api.c)
 *
 * Example bpftrace usage:
 *   kprobe:nv_gpu_sched_schedule {
 *       $ctx = (struct nv_gpu_schedule_ctx *)arg0;
 *       printf("TSG %llu schedule: channels=%u timeslice=%llu\n",
 *              $ctx->tsg_id, $ctx->channel_count, $ctx->timeslice_us);
 *   }
 */
noinline void nv_gpu_sched_schedule(struct nv_gpu_schedule_ctx *ctx)
{
    if (ctx) {
        /* Default: allow scheduling */
        ctx->allow_schedule = 1;
        NV_SCHED_HOOK_BARRIER();
    }
}

/*
 * Hook 3: nv_gpu_sched_token_request
 *
 * Called when user requests a work submit token via ioctl.
 * This is NOT called on every kernel launch - only when userspace
 * explicitly requests a token (typically for synchronization).
 *
 * Triggered by: NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN ioctl
 * Called from: kchannelNotifyWorkSubmitToken_IMPL (kernel_channel.c)
 *
 * This is useful for:
 *   - Tracking synchronization points
 *   - Monitoring when userspace needs completion notification
 *   - Understanding sync patterns in GPU workloads
 *
 * Example bpftrace usage:
 *   kprobe:nv_gpu_sched_token_request {
 *       $ctx = (struct nv_gpu_token_request_ctx *)arg0;
 *       printf("Token request: channel=%u TSG=%llu token=%u\n",
 *              $ctx->channel_id, $ctx->tsg_id, $ctx->token);
 *   }
 */
noinline void nv_gpu_sched_token_request(struct nv_gpu_token_request_ctx *ctx)
{
    if (ctx) {
        NV_SCHED_HOOK_BARRIER();
    }
}

/*
 * Hook 4: nv_gpu_sched_task_destroy
 *
 * Called when a TSG is being destroyed.
 * This is useful for:
 *   - Cleaning up eBPF map entries for the task
 *   - Recording task lifetime statistics
 *   - Releasing any resources allocated in eBPF
 *
 * Called from: kchangrpDestruct_IMPL (kernel_channel_group.c)
 *
 * Example bpftrace usage:
 *   kprobe:nv_gpu_sched_task_destroy {
 *       $ctx = (struct nv_gpu_task_destroy_ctx *)arg0;
 *       printf("TSG %llu destroyed, total_submissions=%llu\n",
 *              $ctx->tsg_id, $ctx->total_submissions);
 *   }
 */
noinline void nv_gpu_sched_task_destroy(struct nv_gpu_task_destroy_ctx *ctx)
{
    if (ctx) {
        NV_SCHED_HOOK_BARRIER();
    }
}

/*
 * Export symbols so they can be called from the nvidia core (nv-kernel.o)
 */
EXPORT_SYMBOL(nv_gpu_sched_task_init);
EXPORT_SYMBOL(nv_gpu_sched_schedule);
EXPORT_SYMBOL(nv_gpu_sched_token_request);
EXPORT_SYMBOL(nv_gpu_sched_task_destroy);
