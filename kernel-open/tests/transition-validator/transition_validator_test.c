/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "nv-gpu-transition-validator.h"

_Static_assert(offsetof(struct nv_gpu_task_init_ctx, tsg_id) == 0,
               "task-init tsg_id ABI");
_Static_assert(offsetof(struct nv_gpu_task_init_ctx, engine_type) == 8,
               "task-init engine_type ABI");
_Static_assert(offsetof(struct nv_gpu_task_init_ctx, default_timeslice) == 16,
               "task-init default_timeslice ABI");
_Static_assert(offsetof(struct nv_gpu_task_init_ctx, default_interleave) == 24,
               "task-init default_interleave ABI");
_Static_assert(offsetof(struct nv_gpu_task_init_ctx, runlist_id) == 28,
               "task-init runlist_id ABI");
_Static_assert(sizeof(struct nv_gpu_task_init_ctx) == 32,
               "task-init input ABI size");
_Static_assert(offsetof(struct nv_gpu_task_init_decision_ctx,
                        timeslice_request) == 32,
               "task-init timeslice decision offset");
_Static_assert(offsetof(struct nv_gpu_task_init_decision_ctx,
                        interleave_request) == 48,
               "task-init interleave decision offset");
_Static_assert(sizeof(struct nv_gpu_task_init_decision_ctx) == 56,
               "task-init decision ABI size");

#define EXPECT(condition)                                                         \
    do                                                                            \
    {                                                                             \
        ++assertion_count;                                                        \
        if (!(condition))                                                         \
        {                                                                         \
            fprintf(stderr, "%s:%d: expectation failed: %s\n",                   \
                    __FILE__, __LINE__, #condition);                              \
            exit(EXIT_FAILURE);                                                   \
        }                                                                         \
    } while (0)

static unsigned int assertion_count;

static void test_scheduler_presence_and_minimum(void)
{
    const nv_gpu_scheduler_snapshot_t snapshot = { 17, 3, 1 };
    nv_gpu_transition_u64_request_t timeslice = { 0 };
    nv_gpu_transition_u32_request_t interleave = { 0 };
    nv_gpu_scheduler_validation_t result;

    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_NOOP_DEFAULT);
    EXPECT(result.timeslice == 100);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_NOOP_DEFAULT);
    EXPECT(result.interleave == 1);

    EXPECT(nv_gpu_transition_record_u64(&timeslice, 0) == NV_GPU_TRANSITION_APPLY);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT(result.timeslice == 100);

    timeslice = (nv_gpu_transition_u64_request_t){ 0 };
    EXPECT(nv_gpu_transition_record_u64(&timeslice, 9) == NV_GPU_TRANSITION_APPLY);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT(result.timeslice == 100);

    timeslice = (nv_gpu_transition_u64_request_t){ 0 };
    EXPECT(nv_gpu_transition_record_u64(&timeslice, 10) == NV_GPU_TRANSITION_APPLY);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_APPLY);
    EXPECT(result.timeslice == 10);
}

static void test_interleave_low_and_range(void)
{
    const nv_gpu_scheduler_snapshot_t snapshot = { 17, 3, 1 };
    nv_gpu_transition_u64_request_t timeslice = { 0 };
    nv_gpu_transition_u32_request_t interleave = { 0 };
    nv_gpu_scheduler_validation_t result;
    NvU32 value;

    for (value = 0; value <= 2; ++value)
    {
        interleave = (nv_gpu_transition_u32_request_t){ 0 };
        EXPECT(nv_gpu_transition_record_u32(&interleave, value) ==
               NV_GPU_TRANSITION_APPLY);
        result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100,
                                                      1, 10, &timeslice,
                                                      &interleave);
        EXPECT(result.interleave_result == NV_GPU_TRANSITION_APPLY);
        EXPECT(result.interleave == value);
    }

    interleave = (nv_gpu_transition_u32_request_t){ 0 };
    nv_gpu_transition_record_u32(&interleave, 3);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT(result.interleave == 1);

    interleave = (nv_gpu_transition_u32_request_t){ 0 };
    nv_gpu_transition_record_u32(&interleave, ~(NvU32)0);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT(result.interleave == 1);
}

