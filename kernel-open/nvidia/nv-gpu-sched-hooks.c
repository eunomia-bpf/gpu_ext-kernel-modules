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
 *
 * Additionally, struct_ops callbacks are invoked for BPF programs that
 * implement the nv_gpu_sched_ops interface.
 */

#include "nv-linux.h"
#include "nv-gpu-sched-hooks.h"

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/bpf_verifier.h>

/* New kernels require trusted kfunc arguments by default and removed this flag. */
#ifndef KF_TRUSTED_ARGS
#define KF_TRUSTED_ARGS 0
#endif

/* Forward declaration - implemented in nv-kernel.o (osapi.c) */
extern NvU32 nv_gpu_sched_do_preempt(nvidia_stack_t *sp, NvU32 hClient, NvU32 hTsg);

/* Compatibility definitions for older kernel versions */
#ifndef BTF_SET8_KFUNCS
#define BTF_SET8_KFUNCS     (1 << 0)
#endif

#ifndef BTF_KFUNCS_START
#define BTF_KFUNCS_START(name) static struct btf_id_set8 __maybe_unused name = { .flags = BTF_SET8_KFUNCS };
#endif

#ifndef BTF_KFUNCS_END
#define BTF_KFUNCS_END(name)
#endif

/*
 * ============================================================================
 * BPF struct_ops Implementation
 * ============================================================================
 */

/*
 * Global instance that BPF programs will implement
 * Protected by RCU for safe concurrent access
 */
static struct nv_gpu_sched_ops __rcu *nv_gpu_sched_bpf_ops;

/*
 * CFI stub functions - required for struct_ops
 */
static int nv_gpu_sched_ops__on_task_init(struct nv_gpu_task_init_ctx *ctx)
{
    return 0;
}

static int nv_gpu_sched_ops__on_bind(struct nv_gpu_bind_ctx *ctx)
{
    return 0;
}

static int nv_gpu_sched_ops__on_task_destroy(struct nv_gpu_task_destroy_ctx *ctx)
{
    return 0;
}

/* CFI stubs structure */
static struct nv_gpu_sched_ops __bpf_ops_nv_gpu_sched_ops = {
    .on_task_init = nv_gpu_sched_ops__on_task_init,
    .on_bind = nv_gpu_sched_ops__on_bind,
    .on_task_destroy = nv_gpu_sched_ops__on_task_destroy,
};

/*
 * kfunc definitions - kernel functions callable from BPF
 */
__bpf_kfunc_start_defs();

/*
 * bpf_nv_gpu_set_timeslice - Set timeslice for a TSG
 */
__bpf_kfunc int bpf_nv_gpu_set_timeslice(struct nv_gpu_task_init_ctx *ctx,
                                         u64 timeslice_us)
{
    struct nv_gpu_task_init_decision_ctx *decision;

    if (!ctx)
        return (int)NV_GPU_TRANSITION_REJECT_IDENTITY;

    decision = container_of(ctx, struct nv_gpu_task_init_decision_ctx, input);
    return (int)nv_gpu_transition_record_u64(&decision->timeslice_request,
                                             timeslice_us);
}

/*
 * bpf_nv_gpu_set_interleave - Set interleave level for a TSG
 */
__bpf_kfunc int bpf_nv_gpu_set_interleave(struct nv_gpu_task_init_ctx *ctx,
                                          u32 interleave_level)
{
    struct nv_gpu_task_init_decision_ctx *decision;

    if (!ctx)
        return (int)NV_GPU_TRANSITION_REJECT_IDENTITY;

    decision = container_of(ctx, struct nv_gpu_task_init_decision_ctx, input);
    return (int)nv_gpu_transition_record_u32(&decision->interleave_request,
                                             interleave_level);
}

/*
 * bpf_nv_gpu_reject_bind - Reject binding for a TSG
 */
__bpf_kfunc void bpf_nv_gpu_reject_bind(struct nv_gpu_bind_ctx *ctx)
{
    if (ctx)
        ctx->allow = 0;
}

/*
 * bpf_nv_gpu_preempt_tsg - Preempt a GPU TSG from BPF context
 *
 * Must be called from sleepable BPF context (e.g., bpf_wq callback).
 * Triggers GPU TSG preemption via RM internal API, bypassing the
 * userspace ioctl path. Enables cross-process preemption.
 */
__bpf_kfunc int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg)
{
    nvidia_stack_t *sp = NULL;
    NvU32 status;

    if (!hClient || !hTsg)
        return -EINVAL;

    if (nv_kmem_cache_alloc_stack(&sp) != 0)
        return -ENOMEM;

    status = nv_gpu_sched_do_preempt(sp, hClient, hTsg);

    nv_kmem_cache_free_stack(sp);

    return (status == 0) ? 0 : -EIO;
}

__bpf_kfunc_end_defs();

