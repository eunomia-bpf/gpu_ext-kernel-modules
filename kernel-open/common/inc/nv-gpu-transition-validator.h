/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
 * SPDX-License-Identifier: MIT
 */

#ifndef _NV_GPU_TRANSITION_VALIDATOR_H_
#define _NV_GPU_TRANSITION_VALIDATOR_H_

#include "nvtypes.h"

enum nv_gpu_transition_result
{
    NV_GPU_TRANSITION_APPLY = 0,
    NV_GPU_TRANSITION_NOOP_DEFAULT,
    NV_GPU_TRANSITION_NOOP_REPEAT,
    NV_GPU_TRANSITION_NOOP_STALE,
    NV_GPU_TRANSITION_NOOP_CONFLICT,
    NV_GPU_TRANSITION_REJECT_ACTION,
    NV_GPU_TRANSITION_REJECT_RANGE,
    NV_GPU_TRANSITION_REJECT_IDENTITY,
};

enum nv_gpu_transition_action
{
    NV_GPU_TRANSITION_ACTION_DEFAULT = 0,
    NV_GPU_TRANSITION_ACTION_BYPASS = 1,
    NV_GPU_TRANSITION_ACTION_ENTER_LOOP = 2,
};

enum nv_gpu_pmm_destination
{
    NV_GPU_PMM_DESTINATION_USED = 1,
    NV_GPU_PMM_DESTINATION_UNUSED = 2,
};

enum nv_gpu_pmm_position
{
    NV_GPU_PMM_POSITION_HEAD = 1,
    NV_GPU_PMM_POSITION_TAIL = 2,
};

enum nv_gpu_pmm_access_effect
{
    NV_GPU_PMM_ACCESS_NATIVE = 0,
    NV_GPU_PMM_ACCESS_PRESERVE,
    NV_GPU_PMM_ACCESS_COMMIT,
};

typedef struct
{
    NvBool attempted;
    NvBool conflict;
    NvU64 value;
} nv_gpu_transition_u64_request_t;

typedef struct
{
    NvBool attempted;
    NvBool conflict;
    NvU32 value;
} nv_gpu_transition_u32_request_t;

typedef struct
{
    NvU64 tsg_id;
    NvU32 runlist_id;
    NvU32 phase;
} nv_gpu_scheduler_snapshot_t;

typedef struct
{
    enum nv_gpu_transition_result timeslice_result;
    enum nv_gpu_transition_result interleave_result;
    NvU64 timeslice;
    NvU32 interleave;
} nv_gpu_scheduler_validation_t;

typedef struct
{
    NvBool attempted;
    NvBool conflict;
    NvU64 first;
    NvU64 outer;
} nv_gpu_prefetch_decision_t;

typedef struct
{
    NvU64 first;
    NvU64 outer;
} nv_gpu_transition_region_t;

typedef struct
{
    NvBool attempted;
    NvBool conflict;
    NvU64 destination;
    NvU64 position;
} nv_gpu_pmm_request_t;

typedef struct
{
    NvU64 owner_id;
    NvU64 root_id;
    NvU64 generation;
    NvU32 source;
} nv_gpu_pmm_snapshot_t;

