/* SPDX-License-Identifier: MIT */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "nv-gpu-stale-state-v1.h"

static unsigned int assertion_count;

#define EXPECT(condition)                                                        \
    do {                                                                         \
        ++assertion_count;                                                       \
        if (!(condition)) {                                                      \
            fprintf(stderr, "%s:%d: expectation failed: %s\n",                 \
                    __FILE__, __LINE__, #condition);                             \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    } while (0)

static nv_gpu_stale_state_v1_snapshot_t snapshot(NvU32 phase)
{
    nv_gpu_stale_state_v1_snapshot_t value = {
        .sequence = 7,
        .source_mono_ns = 100,
        .published_mono_ns = 120,
        .phase = phase,
    };
    return value;
}

static void test_abi(void)
{
    EXPECT(NV_GPU_STALE_STATE_V1_ABI_VERSION == 1);
    EXPECT(sizeof(nv_gpu_stale_state_v1_snapshot_t) == 32);
    EXPECT(offsetof(nv_gpu_stale_state_v1_snapshot_t, sequence) == 0);
    EXPECT(offsetof(nv_gpu_stale_state_v1_snapshot_t, source_mono_ns) == 8);
    EXPECT(offsetof(nv_gpu_stale_state_v1_snapshot_t, published_mono_ns) == 16);
    EXPECT(offsetof(nv_gpu_stale_state_v1_snapshot_t, phase) == 24);
    EXPECT(sizeof(nv_gpu_stale_state_v1_decision_t) == 24);
}

static void test_dense_and_sparse(void)
{
    nv_gpu_stale_state_v1_snapshot_t dense =
        snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    nv_gpu_stale_state_v1_snapshot_t sparse =
        snapshot(NV_GPU_STALE_STATE_V1_PHASE_SPARSE);
    nv_gpu_stale_state_v1_decision_t decision = {0};

    EXPECT(nv_gpu_stale_state_v1_choose(&dense, 170, &decision) ==
           NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX);
    EXPECT(decision.snapshot_sequence == 7);
    EXPECT(decision.snapshot_phase == NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    EXPECT(decision.decision_age_ns == 70);
    EXPECT(decision.action == NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX);

    decision = (nv_gpu_stale_state_v1_decision_t){0};
    EXPECT(nv_gpu_stale_state_v1_choose(&sparse, 180, &decision) ==
           NV_GPU_STALE_STATE_V1_ACTION_DISCARD_PREFETCH);
    EXPECT(decision.snapshot_phase == NV_GPU_STALE_STATE_V1_PHASE_SPARSE);
    EXPECT(decision.decision_age_ns == 80);
}

static void test_rejections(void)
{
    nv_gpu_stale_state_v1_snapshot_t value =
        snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    nv_gpu_stale_state_v1_decision_t decision = {0};

    EXPECT(nv_gpu_stale_state_v1_choose(NULL, 170, &decision) ==
           NV_GPU_STALE_STATE_V1_ACTION_REJECT);
    EXPECT(nv_gpu_stale_state_v1_choose(&value, 170, NULL) ==
           NV_GPU_STALE_STATE_V1_ACTION_REJECT);
    value.sequence = 0;
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 170));
    value = snapshot(NV_GPU_STALE_STATE_V1_PHASE_INVALID);
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 170));
    value = snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    value.reserved = 1;
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 170));
    value = snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    value.source_mono_ns = 0;
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 170));
    value = snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    value.published_mono_ns = 0;
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 170));
    value = snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    value.published_mono_ns = 99;
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 170));
    value = snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);
    EXPECT(!nv_gpu_stale_state_v1_snapshot_valid(&value, 119));
}

static void test_publication_order(void)
{
    nv_gpu_stale_state_v1_snapshot_t current_snapshot =
        snapshot(NV_GPU_STALE_STATE_V1_PHASE_DENSE);

    EXPECT(nv_gpu_stale_state_v1_publication_follows(NULL, 1, 100));
    EXPECT(!nv_gpu_stale_state_v1_publication_follows(NULL, 2, 100));
    EXPECT(nv_gpu_stale_state_v1_publication_follows(&current_snapshot, 8, 101));
    EXPECT(!nv_gpu_stale_state_v1_publication_follows(&current_snapshot, 9, 101));
    EXPECT(!nv_gpu_stale_state_v1_publication_follows(&current_snapshot, 8, 100));
    current_snapshot.sequence = ~(NvU64)0;
    EXPECT(!nv_gpu_stale_state_v1_publication_follows(&current_snapshot, 1, 101));
}

int main(void)
{
    test_abi();
    test_dense_and_sparse();
    test_rejections();
    test_publication_order();
    printf("stale_state_v1_test: %u assertions passed\n", assertion_count);
    return 0;
}
