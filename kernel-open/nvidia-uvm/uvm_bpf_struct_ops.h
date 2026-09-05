#ifndef _UVM_BPF_STRUCT_OPS_H
#define _UVM_BPF_STRUCT_OPS_H

#include "uvm_va_block_types.h"
#include "uvm_perf_prefetch.h"
#include "uvm_pmm_gpu.h"
#include "nv-gpu-transition-validator.h"
#include "uvm_stale_state_v1.h"

typedef struct uvm_bpf_prefetch_decision
{
    nv_gpu_prefetch_decision_t request;
} uvm_bpf_prefetch_decision_t;

enum uvm_bpf_prefetch_diagnostic_phase {
    UVM_BPF_PREFETCH_DIAG_SELECTED = 1,
    UVM_BPF_PREFETCH_DIAG_FINISHED = 2,
};

/* Driver-owned copies for a privileged, read-only tracing observer.
 * Output fields are valid only at FINISHED. */
struct uvm_bpf_prefetch_diagnostic_ctx {
    NvS64 raw_action;
    NvU64 requested_first;
    NvU64 requested_outer;
    NvU64 max_first;
    NvU64 max_outer;
    NvU64 output_first;
    NvU64 output_outer;
    NvU32 phase;
    NvU32 request_attempted;
    NvU32 request_conflict;
    NvU32 initial_region_result;
    NvU32 initial_effect;
    NvU32 native_iterations;
    NvU32 native_completed;
};

void uvm_bpf_prefetch_diagnostic(const struct uvm_bpf_prefetch_diagnostic_ctx *ctx);

typedef struct uvm_bpf_pmm_decision_ctx
{
    // All fields are driver-owned and read/write access from BPF is rejected.
    // The pointer is only valid for the current callback invocation.
    uvm_pmm_gpu_t *pmm;
    uvm_gpu_root_chunk_t *root_chunk;
    nv_gpu_pmm_snapshot_t observed;
    nv_gpu_pmm_request_t request;
} uvm_bpf_pmm_decision_ctx_t;

/* Action codes returned by BPF hooks */
enum uvm_bpf_action {
    UVM_BPF_ACTION_DEFAULT = 0,       /* Use default kernel behavior */
    UVM_BPF_ACTION_BYPASS = 1,        /* Skip default kernel behavior */
    UVM_BPF_ACTION_ENTER_LOOP = 2,    /* Enter the tree iteration loop with BPF hooks */
};

/* Function declarations for BPF struct_ops initialization */
int uvm_bpf_struct_ops_init(void);
void uvm_bpf_struct_ops_exit(void);

/* Wrapper functions for calling BPF hooks from kernel code */
NvS64 uvm_bpf_call_gpu_page_prefetch(
    uvm_page_index_t page_index,
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t *max_prefetch_region,
    nv_gpu_prefetch_decision_t *decision);

NvS64 uvm_bpf_call_gpu_page_prefetch_iter(
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t *max_prefetch_region,
    uvm_va_block_region_t *current_region,
    unsigned int counter,
    nv_gpu_prefetch_decision_t *decision);

NvS64 uvm_bpf_call_gpu_stale_state_v1(
    uvm_stale_state_v1_decision_ctx_t *decision_ctx);

/* PMM eviction policy hook wrapper functions */
void uvm_bpf_call_gpu_block_activate(
    uvm_pmm_gpu_t *pmm,
    uvm_gpu_chunk_t *chunk);

enum nv_gpu_pmm_access_effect uvm_bpf_call_gpu_block_access(
    uvm_pmm_gpu_t *pmm,
    uvm_gpu_chunk_t *chunk);

void uvm_bpf_call_gpu_evict_prepare(
    uvm_pmm_gpu_t *pmm,
    struct list_head *va_block_used,
    struct list_head *va_block_unused);

#endif /* _UVM_BPF_STRUCT_OPS_H */
