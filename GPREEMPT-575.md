# GPreempt 575 owned-context compatibility transport

This port provides the minimal query/timeslice transport needed by the
GPreempt CUDA artifact at upstream revision `249ee3e`. It does not copy the
original patch's global `g_clientOSInfo`, fixed eight-channel test,
Ampere-only channel class, query-to-free fallthrough, or global replacement
of `Nv04ControlWithSecInfo`.

## ABI

The control-device escape remains `NV_ESC_RM_QUERY_GROUP = 0x60`, carried in
the original 32-byte `NVOS54_PARAMETERS` envelope (`0xc0204660` on x86-64).
All input fields must be initialized. A well-sized request returns its RM
status in `status`; a bad outer size/device fails the ioctl. User callers
must check both syscall and RM status.

| Operation | flags | cmd | Input hClient / hObject | Payload |
|---|---:|---:|---|---|
| QUERY | 0 | 0 | creator TID / 0 | writable `NV2080_CTRL_FIFO_DISABLE_CHANNELS_PARAMS`, exact size |
| Version 1 SET_TIMESLICE | `0x00010001` | `0xa06c0103` | queried client / TSG handle | readable 8-byte `NVA06C_CTRL_TIMESLICE_PARAMS` |

QUERY returns one owned GR TSG's client/object handles and its 1–64 child
channel handles. No match, multiple matching GR TSGs, an empty channel list,
overflow, or a failed output copy is an error; no partial selection is
published. Multiple TSG enumeration is deliberately not implemented. The
creator TID uses the driver's host TID convention, as in the original patch;
the first runtime canary is restricted to the host PID namespace.

SET_TIMESLICE accepts only the two original algorithm values, 1 us and
1,000,000 us. It authorizes only a currently owned GR object on a GSP client
and executes the fixed RM SET_TIMESLICE control. Other commands, flags,
versions, sizes, values, clients, and object classes are rejected. General
`NV_ESC_RM_CONTROL` still uses `Nv04ControlWithSecInfo` unchanged.

## Ownership and lifetime

Both operations obtain a retained `osGetPidInfo()` object for the calling
TGID and require pointer identity with the RM user client's retained
`pOsPidInfo`. Numeric PID/TID equality and file-descriptor identity are not
authorization. This rejects foreign processes, kernel clients, and PID reuse;
a separate control FD in the same process works without a global OSInfo.
The creator TID only selects an object after its process ownership is proven.

The transport initializes thread state, takes the API write lock, then the
GPU locks. Client/group iteration and the restricted control occur under
those same locks. User timeslice data is copied once before locking; query
output is a zero-initialized bounded snapshot copied after unlocking. Failed
copy operations propagate their status, and query handles remain zero.

## Verification boundary

`make -C kernel-open/tests/gpreempt-transport test` tests the actual request,
owner-identity, value, and channel-bound helpers. The nearby transition
validator test remains applicable. A successful CPU driver build/test does
not show that a GSP timeslice request has been accepted or applied on hardware.

Before performance: check legacy QUERY with the real two-context client;
verify different creator TIDs select the intended LC/BE GR TSG; exercise bad
sizes/pointers/flags, foreign-process handles, non-GR handles, ambiguity, and
creation/destruction; verify both timeslice controls' real GSP status and
cleanup. Preserve full GPreempt's GDRCopy/two-block-kernel/hint behavior in
the userspace port. Shadow GET/host fields alone are not hardware evidence.
No installation, module load, or GPU run is part of this CPU-only port.

## Runlist identity and diagnostic completion hook

`grpID` is allocated by a per-runlist `CHID_MGR`: GR and CE groups may have
the same numeric ID. The destroy context appends `runlist_id` and the actual
RM `engine_type`, preserving `tsg_id` at offset zero. Consumers that retain
TSG state must use `(runlist_id, tsg_id)` and appropriate engine scoping; a
numeric TSG ID alone is not a unique GPU-wide identity. The CPU tests check
the appended 16-byte layout without changing the existing first field.

Core RM routines are built notrace. Instead of bypassing that restriction,
the normal Kbuild-instrumented `nv_gpu_sched_gsp_control_complete` provides
an observation-only attachment point. `rpcRmApiControl_GSP` calls it only
after a real RPC wait for SET_TIMESLICE or SET_INTERLEAVE_LEVEL. Its 48-byte
record preserves the original input value/size before serialization and
records actual transport status and firmware response status. Firmware
status is explicitly invalid if transport failed. Cache hits and pre-RPC
rejections do not emit completion events. The hook neither dispatches a
policy nor changes driver state or the existing return/error path.

This record is a GSP completion status, not the final host-deserialization
return or a measurement of physical scheduling quantum. Userspace must
still check its control ioctl/RM status. CPU tests cover successful firmware
status, firmware rejection, and failed transport without a valid response.
An actual attachment and correlated two-context canary remain required.

## Persistent timeslice-control policy

The real completion canary found that CUDA subsequently submitted 2,048 us
after successful BPF initialization. `on_timeslice_control` is an optional,
appended scheduler callback inside the existing authorized and locked
`kchangrpapiCtrlCmdSetTimeslice_IMPL` GSP branch, before its original RPC.
Old policies omit this member and retain the original payload and path.

Its read-only 40-byte context contains the actual RM client/object, grpID,
runlist, engine, GPU instance, incoming timeslice and phase. A trusted-argument
kfunc records a proposed timeslice; it does not sleep, call RM, write the
context or change any object. Once the RCU callback returns, the driver checks
the identity/phase snapshot, conflicting requests, hardware minimum and the
bounded policy range 1–1,000,000 us. Invalid policy proposals return an error
before RPC. Same-value repeated proposals are idempotent.

Only incoming values in that same bounded domain are policy-eligible. Other
values use the original native path unchanged: BPF cannot turn an out-of-range
request into a valid one. This is not a claim to enumerate every possible
firmware semantic rejection. Existing class, parameter, permission and object
checks remain in place. With no proposal there is no validation-induced error
or payload change. The existing physical RPC, returned status and update of
host bookkeeping only after successful RPC remain unchanged; there is no new
generic control escape or sleeping executor.

CPU coverage checks the input layout, native 2,048-us no-op, LC/BE proposals,
every identity field, stale phase, lower/upper bounds, minimum timeslice,
repeat/conflict and preservation of invalid incoming values. This build-only
checkpoint still requires a newly loaded module and real original/BPF canaries
with the final GSP value checked before kernel execution.
