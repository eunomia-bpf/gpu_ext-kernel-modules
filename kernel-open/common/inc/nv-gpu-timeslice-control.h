/* SPDX-License-Identifier: MIT */
#ifndef NV_GPU_TIMESLICE_CONTROL_H
#define NV_GPU_TIMESLICE_CONTROL_H
#include "nv-gpu-transition-validator.h"

#define NV_GPU_TIMESLICE_CONTROL_PHASE 1U
#define NV_GPU_TIMESLICE_CONTROL_MAX_US 1000000ULL

/* Read-only, driver-owned identity at an already-authorized control boundary. */
struct nv_gpu_timeslice_control_ctx {
    NvU64 tsg_id;
    NvU64 requested_timeslice_us;
    NvU32 engine_type;
    NvU32 runlist_id;
    NvU32 hclient;
    NvU32 htsg;
    NvU32 gpu_instance;
    NvU32 phase;
};
struct nv_gpu_timeslice_control_decision_ctx {
    struct nv_gpu_timeslice_control_ctx input;
    nv_gpu_transition_u64_request_t request;
};

/* Values outside this explicitly bounded policy domain retain native behavior;
 * in particular an invalid incoming request is never repaired by BPF. */
static inline NvBool nv_gpu_timeslice_control_eligible(NvU64 value, NvU64 minimum)
{
    return value >= 1 && value >= minimum && value <= NV_GPU_TIMESLICE_CONTROL_MAX_US;
}

static inline enum nv_gpu_transition_result
nv_gpu_timeslice_control_record(struct nv_gpu_timeslice_control_decision_ctx *decision, NvU64 value)
{
    enum nv_gpu_transition_result result = nv_gpu_transition_record_u64(&decision->request, value);
    if (result == NV_GPU_TRANSITION_APPLY && !nv_gpu_timeslice_control_eligible(value, 0))
        return NV_GPU_TRANSITION_REJECT_RANGE;
    return result;
}

static inline enum nv_gpu_transition_result
nv_gpu_timeslice_control_validate(const struct nv_gpu_timeslice_control_ctx *expected,
                                 const struct nv_gpu_timeslice_control_ctx *observed,
                                 NvU64 minimum,
                                 const nv_gpu_transition_u64_request_t *request,
                                 NvU64 *effective)
{
    *effective = expected->requested_timeslice_us;
    if (expected->tsg_id != observed->tsg_id || expected->runlist_id != observed->runlist_id ||
        expected->engine_type != observed->engine_type || expected->hclient != observed->hclient ||
        expected->htsg != observed->htsg || expected->gpu_instance != observed->gpu_instance ||
        expected->requested_timeslice_us != observed->requested_timeslice_us ||
        !expected->hclient || !expected->htsg)
        return NV_GPU_TRANSITION_REJECT_IDENTITY;
    if (expected->phase != NV_GPU_TIMESLICE_CONTROL_PHASE || expected->phase != observed->phase)
        return NV_GPU_TRANSITION_NOOP_STALE;
    if (!request->attempted)
        return NV_GPU_TRANSITION_NOOP_DEFAULT;
    if (request->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;
    if (!nv_gpu_timeslice_control_eligible(expected->requested_timeslice_us, minimum) ||
        !nv_gpu_timeslice_control_eligible(request->value, minimum))
        return NV_GPU_TRANSITION_REJECT_RANGE;
    *effective = request->value;
    return NV_GPU_TRANSITION_APPLY;
}
#endif
