/* SPDX-License-Identifier: MIT */
#ifndef NV_GPU_SCHED_INIT_DIAGNOSTIC_H
#define NV_GPU_SCHED_INIT_DIAGNOSTIC_H

#include "nvtypes.h"

#define NV_GPU_SCHED_INIT_DIAGNOSTIC_ABI_VERSION 1U
#define NV_GPU_SCHED_INIT_DIAGNOSTIC_STATUS_NOT_OBSERVED (~(NvU32)0)

enum nv_gpu_sched_init_diagnostic_phase
{
    NV_GPU_SCHED_INIT_DIAGNOSTIC_VALIDATED = 1,
    NV_GPU_SCHED_INIT_DIAGNOSTIC_NATIVE_RETURN = 2,
    NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN = 3,
};

enum nv_gpu_sched_init_diagnostic_field
{
    NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_NONE = 0,
    NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_TIMESLICE = 1,
    NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_INTERLEAVE = 2,
};

/*
 * Address-free, read-only observation of one scheduler-init constructor.
 * The hook receiving this context has no return channel and is not consulted
 * by the driver. All booleans are normalized into fixed-width NvU32 fields so
 * the kernel and BPF observer share one stable layout.
 */
struct nv_gpu_sched_init_diagnostic_ctx
{
    NvU32 abi_version;
    NvU32 abi_size;
    NvU32 phase;
    NvU32 field;

    NvU32 h_client;
    NvU32 h_resource;
    NvU32 gpu_instance;
    NvU32 subdevice_instance;
    NvU32 group_id;
    NvU32 runlist_id;
    NvU32 engine_type;
    NvU32 constructor_epoch;

    NvU64 default_timeslice;
    NvU64 minimum_timeslice;
    NvU32 default_interleave;
    NvU32 timeslice_attempted;
    NvU32 timeslice_conflict;
    NvU32 reserved0;
    NvU64 timeslice_request_value;
    NvU32 interleave_attempted;
    NvU32 interleave_conflict;
    NvU32 interleave_request_value;
    NvU32 timeslice_validation_result;
    NvU32 interleave_validation_result;
    NvU32 reserved1;
    NvU64 effective_timeslice;
    NvU32 effective_interleave;
    NvU32 timeslice_native_status;
    NvU64 timeslice_post_value;
    NvU32 interleave_native_status;
    NvU32 interleave_post_value;
    NvU32 constructor_status;
    NvU32 final_interleave;
    NvU64 final_timeslice;
    NvU32 final_snapshot_valid;
    NvU32 reserved2;
};

#endif /* NV_GPU_SCHED_INIT_DIAGNOSTIC_H */
