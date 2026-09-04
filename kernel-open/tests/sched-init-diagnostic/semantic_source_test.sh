#!/bin/sh
set -eu

constructor_source=$1
hook_source=$2

fail()
{
    echo "sched-init diagnostic source check: $*" >&2
    exit 1
}

line_of()
{
    occurrence=$1
    pattern=$2
    file=$3
    grep -nF "$pattern" "$file" | sed -n "${occurrence}s/:.*//p"
}

expect_count()
{
    expected=$1
    pattern=$2
    file=$3
    actual=$(grep -cF "$pattern" "$file" || true)
    [ "$actual" -eq "$expected" ] ||
        fail "expected $expected occurrences of '$pattern' in $file, saw $actual"
}

expect_order()
{
    previous=0
    for current in "$@"
    do
        [ -n "$current" ] || fail "required source marker is absent"
        [ "$current" -gt "$previous" ] || fail "required source markers are out of order"
        previous=$current
    done
}

validator=$(line_of 1 "validation = nv_gpu_transition_validate_scheduler(" "$constructor_source")
validated_phase=$(line_of 1 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_VALIDATED;" "$constructor_source")
validated_emit=$(line_of 1 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source")

timeslice_setter=$(line_of 1 "rmStatus = kfifoChannelGroupSetTimeslice(" "$constructor_source")
timeslice_status=$(line_of 1 "schedDiagnostic.timeslice_native_status = rmStatus;" "$constructor_source")
timeslice_phase=$(line_of 1 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_NATIVE_RETURN;" "$constructor_source")
timeslice_emit=$(line_of 2 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source")
timeslice_branch=$(line_of 1 "NV_ASSERT_OK_OR_GOTO(rmStatus, rmStatus, failed);" "$constructor_source")
timeslice_commit=$(line_of 1 "bPolicyTimeslice = NV_TRUE;" "$constructor_source")

interleave_setter=$(line_of 1 "rmStatus = kchangrpSetInterleaveLevel(" "$constructor_source")
interleave_status=$(line_of 1 "schedDiagnostic.interleave_native_status = rmStatus;" "$constructor_source")
interleave_phase=$(line_of 2 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_NATIVE_RETURN;" "$constructor_source")
interleave_emit=$(line_of 3 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source")
interleave_branch=$(line_of 2 "NV_ASSERT_OK_OR_GOTO(rmStatus, rmStatus, failed);" "$constructor_source")
interleave_commit=$(line_of 1 "bPolicyInterleave = NV_TRUE;" "$constructor_source")

failed_label=$(line_of 1 "failed:" "$constructor_source")
failure_return_phase=$(line_of 1 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN;" "$constructor_source")
failure_emit=$(line_of 4 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source")
failure_disable=$(line_of 2 "bSchedDiagnosticActive = NV_FALSE;" "$constructor_source")
cleanup_destroy=$(line_of 1 "kchangrpDestroy(pGpu, pKernelChannelGroup);" "$constructor_source")

reserve=$(line_of 1 "ctxBufPoolReserve(pGpu, pKernelChannelGroup->pCtxBufPool" "$constructor_source")
lock_reacquire=$(line_of 1 "rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE, RM_LOCK_MODULES_FIFO);" "$constructor_source")
early_return_phase=$(line_of 2 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN;" "$constructor_source")
early_return_emit=$(line_of 5 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source")
early_return_disable=$(line_of 3 "bSchedDiagnosticActive = NV_FALSE;" "$constructor_source")
lock_return=$(line_of 1 "NV_ASSERT_OK_OR_RETURN(lockStatus);" "$constructor_source")
retry_failed=$(line_of 11 "goto failed;" "$constructor_source")

buffer_free=$(line_of 1 "portMemFree(bufInfoList);" "$constructor_source")
success_return_phase=$(line_of 3 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN;" "$constructor_source")
success_emit=$(line_of 6 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source")
constructor_return=$(line_of 1 "return rmStatus;" "$constructor_source")

expect_order "$validator" "$validated_phase" "$validated_emit"
expect_order "$timeslice_setter" "$timeslice_status" "$timeslice_phase" \
             "$timeslice_emit" "$timeslice_branch" "$timeslice_commit"
expect_order "$interleave_setter" "$interleave_status" "$interleave_phase" \
             "$interleave_emit" "$interleave_branch" "$interleave_commit"
expect_order "$failed_label" "$failure_return_phase" "$failure_emit" \
             "$failure_disable" "$cleanup_destroy"
expect_order "$reserve" "$lock_reacquire" "$early_return_phase" \
             "$early_return_emit" "$early_return_disable" "$lock_return" \
             "$retry_failed"
expect_order "$buffer_free" "$success_return_phase" "$success_emit" \
             "$constructor_return"

expect_count 6 "nv_gpu_sched_init_diagnostic(&schedDiagnostic);" "$constructor_source"
expect_count 3 "schedDiagnostic.phase = NV_GPU_SCHED_INIT_DIAGNOSTIC_CONSTRUCTOR_RETURN;" "$constructor_source"
expect_count 2 "NV_ASSERT_OK_OR_GOTO(rmStatus, rmStatus, failed);" "$constructor_source"
expect_count 1 "NV_ASSERT_OK_OR_RETURN(lockStatus);" "$constructor_source"
expect_count 1 "noinline void nv_gpu_sched_init_diagnostic(" "$hook_source"

hook_start=$(line_of 1 "noinline void nv_gpu_sched_init_diagnostic(" "$hook_source")
hook_barrier=$(tail -n +"$hook_start" "$hook_source" | grep -nF "NV_SCHED_HOOK_BARRIER();" | sed -n '1s/:.*//p')
[ -n "$hook_barrier" ] || fail "barrier-only hook has no compiler barrier"

echo "sched_init_diagnostic_source: placement checks passed (CPU only)"
