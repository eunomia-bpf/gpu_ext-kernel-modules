/* SPDX-License-Identifier: MIT */
#ifndef _UVM_STALE_STATE_V1_H_
#define _UVM_STALE_STATE_V1_H_

#include "nv-gpu-stale-state-v1.h"
#include "nv-gpu-transition-validator.h"
#include "uvm_va_block_types.h"

enum uvm_stale_state_v1_mode
{
    UVM_STALE_STATE_V1_MODE_OFF = 0,
    UVM_STALE_STATE_V1_MODE_NATIVE = 1,
    UVM_STALE_STATE_V1_MODE_BPF = 2,
};

enum uvm_stale_state_v1_status
{
    UVM_STALE_STATE_V1_STATUS_UNSET = 0,
    UVM_STALE_STATE_V1_STATUS_MISSING_SNAPSHOT = 1,
    UVM_STALE_STATE_V1_STATUS_INVALID_SNAPSHOT = 2,
    UVM_STALE_STATE_V1_STATUS_REQUEST_ERROR = 3,
    UVM_STALE_STATE_V1_STATUS_DECISION_READY = 4,
    UVM_STALE_STATE_V1_STATUS_EFFECT_APPLIED = 5,
    UVM_STALE_STATE_V1_STATUS_EFFECT_ERROR = 6,
};

enum uvm_stale_state_v1_diagnostic_phase
{
    UVM_STALE_STATE_V1_DIAG_SELECTED = 1,
    UVM_STALE_STATE_V1_DIAG_FINISHED = 2,
};

/*
 * This prefix is driver-owned and read-only to BPF. The timestamp is captured
 * once in the driver so native and BPF consumers use the same clock boundary.
 */
struct uvm_stale_state_v1_input
{
    nv_gpu_stale_state_v1_snapshot_t snapshot;
    NvU64 generation;
    NvU64 decision_sequence;
    NvU64 decision_mono_ns;
    NvU64 page_index;
    NvU64 max_first;
    NvU64 max_outer;
    NvU32 abi_version;
    NvU32 reserved;
};

/*
 * BPF cannot write this context directly. It submits one action through the
 * trusted kfunc, which latches the request in the private suffix.
 */
typedef struct uvm_stale_state_v1_decision_ctx
{
    struct uvm_stale_state_v1_input input;
    nv_gpu_transition_u32_request_t action_request;
    NvU32 request_calls;
    NvU32 request_cookie;
} uvm_stale_state_v1_decision_ctx_t;

/* Address-free record consumed by a privileged fentry/fexit observer. */
struct uvm_stale_state_v1_diagnostic
{
    struct uvm_stale_state_v1_input input;
    NvS64 callback_return;
    NvU64 decision_age_ns;
    NvU64 requested_first;
    NvU64 requested_outer;
    NvU64 output_first;
    NvU64 output_outer;
    NvU32 diagnostic_phase;
    NvU32 mode;
    NvU32 status;
    NvU32 action;
    NvU32 action_attempted;
    NvU32 action_conflict;
    NvU32 action_request_calls;
    NvU32 region_result;
    NvU32 initial_effect;
    NvU32 reserved;
};

int uvm_stale_state_v1_init(void);
void uvm_stale_state_v1_exit(void);

/* Returns false only when the versioned bridge is disabled. */
bool uvm_stale_state_v1_begin(uvm_page_index_t page_index,
                              const uvm_va_block_region_t *maximum,
                              uvm_stale_state_v1_decision_ctx_t *decision_ctx,
                              struct uvm_stale_state_v1_diagnostic *diagnostic,
                              NvS64 *raw_action,
                              nv_gpu_prefetch_decision_t *request);

void uvm_stale_state_v1_selected(struct uvm_stale_state_v1_diagnostic *diagnostic,
                                 enum nv_gpu_transition_result region_result,
                                 enum nv_gpu_prefetch_initial_effect initial_effect);

void uvm_stale_state_v1_finished(struct uvm_stale_state_v1_diagnostic *diagnostic,
                                 const uvm_va_block_region_t *output);

int uvm_stale_state_v1_record_bpf_action(uvm_stale_state_v1_decision_ctx_t *decision_ctx,
                                         NvU32 action);

void uvm_stale_state_v1_diagnostic(
    const struct uvm_stale_state_v1_diagnostic *diagnostic);

#endif