static void test_scheduler_identity_and_phase(void)
{
    const nv_gpu_scheduler_snapshot_t expected = { 17, 3, 1 };
    nv_gpu_scheduler_snapshot_t current = expected;
    nv_gpu_transition_u64_request_t timeslice = { 0 };
    nv_gpu_transition_u32_request_t interleave = { 0 };
    nv_gpu_scheduler_validation_t result;

    nv_gpu_transition_record_u64(&timeslice, 20);
    current.tsg_id = 18;
    result = nv_gpu_transition_validate_scheduler(&expected, &current, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_REJECT_IDENTITY);
    EXPECT(result.timeslice == 100);

    current = expected;
    current.runlist_id = 4;
    result = nv_gpu_transition_validate_scheduler(&expected, &current, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_REJECT_IDENTITY);
    EXPECT(result.timeslice == 100);

    current = expected;
    current.phase = 2;
    result = nv_gpu_transition_validate_scheduler(&expected, &current, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_NOOP_STALE);
    EXPECT(result.timeslice == 100);

    result = nv_gpu_transition_validate_scheduler(&expected, &expected, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_APPLY);
    EXPECT(result.timeslice == 20);
}

static void test_scheduler_repeat_and_conflict(void)
{
    const nv_gpu_scheduler_snapshot_t snapshot = { 17, 3, 1 };
    nv_gpu_transition_u64_request_t timeslice = { 0 };
    nv_gpu_transition_u32_request_t interleave = { 0 };
    nv_gpu_scheduler_validation_t result;

    EXPECT(nv_gpu_transition_record_u64(&timeslice, 20) == NV_GPU_TRANSITION_APPLY);
    EXPECT(nv_gpu_transition_record_u64(&timeslice, 20) ==
           NV_GPU_TRANSITION_NOOP_REPEAT);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_APPLY);
    EXPECT(result.timeslice == 20);

    EXPECT(nv_gpu_transition_record_u64(&timeslice, 30) ==
           NV_GPU_TRANSITION_NOOP_CONFLICT);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_NOOP_CONFLICT);
    EXPECT(result.timeslice == 100);

    EXPECT(nv_gpu_transition_record_u32(&interleave, 0) == NV_GPU_TRANSITION_APPLY);
    EXPECT(nv_gpu_transition_record_u32(&interleave, 0) ==
           NV_GPU_TRANSITION_NOOP_REPEAT);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_APPLY);
    EXPECT(result.interleave == 0);

    EXPECT(nv_gpu_transition_record_u32(&interleave, 2) ==
           NV_GPU_TRANSITION_NOOP_CONFLICT);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_NOOP_CONFLICT);
    EXPECT(result.interleave == 1);
}

static void test_scheduler_independent_fields(void)
{
    const nv_gpu_scheduler_snapshot_t snapshot = { 17, 3, 1 };
    nv_gpu_transition_u64_request_t timeslice = { 0 };
    nv_gpu_transition_u32_request_t interleave = { 0 };
    nv_gpu_scheduler_validation_t result;

    nv_gpu_transition_record_u64(&timeslice, 9);
    nv_gpu_transition_record_u32(&interleave, 2);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT(result.timeslice == 100);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_APPLY);
    EXPECT(result.interleave == 2);

    timeslice = (nv_gpu_transition_u64_request_t){ 0 };
    interleave = (nv_gpu_transition_u32_request_t){ 0 };
    nv_gpu_transition_record_u64(&timeslice, 20);
    nv_gpu_transition_record_u32(&interleave, 3);
    result = nv_gpu_transition_validate_scheduler(&snapshot, &snapshot, 100, 1,
                                                  10, &timeslice, &interleave);
    EXPECT(result.timeslice_result == NV_GPU_TRANSITION_APPLY);
    EXPECT(result.timeslice == 20);
    EXPECT(result.interleave_result == NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT(result.interleave == 1);
}