/* Define the BTF kfuncs ID set */
BTF_KFUNCS_START(nv_gpu_sched_kfunc_ids_set)
BTF_ID_FLAGS(func, bpf_nv_gpu_set_timeslice, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_nv_gpu_set_interleave, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_nv_gpu_reject_bind, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_nv_gpu_preempt_tsg, KF_SLEEPABLE)
BTF_KFUNCS_END(nv_gpu_sched_kfunc_ids_set)

/* Register the kfunc ID set for struct_ops */
static const struct btf_kfunc_id_set nv_gpu_sched_kfunc_set = {
    .owner = THIS_MODULE,
    .set = &nv_gpu_sched_kfunc_ids_set,
};

/*
 * Separate kfunc set for preempt_tsg - registered for all program types
 * so it can be called from kprobe/tracepoint bpf_wq callbacks.
 */
BTF_KFUNCS_START(nv_gpu_preempt_kfunc_ids_set)
BTF_ID_FLAGS(func, bpf_nv_gpu_preempt_tsg, KF_SLEEPABLE)
BTF_KFUNCS_END(nv_gpu_preempt_kfunc_ids_set)

static const struct btf_kfunc_id_set nv_gpu_preempt_kfunc_set = {
    .owner = THIS_MODULE,
    .set = &nv_gpu_preempt_kfunc_ids_set,
};

/*
 * BTF and verifier callbacks
 */
static int nv_gpu_sched_ops_init_btf(struct btf *btf)
{
    return 0;
}

static bool nv_gpu_sched_ops_is_valid_access(int off, int size,
                                             enum bpf_access_type type,
                                             const struct bpf_prog *prog,
                                             struct bpf_insn_access_aux *info)
{
    if (type == BPF_WRITE)
        return false;

    return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

static int nv_gpu_sched_ops_btf_struct_access(struct bpf_verifier_log *log,
                                              const struct bpf_reg_state *reg,
                                              int off,
                                              int size)
{
    return -EACCES;
}

/*
 * BPF helper: get current process comm (name)
 * Note: bpf_get_current_comm_proto is not exported by kernel, so we implement our own.
 */
BPF_CALL_2(nv_bpf_get_current_comm, char *, buf, u32, size)
{
    struct task_struct *task = current;

    if (unlikely(!task))
        goto err_clear;

    strscpy_pad(buf, task->comm, size);
    return 0;
err_clear:
    memset(buf, 0, size);
    return -EINVAL;
}

static const struct bpf_func_proto nv_bpf_get_current_comm_proto = {
    .func       = nv_bpf_get_current_comm,
    .gpl_only   = false,
    .ret_type   = RET_INTEGER,
    .arg1_type  = ARG_PTR_TO_UNINIT_MEM,
    .arg2_type  = ARG_CONST_SIZE,
};

static const struct bpf_func_proto *
nv_gpu_sched_ops_get_func_proto(enum bpf_func_id func_id,
                                const struct bpf_prog *prog)
{
    const struct bpf_func_proto *proto;

    /* First try base func proto */
    proto = bpf_base_func_proto(func_id, prog);
    if (proto)
        return proto;

    /* Add additional helpers for GPU scheduling */
    switch (func_id) {
    case BPF_FUNC_get_current_comm:
        return &nv_bpf_get_current_comm_proto;
    default:
        return NULL;
    }
}

static const struct bpf_verifier_ops nv_gpu_sched_ops_verifier_ops = {
    .is_valid_access = nv_gpu_sched_ops_is_valid_access,
    .get_func_proto = nv_gpu_sched_ops_get_func_proto,
    .btf_struct_access = nv_gpu_sched_ops_btf_struct_access,
};

static int nv_gpu_sched_ops_init_member(const struct btf_type *t,
                                        const struct btf_member *member,
                                        void *kdata, const void *udata)
{
    return 0;
}

/*
 * Registration function - called when BPF program attaches
 */
static int nv_gpu_sched_ops_reg(void *kdata, struct bpf_link *link)
{
    struct nv_gpu_sched_ops *ops = kdata;

    if (cmpxchg(&nv_gpu_sched_bpf_ops, NULL, ops) != NULL)
        return -EEXIST;

    pr_info("nvidia: GPU sched struct_ops registered\n");
    return 0;
}

/*
 * Unregistration function - called when BPF program detaches
 */
static void nv_gpu_sched_ops_unreg(void *kdata, struct bpf_link *link)
{
    struct nv_gpu_sched_ops *ops = kdata;

    if (cmpxchg(&nv_gpu_sched_bpf_ops, ops, NULL) != ops)
        pr_warn("nvidia: GPU sched struct_ops unexpected unreg\n");
    else
        pr_info("nvidia: GPU sched struct_ops unregistered\n");
}

/* Struct ops definition */
static struct bpf_struct_ops nv_gpu_sched_struct_ops = {
    .verifier_ops = &nv_gpu_sched_ops_verifier_ops,
    .init = nv_gpu_sched_ops_init_btf,
    .init_member = nv_gpu_sched_ops_init_member,
    .reg = nv_gpu_sched_ops_reg,
    .unreg = nv_gpu_sched_ops_unreg,
    .cfi_stubs = &__bpf_ops_nv_gpu_sched_ops,
    .name = "nv_gpu_sched_ops",
    .owner = THIS_MODULE,
};

/*
 * Internal wrapper functions for calling BPF hooks
 */
static void nv_gpu_sched_bpf_on_task_init(struct nv_gpu_task_init_ctx *ctx)
{
    struct nv_gpu_sched_ops *ops;

    rcu_read_lock();
    ops = rcu_dereference(nv_gpu_sched_bpf_ops);
    if (ops && ops->on_task_init)
        ops->on_task_init(ctx);
    rcu_read_unlock();
}

static void nv_gpu_sched_bpf_on_bind(struct nv_gpu_bind_ctx *ctx)
{
    struct nv_gpu_sched_ops *ops;

    rcu_read_lock();
    ops = rcu_dereference(nv_gpu_sched_bpf_ops);
    if (ops && ops->on_bind)
        ops->on_bind(ctx);
    rcu_read_unlock();
}

static void nv_gpu_sched_bpf_on_task_destroy(struct nv_gpu_task_destroy_ctx *ctx)
{
    struct nv_gpu_sched_ops *ops;

    rcu_read_lock();
    ops = rcu_dereference(nv_gpu_sched_bpf_ops);
    if (ops && ops->on_task_destroy)
        ops->on_task_destroy(ctx);
    rcu_read_unlock();
}

/*
 * struct_ops initialization
 */
int nv_gpu_sched_struct_ops_init(void)
{
    int ret;

    ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &nv_gpu_sched_kfunc_set);
    if (ret) {
        pr_err("nvidia: Failed to register GPU sched kfunc ID set: %d\n", ret);
        return ret;
    }

    ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_UNSPEC, &nv_gpu_preempt_kfunc_set);
    if (ret) {
        pr_err("nvidia: Failed to register GPU preempt kfunc ID set: %d\n", ret);
        return ret;
    }

    ret = register_bpf_struct_ops(&nv_gpu_sched_struct_ops, nv_gpu_sched_ops);
    if (ret) {
        pr_err("nvidia: Failed to register GPU sched struct_ops: %d\n", ret);
        return ret;
    }

    pr_info("nvidia: GPU sched struct_ops initialized\n");
    return 0;
}
EXPORT_SYMBOL(nv_gpu_sched_struct_ops_init);