static inline enum nv_gpu_transition_result
nv_gpu_transition_record_u64(nv_gpu_transition_u64_request_t *request, NvU64 value)
{
    if (!request->attempted)
    {
        request->attempted = NV_TRUE;
        request->value = value;
        return NV_GPU_TRANSITION_APPLY;
    }

    if (request->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;

    if (request->value == value)
        return NV_GPU_TRANSITION_NOOP_REPEAT;

    request->conflict = NV_TRUE;
    return NV_GPU_TRANSITION_NOOP_CONFLICT;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_record_u32(nv_gpu_transition_u32_request_t *request, NvU32 value)
{
    if (!request->attempted)
    {
        request->attempted = NV_TRUE;
        request->value = value;
        return NV_GPU_TRANSITION_APPLY;
    }

    if (request->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;

    if (request->value == value)
        return NV_GPU_TRANSITION_NOOP_REPEAT;

    request->conflict = NV_TRUE;
    return NV_GPU_TRANSITION_NOOP_CONFLICT;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_validate_snapshot(const nv_gpu_scheduler_snapshot_t *expected,
                                    const nv_gpu_scheduler_snapshot_t *current)
{
    if ((expected->tsg_id != current->tsg_id) ||
        (expected->runlist_id != current->runlist_id))
        return NV_GPU_TRANSITION_REJECT_IDENTITY;

    if (expected->phase != current->phase)
        return NV_GPU_TRANSITION_NOOP_STALE;

    return NV_GPU_TRANSITION_APPLY;
}

static inline nv_gpu_scheduler_validation_t
nv_gpu_transition_validate_scheduler(const nv_gpu_scheduler_snapshot_t *expected,
                                     const nv_gpu_scheduler_snapshot_t *current,
                                     NvU64 native_timeslice,
                                     NvU32 native_interleave,
                                     NvU64 minimum_timeslice,
                                     const nv_gpu_transition_u64_request_t *timeslice_request,
                                     const nv_gpu_transition_u32_request_t *interleave_request)
{
    nv_gpu_scheduler_validation_t validation = {
        .timeslice_result = NV_GPU_TRANSITION_NOOP_DEFAULT,
        .interleave_result = NV_GPU_TRANSITION_NOOP_DEFAULT,
        .timeslice = native_timeslice,
        .interleave = native_interleave,
    };
    enum nv_gpu_transition_result snapshot_result =
        nv_gpu_transition_validate_snapshot(expected, current);

    if (snapshot_result != NV_GPU_TRANSITION_APPLY)
    {
        validation.timeslice_result = snapshot_result;
        validation.interleave_result = snapshot_result;
        return validation;
    }

    if (timeslice_request->conflict)
        validation.timeslice_result = NV_GPU_TRANSITION_NOOP_CONFLICT;
    else if (timeslice_request->attempted)
    {
        if (timeslice_request->value < minimum_timeslice)
            validation.timeslice_result = NV_GPU_TRANSITION_REJECT_RANGE;
        else
        {
            validation.timeslice_result = NV_GPU_TRANSITION_APPLY;
            validation.timeslice = timeslice_request->value;
        }
    }

    if (interleave_request->conflict)
        validation.interleave_result = NV_GPU_TRANSITION_NOOP_CONFLICT;
    else if (interleave_request->attempted)
    {
        if (interleave_request->value > 2U)
            validation.interleave_result = NV_GPU_TRANSITION_REJECT_RANGE;
        else
        {
            validation.interleave_result = NV_GPU_TRANSITION_APPLY;
            validation.interleave = interleave_request->value;
        }
    }

    return validation;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_record_prefetch(nv_gpu_prefetch_decision_t *decision,
                                  NvU64 first,
                                  NvU64 outer)
{
    if (!decision->attempted)
    {
        decision->attempted = NV_TRUE;
        decision->first = first;
        decision->outer = outer;
        return NV_GPU_TRANSITION_APPLY;
    }

    if (decision->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;

    if ((decision->first == first) && (decision->outer == outer))
        return NV_GPU_TRANSITION_NOOP_REPEAT;

    decision->conflict = NV_TRUE;
    return NV_GPU_TRANSITION_NOOP_CONFLICT;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_validate_action(NvS64 raw_action, NvU32 *action)
{
    *action = NV_GPU_TRANSITION_ACTION_DEFAULT;

    if ((raw_action < NV_GPU_TRANSITION_ACTION_DEFAULT) ||
        (raw_action > NV_GPU_TRANSITION_ACTION_ENTER_LOOP))
        return NV_GPU_TRANSITION_REJECT_ACTION;

    *action = (NvU32)raw_action;
    return NV_GPU_TRANSITION_APPLY;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_validate_region(const nv_gpu_prefetch_decision_t *decision,
                                  NvU64 max_first,
                                  NvU64 max_outer,
                                  NvU64 block_outer,
                                  NvU64 type_outer,
                                  nv_gpu_transition_region_t *region)
{
    region->first = 0;
    region->outer = 0;

    if (decision->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;

    if (!decision->attempted)
        return NV_GPU_TRANSITION_NOOP_DEFAULT;

    if ((decision->first == 0) && (decision->outer == 0))
        return NV_GPU_TRANSITION_APPLY;

    if ((max_first > max_outer) || (max_outer > block_outer) ||
        (max_outer > type_outer))
        return NV_GPU_TRANSITION_REJECT_RANGE;

    if ((decision->first >= decision->outer) ||
        (decision->first < max_first) ||
        (decision->outer > max_outer) ||
        (decision->first > block_outer) ||
        (decision->outer > block_outer) ||
        (decision->first > type_outer) ||
        (decision->outer > type_outer))
        return NV_GPU_TRANSITION_REJECT_RANGE;

    region->first = decision->first;
    region->outer = decision->outer;
    return NV_GPU_TRANSITION_APPLY;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_translate_endpoint(NvU64 max_first,
                                     NvU64 relative_endpoint,
                                     NvU64 tree_offset,
                                     NvU64 max_outer,
                                     NvU64 block_outer,
                                     NvU64 type_outer,
                                     NvU64 *absolute_endpoint)
{
    NvU64 widened;

    *absolute_endpoint = 0;

    if (relative_endpoint > (~(NvU64)0 - max_first))
        return NV_GPU_TRANSITION_REJECT_RANGE;

    widened = max_first + relative_endpoint;
    if (widened < tree_offset)
        return NV_GPU_TRANSITION_REJECT_RANGE;

    widened -= tree_offset;
    if ((widened < max_first) || (widened > max_outer) ||
        (widened > block_outer) || (widened > type_outer))
        return NV_GPU_TRANSITION_REJECT_RANGE;

    *absolute_endpoint = widened;
    return NV_GPU_TRANSITION_APPLY;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_translate_region(NvU64 max_first,
                                   NvU64 relative_first,
                                   NvU64 relative_outer,
                                   NvU64 tree_offset,
                                   NvU64 max_outer,
                                   NvU64 block_outer,
                                   NvU64 type_outer,
                                   nv_gpu_transition_region_t *region)
{
    enum nv_gpu_transition_result result;

    region->first = 0;
    region->outer = 0;

    result = nv_gpu_transition_translate_endpoint(max_first,
                                                  relative_first,
                                                  tree_offset,
                                                  max_outer,
                                                  block_outer,
                                                  type_outer,
                                                  &region->first);
    if (result != NV_GPU_TRANSITION_APPLY)
        return result;

    result = nv_gpu_transition_translate_endpoint(max_first,
                                                  relative_outer,
                                                  tree_offset,
                                                  max_outer,
                                                  block_outer,
                                                  type_outer,
                                                  &region->outer);
    if (result != NV_GPU_TRANSITION_APPLY)
    {
        region->first = 0;
        return result;
    }

    if (region->first >= region->outer)
    {
        region->first = 0;
        region->outer = 0;
        return NV_GPU_TRANSITION_REJECT_RANGE;
    }

    return NV_GPU_TRANSITION_APPLY;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_record_pmm(nv_gpu_pmm_request_t *request,
                             NvU64 destination,
                             NvU64 position)
{
    if (!request->attempted)
    {
        request->attempted = NV_TRUE;
        request->destination = destination;
        request->position = position;
        return NV_GPU_TRANSITION_APPLY;
    }

    if (request->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;

    if ((request->destination == destination) &&
        (request->position == position))
        return NV_GPU_TRANSITION_NOOP_REPEAT;

    request->conflict = NV_TRUE;
    return NV_GPU_TRANSITION_NOOP_CONFLICT;
}

static inline enum nv_gpu_transition_result
nv_gpu_transition_validate_pmm(const nv_gpu_pmm_snapshot_t *expected,
                               const nv_gpu_pmm_snapshot_t *current,
                               const nv_gpu_pmm_request_t *request)
{
    if ((expected->owner_id != current->owner_id) ||
        (expected->root_id != current->root_id))
        return NV_GPU_TRANSITION_REJECT_IDENTITY;

    if ((expected->generation != current->generation) ||
        (expected->source != current->source))
        return NV_GPU_TRANSITION_NOOP_STALE;

    if (request->conflict)
        return NV_GPU_TRANSITION_NOOP_CONFLICT;

    if (!request->attempted)
        return NV_GPU_TRANSITION_NOOP_DEFAULT;

    if (((request->destination != NV_GPU_PMM_DESTINATION_USED) &&
         (request->destination != NV_GPU_PMM_DESTINATION_UNUSED)) ||
        ((request->position != NV_GPU_PMM_POSITION_HEAD) &&
         (request->position != NV_GPU_PMM_POSITION_TAIL)))
        return NV_GPU_TRANSITION_REJECT_RANGE;

    return NV_GPU_TRANSITION_APPLY;
}

static inline enum nv_gpu_pmm_access_effect
nv_gpu_transition_pmm_access_effect(NvS64 raw_action,
                                    enum nv_gpu_transition_result request_result)
{
    if ((raw_action != NV_GPU_TRANSITION_ACTION_DEFAULT) &&
        (raw_action != NV_GPU_TRANSITION_ACTION_BYPASS))
        return NV_GPU_PMM_ACCESS_PRESERVE;

    if (request_result == NV_GPU_TRANSITION_APPLY)
        return NV_GPU_PMM_ACCESS_COMMIT;

    if ((request_result == NV_GPU_TRANSITION_NOOP_DEFAULT) &&
        (raw_action == NV_GPU_TRANSITION_ACTION_DEFAULT))
        return NV_GPU_PMM_ACCESS_NATIVE;

    return NV_GPU_PMM_ACCESS_PRESERVE;
}

static inline enum nv_gpu_pmm_access_effect
nv_gpu_transition_pmm_activate_effect(enum nv_gpu_transition_result request_result)
{
    if (request_result == NV_GPU_TRANSITION_APPLY)
        return NV_GPU_PMM_ACCESS_COMMIT;

    return NV_GPU_PMM_ACCESS_PRESERVE;
}

#endif /* _NV_GPU_TRANSITION_VALIDATOR_H_ */
