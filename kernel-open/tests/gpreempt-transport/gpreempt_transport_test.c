/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include "nv-gpreempt-transport.h"

static unsigned int assertions;
#define EXPECT(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void test_query_envelope(void)
{
    const NvU32 channelsSize = 536;
    EXPECT(nv_gpreempt_operation(0, 0, 42, 0, NV_TRUE, channelsSize,
                                channelsSize) == NV_GPREEMPT_QUERY);
    EXPECT(nv_gpreempt_operation(0, 0, 0, 0, NV_TRUE, channelsSize,
                                channelsSize) == NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(0, 1, 42, 0, NV_TRUE, channelsSize,
                                channelsSize) == NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(0, 0, 42, 1, NV_TRUE, channelsSize,
                                channelsSize) == NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(0, 0, 42, 0, NV_FALSE, channelsSize,
                                channelsSize) == NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(0, 0, 42, 0, NV_TRUE, 0,
                                channelsSize) == NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(0, 0, 42, 0, NV_TRUE, channelsSize - 1,
                                channelsSize) == NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(0, 0, 42, 0, NV_TRUE, channelsSize + 1,
                                channelsSize) == NV_GPREEMPT_INVALID);
}

static void test_narrow_control(void)
{
    const NvU32 flags = NV_GPREEMPT_V1_SET_TIMESLICE;
    const NvU32 cmd = NV_GPREEMPT_SET_TIMESLICE_CMD;
    EXPECT(nv_gpreempt_operation(flags, cmd, 42, 7, NV_TRUE, 8, 536) ==
           NV_GPREEMPT_SET_TIMESLICE);
    EXPECT(nv_gpreempt_operation(flags, cmd, 0, 7, NV_TRUE, 8, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(flags, cmd, 42, 0, NV_TRUE, 8, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(flags, cmd, 42, 7, NV_FALSE, 8, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(flags + 1, cmd, 42, 7, NV_TRUE, 8, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(flags + 0x10000, cmd, 42, 7, NV_TRUE, 8, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(flags, cmd + 2, 42, 7, NV_TRUE, 8, 536) ==
           NV_GPREEMPT_INVALID); /* PREEMPT is not authorized here. */
    EXPECT(nv_gpreempt_operation(flags, cmd, 42, 7, NV_TRUE, 7, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_operation(flags, cmd, 42, 7, NV_TRUE, 9, 536) ==
           NV_GPREEMPT_INVALID);
    EXPECT(nv_gpreempt_timeslice_allowed(1));
    EXPECT(nv_gpreempt_timeslice_allowed(1000000));
    EXPECT(!nv_gpreempt_timeslice_allowed(0));
    EXPECT(!nv_gpreempt_timeslice_allowed(2));
    EXPECT(!nv_gpreempt_timeslice_allowed(999999));
    EXPECT(!nv_gpreempt_timeslice_allowed(1000001));
    EXPECT(!nv_gpreempt_timeslice_allowed(~(NvU64)0));
}

static void test_owner_identity_and_bounds(void)
{
    int owner, foreign;
    EXPECT(nv_gpreempt_owned_user(&owner, &owner, NV_TRUE));
    EXPECT(!nv_gpreempt_owned_user(&owner, &foreign, NV_TRUE));
    EXPECT(!nv_gpreempt_owned_user(&owner, &owner, NV_FALSE));
    EXPECT(!nv_gpreempt_owned_user(NULL, NULL, NV_TRUE));
    EXPECT(!nv_gpreempt_owned_user(&owner, NULL, NV_TRUE));
    for (NvU32 i = 0; i < NV_GPREEMPT_MAX_CHANNELS; ++i)
        EXPECT(nv_gpreempt_channel_slot_available(i));
    EXPECT(!nv_gpreempt_channel_slot_available(64));
    EXPECT(!nv_gpreempt_channel_slot_available(65));
    EXPECT(!nv_gpreempt_channel_slot_available(~(NvU32)0));
}

static void test_unique_selection(void)
{
    NvU32 matches = 0;
    EXPECT(!nv_gpreempt_query_complete(matches, 8));
    EXPECT(nv_gpreempt_add_unique_match(&matches));
    EXPECT(matches == 1);
    EXPECT(!nv_gpreempt_query_complete(matches, 0));
    EXPECT(nv_gpreempt_query_complete(matches, 1));
    EXPECT(nv_gpreempt_query_complete(matches, 64));
    EXPECT(!nv_gpreempt_query_complete(matches, 65));
    EXPECT(!nv_gpreempt_add_unique_match(&matches));
    EXPECT(matches == 2);
    EXPECT(!nv_gpreempt_query_complete(matches, 8));
    EXPECT(!nv_gpreempt_add_unique_match(&matches));
    EXPECT(matches == 2);
    matches = ~(NvU32)0;
    EXPECT(!nv_gpreempt_add_unique_match(&matches));
    EXPECT(matches == 2);
}

int main(void)
{
    test_query_envelope();
    test_narrow_control();
    test_owner_identity_and_bounds();
    test_unique_selection();
    printf("gpreempt_transport: 4 cases, %u assertions passed (CPU only)\n", assertions);
    return 0;
}