void nv_gpu_sched_struct_ops_exit(void)
{
    pr_info("nvidia: GPU sched struct_ops cleaned up\n");
}
EXPORT_SYMBOL(nv_gpu_sched_struct_ops_exit);

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
 * Called from: kchangrpapiConstruct_IMPL after native scheduling defaults
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
     * The actual work is done by eBPF programs attached via kprobe
     * or struct_ops.
     *
     * The barrier ensures:
     * 1. The function is not optimized away
     * 2. The ctx pointer access is not reordered
     */
    if (ctx) {
        /* Call struct_ops hook if registered */
        nv_gpu_sched_bpf_on_task_init(ctx);
        NV_SCHED_HOOK_BARRIER();
    }
}

/*
 * Hook 2: nv_gpu_sched_bind
 *
 * Called when a TSG is being bound to the hardware runlist.
 * This is a ONE-TIME operation for admission control where eBPF can:
 *   - Accept or reject the bind request
 *   - Implement access control policies
 *   - Track TSG binding events
 *
 * Called from: kchangrpapiCtrlCmdGpFifoSchedule_IMPL (kernel_channel_group_api.c)
 *
 * Example bpftrace usage:
 *   kprobe:nv_gpu_sched_bind {
 *       $ctx = (struct nv_gpu_bind_ctx *)arg0;
 *       printf("TSG %llu bind: channels=%u timeslice=%llu\n",
 *              $ctx->tsg_id, $ctx->channel_count, $ctx->timeslice_us);
 *   }
 */
noinline void nv_gpu_sched_bind(struct nv_gpu_bind_ctx *ctx)
{
    if (ctx) {
        /* Default: allow binding */
        ctx->allow = 1;
        /* Call struct_ops hook if registered - may modify allow */
        nv_gpu_sched_bpf_on_bind(ctx);
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
        /* Call struct_ops hook if registered */
        nv_gpu_sched_bpf_on_task_destroy(ctx);
        NV_SCHED_HOOK_BARRIER();
    }
}

/*
 * Export symbols so they can be called from the nvidia core (nv-kernel.o)
 */
EXPORT_SYMBOL(nv_gpu_sched_task_init);
EXPORT_SYMBOL(nv_gpu_sched_bind);
EXPORT_SYMBOL(nv_gpu_sched_token_request);
EXPORT_SYMBOL(nv_gpu_sched_task_destroy);
