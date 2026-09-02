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