static void test_prefetch_action(void)
{
    NvU32 action;
    NvS64 raw;

    for (raw = 0; raw <= 2; ++raw)
    {
        EXPECT(nv_gpu_transition_validate_initial_action(raw, &action) ==
               NV_GPU_TRANSITION_APPLY);
        EXPECT(action == (NvU32)raw);
    }

    EXPECT(nv_gpu_transition_validate_initial_action(-1, &action) ==
           NV_GPU_TRANSITION_REJECT_ACTION);
    EXPECT(action == NV_GPU_TRANSITION_ACTION_DEFAULT);
    EXPECT(nv_gpu_transition_validate_initial_action(3, &action) ==
           NV_GPU_TRANSITION_REJECT_ACTION);
    EXPECT(action == NV_GPU_TRANSITION_ACTION_DEFAULT);
    EXPECT(nv_gpu_transition_validate_initial_action(2147483647, &action) ==
           NV_GPU_TRANSITION_REJECT_ACTION);
    EXPECT(action == NV_GPU_TRANSITION_ACTION_DEFAULT);

    EXPECT(nv_gpu_transition_validate_iterator_action(
               NV_GPU_TRANSITION_ACTION_DEFAULT, &action) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT(nv_gpu_transition_validate_iterator_action(
               NV_GPU_TRANSITION_ACTION_BYPASS, &action) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT(nv_gpu_transition_validate_iterator_action(
               NV_GPU_TRANSITION_ACTION_ENTER_LOOP, &action) ==
           NV_GPU_TRANSITION_REJECT_ACTION);
    EXPECT(action == NV_GPU_TRANSITION_ACTION_DEFAULT);
}

static void test_prefetch_action_region_routing(void)
{
    EXPECT(nv_gpu_transition_prefetch_initial_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_INITIAL_NATIVE);
    EXPECT(nv_gpu_transition_prefetch_initial_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_INITIAL_BYPASS);
    EXPECT(nv_gpu_transition_prefetch_initial_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS,
               NV_GPU_TRANSITION_REJECT_RANGE) == NV_GPU_PREFETCH_INITIAL_NATIVE);
    EXPECT(nv_gpu_transition_prefetch_initial_effect(
               NV_GPU_TRANSITION_ACTION_ENTER_LOOP,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_INITIAL_ITERATE);
    EXPECT(nv_gpu_transition_prefetch_initial_effect(
               99, NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_INITIAL_NATIVE);

    EXPECT(nv_gpu_transition_prefetch_iterator_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_ITERATOR_IGNORE);
    EXPECT(nv_gpu_transition_prefetch_iterator_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_ITERATOR_COMMIT);
    EXPECT(nv_gpu_transition_prefetch_iterator_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS,
               NV_GPU_TRANSITION_REJECT_RANGE) == NV_GPU_PREFETCH_ITERATOR_IGNORE);
    EXPECT(nv_gpu_transition_prefetch_iterator_effect(
               NV_GPU_TRANSITION_ACTION_ENTER_LOOP,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PREFETCH_ITERATOR_IGNORE);
}

static void test_prefetch_region_and_width(void)
{
    nv_gpu_prefetch_decision_t decision = { 0 };
    nv_gpu_transition_region_t region;
    const NvU64 type_outer = 65535;

    nv_gpu_transition_record_prefetch(&decision, 10, 10);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT((region.first == 0) && (region.outer == 0));

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 0, 0);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_APPLY);

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 10, 11);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT((region.first == 10) && (region.outer == 11));

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 20, 10);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 9, 11);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 10, 21);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 10, 20);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT((region.first == 10) && (region.outer == 20));

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 10, type_outer + 1);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer,
                                             &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 10, ~(NvU64)0);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20,
                                             512, type_outer, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    decision = (nv_gpu_prefetch_decision_t){ 0 };
    nv_gpu_transition_record_prefetch(&decision, 10, 513);
    EXPECT(nv_gpu_transition_validate_region(&decision, 10, 20, 512,
                                             type_outer, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);
}

