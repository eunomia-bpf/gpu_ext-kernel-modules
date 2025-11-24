#ifndef _UVM_BPF_STRUCT_OPS_H
#define _UVM_BPF_STRUCT_OPS_H

#include "uvm_va_block_types.h"
#include "uvm_perf_prefetch.h"
#include "uvm_pmm_gpu.h"

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
enum uvm_bpf_action uvm_bpf_call_before_compute_prefetch(
    uvm_page_index_t page_index,
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t *max_prefetch_region,
    uvm_va_block_region_t *result_region);

enum uvm_bpf_action uvm_bpf_call_on_tree_iter(
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t *max_prefetch_region,
    uvm_va_block_region_t *current_region,
    unsigned int counter,
    uvm_va_block_region_t *prefetch_region);

/* PMM eviction policy hook wrapper functions */
void uvm_bpf_call_pmm_chunk_activate(
    uvm_pmm_gpu_t *pmm,
    uvm_gpu_chunk_t *chunk,
    struct list_head *list);

void uvm_bpf_call_pmm_chunk_populate(
    uvm_pmm_gpu_t *pmm,
    uvm_gpu_chunk_t *chunk,
    struct list_head *list);

void uvm_bpf_call_pmm_chunk_depopulate(
    uvm_pmm_gpu_t *pmm,
    uvm_gpu_chunk_t *chunk,
    struct list_head *list);

void uvm_bpf_call_pmm_eviction_prepare(
    uvm_pmm_gpu_t *pmm,
    struct list_head *va_block_used,
    struct list_head *va_block_unused);

#endif /* _UVM_BPF_STRUCT_OPS_H */
