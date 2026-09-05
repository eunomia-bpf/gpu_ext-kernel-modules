/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
 * SPDX-License-Identifier: MIT
 */

#ifndef _NV_GPU_STALE_STATE_V1_H_
#define _NV_GPU_STALE_STATE_V1_H_

#include "nvtypes.h"

#define NV_GPU_STALE_STATE_V1_ABI_VERSION 1U

enum nv_gpu_stale_state_v1_phase
{
    NV_GPU_STALE_STATE_V1_PHASE_INVALID = 0,
    NV_GPU_STALE_STATE_V1_PHASE_DENSE = 1,
    NV_GPU_STALE_STATE_V1_PHASE_SPARSE = 2,
};

enum nv_gpu_stale_state_v1_action
{
    NV_GPU_STALE_STATE_V1_ACTION_REJECT = 0,
    NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX = 1,
    NV_GPU_STALE_STATE_V1_ACTION_DISCARD_PREFETCH = 2,
};

/* Immutable after publication. One pointer publication exposes all fields. */
typedef struct
{
    NvU64 sequence;
    NvU64 source_mono_ns;
    NvU64 published_mono_ns;
    NvU32 phase;
    NvU32 reserved;
} nv_gpu_stale_state_v1_snapshot_t;

typedef struct
{
    NvU64 snapshot_sequence;
    NvU64 decision_age_ns;
    NvU32 snapshot_phase;
    NvU32 action;
} nv_gpu_stale_state_v1_decision_t;

static inline NvBool
nv_gpu_stale_state_v1_publication_follows(
    const nv_gpu_stale_state_v1_snapshot_t *current_snapshot,
    NvU64 sequence,
    NvU64 source_mono_ns)
{
    if ((sequence == 0) || (source_mono_ns == 0))
        return NV_FALSE;

    if (current_snapshot == 0)
        return sequence == 1 ? NV_TRUE : NV_FALSE;

    if ((current_snapshot->sequence == ~(NvU64)0) ||
        (sequence != current_snapshot->sequence + 1) ||
        (source_mono_ns <= current_snapshot->source_mono_ns))
        return NV_FALSE;

    return NV_TRUE;
}

static inline NvBool
nv_gpu_stale_state_v1_snapshot_valid(const nv_gpu_stale_state_v1_snapshot_t *snapshot,
                                     NvU64 decision_mono_ns)
{
    if ((snapshot == 0) || (snapshot->sequence == 0) ||
        (snapshot->reserved != 0))
        return NV_FALSE;

    if ((snapshot->phase != NV_GPU_STALE_STATE_V1_PHASE_DENSE) &&
        (snapshot->phase != NV_GPU_STALE_STATE_V1_PHASE_SPARSE))
        return NV_FALSE;

    if ((snapshot->source_mono_ns == 0) ||
        (snapshot->published_mono_ns == 0) ||
        (snapshot->published_mono_ns < snapshot->source_mono_ns) ||
        (decision_mono_ns < snapshot->published_mono_ns))
        return NV_FALSE;

    return NV_TRUE;
}

static inline enum nv_gpu_stale_state_v1_action
nv_gpu_stale_state_v1_choose(const nv_gpu_stale_state_v1_snapshot_t *snapshot,
                             NvU64 decision_mono_ns,
                             nv_gpu_stale_state_v1_decision_t *decision)
{
    enum nv_gpu_stale_state_v1_action action;

    if ((decision == 0) ||
        !nv_gpu_stale_state_v1_snapshot_valid(snapshot, decision_mono_ns))
        return NV_GPU_STALE_STATE_V1_ACTION_REJECT;

    action = snapshot->phase == NV_GPU_STALE_STATE_V1_PHASE_DENSE ?
                 NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX :
                 NV_GPU_STALE_STATE_V1_ACTION_DISCARD_PREFETCH;
    decision->snapshot_sequence = snapshot->sequence;
    decision->decision_age_ns = decision_mono_ns - snapshot->source_mono_ns;
    decision->snapshot_phase = snapshot->phase;
    decision->action = action;
    return action;
}

#endif