static void test_prefetch_translation(void)
{
    nv_gpu_transition_region_t region;

    EXPECT(nv_gpu_transition_translate_region(10, 4, 5, 2, 20, 512, 65535,
                                              &region) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT((region.first == 12) && (region.outer == 13));

    EXPECT(nv_gpu_transition_translate_region(10, 2, 12, 2, 20, 512, 65535,
                                              &region) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT((region.first == 10) && (region.outer == 20));

    EXPECT(nv_gpu_transition_translate_region(1, 0, 1, 2, 20, 512, 65535,
                                              &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT((region.first == 0) && (region.outer == 0));

    EXPECT(nv_gpu_transition_translate_region(~(NvU64)0 - 1, 2, 3, 0,
                                              ~(NvU64)0, ~(NvU64)0,
                                              ~(NvU64)0, &region) ==
           NV_GPU_TRANSITION_REJECT_RANGE);
    EXPECT((region.first == 0) && (region.outer == 0));
}

static void test_pmm_attempt_latching(void)
{
    const nv_gpu_pmm_snapshot_t snapshot = { 1, 2, 3,
                                             NV_GPU_PMM_DESTINATION_USED };
    nv_gpu_pmm_request_t request = { 0 };

    EXPECT(nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request) ==
           NV_GPU_TRANSITION_NOOP_DEFAULT);

    EXPECT(nv_gpu_transition_record_pmm(&request, 99,
                                        NV_GPU_PMM_POSITION_HEAD) ==
           NV_GPU_TRANSITION_APPLY);
    EXPECT(request.attempted == NV_TRUE);
    EXPECT(nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    EXPECT(nv_gpu_transition_record_pmm(&request, 99,
                                        NV_GPU_PMM_POSITION_HEAD) ==
           NV_GPU_TRANSITION_NOOP_REPEAT);
    EXPECT(nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    request = (nv_gpu_pmm_request_t){ 0 };
    nv_gpu_transition_record_pmm(&request, NV_GPU_PMM_DESTINATION_USED,
                                 NV_GPU_PMM_POSITION_HEAD);
    EXPECT(nv_gpu_transition_record_pmm(&request, 99,
                                        NV_GPU_PMM_POSITION_HEAD) ==
           NV_GPU_TRANSITION_NOOP_CONFLICT);
    EXPECT(nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request) ==
           NV_GPU_TRANSITION_NOOP_CONFLICT);

    request = (nv_gpu_pmm_request_t){ 0 };
    nv_gpu_transition_record_pmm(&request, NV_GPU_PMM_DESTINATION_USED,
                                 ~(NvU64)0);
    EXPECT(request.attempted == NV_TRUE);
    EXPECT(nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request) ==
           NV_GPU_TRANSITION_REJECT_RANGE);

    request = (nv_gpu_pmm_request_t){ 0 };
    nv_gpu_transition_record_pmm(&request, 99, NV_GPU_PMM_POSITION_HEAD);
    EXPECT(nv_gpu_transition_record_pmm(&request,
                                        NV_GPU_PMM_DESTINATION_USED,
                                        NV_GPU_PMM_POSITION_HEAD) ==
           NV_GPU_TRANSITION_NOOP_CONFLICT);
    EXPECT(nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request) ==
           NV_GPU_TRANSITION_NOOP_CONFLICT);
}

static void expect_pmm_preserve_for_both_actions(
    enum nv_gpu_transition_result result)
{
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT, result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS, result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);
}

static void test_pmm_rejected_attempt_sequences(void)
{
    const nv_gpu_pmm_snapshot_t snapshot = { 1, 2, 3,
                                             NV_GPU_PMM_DESTINATION_USED };
    nv_gpu_pmm_request_t request = { 0 };
    enum nv_gpu_transition_result result;

    nv_gpu_transition_record_pmm(&request, 99, NV_GPU_PMM_POSITION_HEAD);
    result = nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request);
    EXPECT(result == NV_GPU_TRANSITION_REJECT_RANGE);
    expect_pmm_preserve_for_both_actions(result);

    EXPECT(nv_gpu_transition_record_pmm(&request, 99,
                                        NV_GPU_PMM_POSITION_HEAD) ==
           NV_GPU_TRANSITION_NOOP_REPEAT);
    result = nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request);
    EXPECT(result == NV_GPU_TRANSITION_REJECT_RANGE);
    expect_pmm_preserve_for_both_actions(result);

    request = (nv_gpu_pmm_request_t){ 0 };
    nv_gpu_transition_record_pmm(&request, NV_GPU_PMM_DESTINATION_USED,
                                 NV_GPU_PMM_POSITION_HEAD);
    nv_gpu_transition_record_pmm(&request, 99, NV_GPU_PMM_POSITION_HEAD);
    result = nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request);
    EXPECT(result == NV_GPU_TRANSITION_NOOP_CONFLICT);
    expect_pmm_preserve_for_both_actions(result);

    request = (nv_gpu_pmm_request_t){ 0 };
    nv_gpu_transition_record_pmm(&request, 99, NV_GPU_PMM_POSITION_HEAD);
    nv_gpu_transition_record_pmm(&request, NV_GPU_PMM_DESTINATION_USED,
                                 NV_GPU_PMM_POSITION_HEAD);
    result = nv_gpu_transition_validate_pmm(&snapshot, &snapshot, &request);
    EXPECT(result == NV_GPU_TRANSITION_NOOP_CONFLICT);
    expect_pmm_preserve_for_both_actions(result);
}

