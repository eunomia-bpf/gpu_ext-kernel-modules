/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "nv-gpu-sched-init-diagnostic.h"
#include "nv-gpu-sched-hooks.h"

static unsigned int assertions;

#define EXPECT(expression)                                                     \
    do                                                                         \
    {                                                                          \
        ++assertions;                                                          \
        if (!(expression))                                                     \
        {                                                                      \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

typedef void (*diagnostic_hook_t)(
    const struct nv_gpu_sched_init_diagnostic_ctx *ctx);

_Static_assert(
    __builtin_types_compatible_p(
        __typeof__(&nv_gpu_sched_init_diagnostic), diagnostic_hook_t),
    "the diagnostic hook must have no return channel");

static void test_exact_abi(void)
{
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_ABI_VERSION == 1U);
    EXPECT(sizeof(struct nv_gpu_sched_init_diagnostic_ctx) == 168U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, abi_version) == 0U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, abi_size) == 4U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, phase) == 8U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, field) == 12U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, h_client) == 16U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, h_resource) == 20U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, gpu_instance) == 24U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, subdevice_instance) == 28U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, group_id) == 32U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, runlist_id) == 36U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, engine_type) == 40U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, constructor_epoch) == 44U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, default_timeslice) == 48U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, minimum_timeslice) == 56U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, default_interleave) == 64U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, timeslice_attempted) == 68U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, timeslice_conflict) == 72U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, reserved0) == 76U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, timeslice_request_value) == 80U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, interleave_attempted) == 88U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, interleave_conflict) == 92U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, interleave_request_value) == 96U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, timeslice_validation_result) == 100U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, interleave_validation_result) == 104U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, reserved1) == 108U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, effective_timeslice) == 112U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, effective_interleave) == 120U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, timeslice_native_status) == 124U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, timeslice_post_value) == 128U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, interleave_native_status) == 136U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, interleave_post_value) == 140U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, constructor_status) == 144U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, final_interleave) == 148U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, final_timeslice) == 152U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, final_snapshot_valid) == 160U);
    EXPECT(offsetof(struct nv_gpu_sched_init_diagnostic_ctx, reserved2) == 164U);
}

static void test_phase_and_field_values(void)
{
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_VALIDATED == 1);
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_NATIVE_RETURN == 2);
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN == 3);
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_NONE == 0);
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_TIMESLICE == 1);
    EXPECT(NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_INTERLEAVE == 2);
}

static void test_event_construction(void)
{
    const NvU32 nativeStatus = 0x31U;
    const NvU32 constructorStatus = 0x47U;
    struct nv_gpu_sched_init_diagnostic_ctx ctx = {
        .abi_version = NV_GPU_SCHED_INIT_DIAGNOSTIC_ABI_VERSION,
        .abi_size = sizeof(ctx),
        .h_client = 11U,
        .h_resource = 12U,
        .gpu_instance = 1U,
        .subdevice_instance = 2U,
        .group_id = 13U,
        .runlist_id = 14U,
        .engine_type = 15U,
        .constructor_epoch = 16U,
        .default_timeslice = 2048U,
        .minimum_timeslice = 1024U,
        .default_interleave = 1U,
        .timeslice_attempted = 1U,
        .timeslice_request_value = 4096U,
        .interleave_attempted = 1U,
        .interleave_request_value = 0U,
        .timeslice_validation_result = 0U,
        .interleave_validation_result = 0U,
        .effective_timeslice = 4096U,
        .effective_interleave = 0U,
        .timeslice_native_status = NV_GPU_SCHED_INIT_DIAGNOSTIC_STATUS_NOT_OBSERVED,
        .interleave_native_status = NV_GPU_SCHED_INIT_DIAGNOSTIC_STATUS_NOT_OBSERVED,
    };
    struct nv_gpu_sched_init_diagnostic_ctx validated;
    struct nv_gpu_sched_init_diagnostic_ctx nativeTimeslice;
    struct nv_gpu_sched_init_diagnostic_ctx nativeInterleave;
    struct nv_gpu_sched_init_diagnostic_ctx constructorReturn;

    ctx.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_VALIDATED;
    ctx.field = NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_NONE;
    validated = ctx;

    ctx.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_NATIVE_RETURN;
    ctx.field = NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_TIMESLICE;
    ctx.timeslice_native_status = nativeStatus;
    ctx.timeslice_post_value = 4096U;
    nativeTimeslice = ctx;

    ctx.field = NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_INTERLEAVE;
    ctx.interleave_native_status = nativeStatus;
    ctx.interleave_post_value = 0U;
    nativeInterleave = ctx;

    ctx.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN;
    ctx.field = NV_GPU_SCHED_INIT_DIAGNOSTIC_FIELD_NONE;
    ctx.constructor_status = constructorStatus;
    ctx.final_timeslice = 4096U;
    ctx.final_interleave = 0U;
    ctx.final_snapshot_valid = 1U;
    constructorReturn = ctx;

    EXPECT(validated.constructor_epoch == 16U);
    EXPECT(validated.timeslice_native_status ==
           NV_GPU_SCHED_INIT_DIAGNOSTIC_STATUS_NOT_OBSERVED);
    EXPECT(validated.interleave_native_status ==
           NV_GPU_SCHED_INIT_DIAGNOSTIC_STATUS_NOT_OBSERVED);
    EXPECT(nativeTimeslice.timeslice_native_status == nativeStatus);
    EXPECT(nativeTimeslice.interleave_native_status ==
           NV_GPU_SCHED_INIT_DIAGNOSTIC_STATUS_NOT_OBSERVED);
    EXPECT(nativeInterleave.timeslice_native_status == nativeStatus);
    EXPECT(nativeInterleave.interleave_native_status == nativeStatus);
    EXPECT(constructorReturn.constructor_status == constructorStatus);
    EXPECT(nativeStatus == 0x31U);
    EXPECT(constructorStatus == 0x47U);
    EXPECT(constructorReturn.final_snapshot_valid == 1U);
    EXPECT(constructorReturn.h_client == validated.h_client);
    EXPECT(constructorReturn.h_resource == validated.h_resource);
    EXPECT(constructorReturn.constructor_epoch == validated.constructor_epoch);
}

int main(void)
{
    test_exact_abi();
    test_phase_and_field_values();
    test_event_construction();
    printf("sched_init_diagnostic: 3 cases, %u assertions passed (CPU only)\n",
           assertions);
    return 0;
}
