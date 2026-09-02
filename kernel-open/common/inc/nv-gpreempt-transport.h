/* SPDX-License-Identifier: MIT */
#ifndef NV_GPREEMPT_TRANSPORT_H
#define NV_GPREEMPT_TRANSPORT_H

#include "nvtypes.h"

/* NV_ESC_RM_QUERY_GROUP, NVOS54 envelope. Zero flags preserve legacy QUERY. */
#define NV_GPREEMPT_V1_SET_TIMESLICE 0x00010001U
#define NV_GPREEMPT_SET_TIMESLICE_CMD 0xa06c0103U
#define NV_GPREEMPT_MAX_CHANNELS 64U

enum nv_gpreempt_operation {
    NV_GPREEMPT_INVALID,
    NV_GPREEMPT_QUERY,
    NV_GPREEMPT_SET_TIMESLICE,
};

static inline enum nv_gpreempt_operation
nv_gpreempt_operation(NvU32 flags, NvU32 cmd, NvU32 hClient, NvU32 hObject,
                      NvBool hasParams, NvU32 paramsSize, NvU32 channelsSize)
{
    if (!hClient || !hasParams)
        return NV_GPREEMPT_INVALID;
    if (flags == 0 && cmd == 0 && hObject == 0 && paramsSize == channelsSize)
        return NV_GPREEMPT_QUERY;
    if (flags == NV_GPREEMPT_V1_SET_TIMESLICE &&
        cmd == NV_GPREEMPT_SET_TIMESLICE_CMD && hObject != 0 &&
        paramsSize == sizeof(NvU64))
        return NV_GPREEMPT_SET_TIMESLICE;
    return NV_GPREEMPT_INVALID;
}

static inline NvBool
nv_gpreempt_owned_user(const void *currentPidInfo, const void *ownerPidInfo,
                       NvBool isUserClient)
{
    return isUserClient && currentPidInfo != 0 && currentPidInfo == ownerPidInfo;
}

static inline NvBool nv_gpreempt_timeslice_allowed(NvU64 value)
{
    /* This compatibility endpoint is not a general scheduling-control API. */
    return value == 1U || value == 1000000U;
}

static inline NvBool nv_gpreempt_channel_slot_available(NvU32 count)
{
    return count < NV_GPREEMPT_MAX_CHANNELS;
}

static inline NvBool nv_gpreempt_add_unique_match(NvU32 *matches)
{
    if (*matches != 0)
    {
        *matches = 2; /* Saturate at ambiguous, never wrap back to unique. */
        return NV_FALSE;
    }
    *matches = 1;
    return NV_TRUE;
}

static inline NvBool nv_gpreempt_query_complete(NvU32 matches, NvU32 channels)
{
    return matches == 1 && channels > 0 && channels <= NV_GPREEMPT_MAX_CHANNELS;
}

#endif