static void test_pmm_snapshot_and_routing(void)
{
    const nv_gpu_pmm_snapshot_t expected = { 1, 2, 3,
                                             NV_GPU_PMM_DESTINATION_USED };
    nv_gpu_pmm_snapshot_t current = expected;
    nv_gpu_pmm_request_t request = { 0 };
    enum nv_gpu_transition_result result;

    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT,
               NV_GPU_TRANSITION_NOOP_DEFAULT) == NV_GPU_PMM_ACCESS_NATIVE);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS,
               NV_GPU_TRANSITION_NOOP_DEFAULT) == NV_GPU_PMM_ACCESS_PRESERVE);

    nv_gpu_transition_record_pmm(&request, NV_GPU_PMM_DESTINATION_UNUSED,
                                 NV_GPU_PMM_POSITION_TAIL);
    result = nv_gpu_transition_validate_pmm(&expected, &current, &request);
    EXPECT(result == NV_GPU_TRANSITION_APPLY);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT, result) ==
           NV_GPU_PMM_ACCESS_COMMIT);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS, result) ==
           NV_GPU_PMM_ACCESS_COMMIT);
    EXPECT(nv_gpu_transition_pmm_activate_effect(result) ==
           NV_GPU_PMM_ACCESS_COMMIT);

    current.generation++;
    result = nv_gpu_transition_validate_pmm(&expected, &current, &request);
    EXPECT(result == NV_GPU_TRANSITION_NOOP_STALE);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT, result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);
    EXPECT(nv_gpu_transition_pmm_access_effect(99, result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);

    current = expected;
    current.owner_id = 9;
    result = nv_gpu_transition_validate_pmm(&expected, &current, &request);
    EXPECT(result == NV_GPU_TRANSITION_REJECT_IDENTITY);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS, result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);

    current = expected;
    current.root_id = 9;
    result = nv_gpu_transition_validate_pmm(&expected, &current, &request);
    EXPECT(result == NV_GPU_TRANSITION_REJECT_IDENTITY);
    EXPECT(nv_gpu_transition_pmm_activate_effect(result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);

    current = expected;
    current.source = NV_GPU_PMM_DESTINATION_UNUSED;
    result = nv_gpu_transition_validate_pmm(&expected, &current, &request);
    EXPECT(result == NV_GPU_TRANSITION_NOOP_STALE);
    EXPECT(nv_gpu_transition_pmm_activate_effect(result) ==
           NV_GPU_PMM_ACCESS_PRESERVE);

    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_ENTER_LOOP,
               NV_GPU_TRANSITION_APPLY) == NV_GPU_PMM_ACCESS_PRESERVE);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_DEFAULT,
               NV_GPU_TRANSITION_NOOP_CONFLICT) == NV_GPU_PMM_ACCESS_PRESERVE);
    EXPECT(nv_gpu_transition_pmm_access_effect(
               NV_GPU_TRANSITION_ACTION_BYPASS,
               NV_GPU_TRANSITION_REJECT_RANGE) == NV_GPU_PMM_ACCESS_PRESERVE);
}

struct test_case
{
    const char *name;
    void (*run)(void);
};

static const struct test_case test_cases[] = {
    { "scheduler-presence-minimum", test_scheduler_presence_and_minimum },
    { "interleave-low-range", test_interleave_low_and_range },
    { "scheduler-identity-phase", test_scheduler_identity_and_phase },
    { "scheduler-repeat-conflict", test_scheduler_repeat_and_conflict },
    { "scheduler-independent-fields", test_scheduler_independent_fields },
    { "prefetch-action", test_prefetch_action },
    { "prefetch-action-region-routing", test_prefetch_action_region_routing },
    { "prefetch-region-width", test_prefetch_region_and_width },
    { "prefetch-translation", test_prefetch_translation },
    { "pmm-attempt-latching", test_pmm_attempt_latching },
    { "pmm-rejected-attempt-sequences", test_pmm_rejected_attempt_sequences },
    { "pmm-snapshot-routing", test_pmm_snapshot_and_routing },
};

int main(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i)
    {
        test_cases[i].run();
        printf("PASS %s\n", test_cases[i].name);
    }

    printf("PASS all: %u cases, %u assertions\n", i, assertion_count);
    return EXIT_SUCCESS;
}
