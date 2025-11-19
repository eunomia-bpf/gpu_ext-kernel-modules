# eBPF Hooks Design for NVIDIA UVM Driver
## struct_ops Style Callbacks for Policy Modification

**Analysis Date**: 2025-11-14
**Repository**: `/home/yunwei37/open-gpu-kernel-modules`
**Branch**: `uvm-print-test`
**Custom Build**: `NVIDIA UVM: Custom build by yunwei37 - UVM driver loading (version 575.57.08)`

---

## Executive Summary

This document identifies **20+ strategic hook points** in the NVIDIA UVM driver where eBPF programs can be inserted using the struct_ops framework to modify driver behavior as policy. These hooks enable runtime-programmable policies for:

- **Fault servicing decisions** (when/how to service GPU page faults)
- **Replay policies** (when to resume stalled warps)
- **Migration strategies** (where to place memory pages)
- **Thrashing detection** (how to detect and mitigate memory thrashing)
- **WIC integration** (producer-consumer communication policies)

**Key Insight**: The UVM driver already has three extensibility frameworks perfect for eBPF:
1. **HAL (Hardware Abstraction Layer)** - 60+ function pointers for GPU operations
2. **Perf Module System** - Event-driven callback framework
3. **Module Parameters** - Runtime-tunable policy knobs

**Legend**:
- `[-]` = Current hardcoded policy (should be removed/made optional)
- `[+]` = eBPF hook insertion point (new policy mechanism)
- `[=]` = Unchanged context code

---

## Table of Contents

1. [eBPF struct_ops Overview](#1-ebpf-struct_ops-overview)
2. [Policy Decision Points (Diff-Style Analysis)](#2-policy-decision-points-diff-style-analysis)
3. [Existing Callback Structures](#3-existing-callback-structures)
4. [Safe Hook Insertion Points](#4-safe-hook-insertion-points)
5. [eBPF Program Interface Design](#5-ebpf-program-interface-design)
6. [WIC Integration via eBPF](#6-wic-integration-via-ebpf)
7. [Implementation Roadmap](#7-implementation-roadmap)

---

## 1. eBPF struct_ops Overview

### What is struct_ops?

eBPF struct_ops allows attaching eBPF programs to function pointers in kernel structures. Examples:
- **TCP Congestion Control**: `struct tcp_congestion_ops`
- **Schedulers**: `struct sched_ext_ops`
- **HID Drivers**: `struct hid_bpf_ops`

### Why struct_ops for UVM?

The UVM driver has **~100 function pointers** across HAL and perf modules. Converting these to eBPF struct_ops enables:
- **Runtime policy updates** without driver recompilation
- **Per-process policies** via BPF maps
- **Safe experimentation** with fault handling strategies
- **Dynamic WIC implementation** without permanent driver changes

---

## 2. Policy Decision Points (Diff-Style Analysis)

This section uses diff notation to show **exactly where policies are enforced** and **where eBPF hooks should replace them**.

### 2.1 POLICY #1: Fault Batch Size

**File**: `kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c:153-165`

**Current Code** (hardcoded policy):
```c
static NV_STATUS fault_buffer_init_replayable_faults(uvm_parent_gpu_t *parent_gpu)
{
    uvm_replayable_fault_buffer_t *replayable_faults = &parent_gpu->fault_buffer.replayable;
    uvm_fault_service_batch_context_t *batch_context = &replayable_faults->batch_service_context;

[=] // Check provided module parameter value
[-] parent_gpu->fault_buffer.max_batch_size = max(uvm_perf_fault_batch_count,
[-]                                               (NvU32)UVM_PERF_FAULT_BATCH_COUNT_MIN);
[-] parent_gpu->fault_buffer.max_batch_size = min(parent_gpu->fault_buffer.max_batch_size,
[-]                                               replayable_faults->max_faults);
[=]
[=] if (parent_gpu->fault_buffer.max_batch_size != uvm_perf_fault_batch_count) {
[=]     UVM_INFO_PRINT("Invalid uvm_perf_fault_batch_count value on GPU %s: %u...",
[=]                    uvm_parent_gpu_name(parent_gpu), uvm_perf_fault_batch_count, ...);
[=] }
```

**eBPF Hook Version**:
```c
static NV_STATUS fault_buffer_init_replayable_faults(uvm_parent_gpu_t *parent_gpu)
{
    uvm_replayable_fault_buffer_t *replayable_faults = &parent_gpu->fault_buffer.replayable;
    uvm_fault_service_batch_context_t *batch_context = &replayable_faults->batch_service_context;

[=] // Check provided module parameter value
[+] // eBPF POLICY HOOK #1: Dynamic batch size selection
[+] if (parent_gpu->bpf_ops && parent_gpu->bpf_ops->get_batch_size) {
[+]     struct uvm_bpf_batch_context ctx = {
[+]         .gpu = parent_gpu,
[+]         .max_faults = replayable_faults->max_faults,
[+]         .default_batch_size = uvm_perf_fault_batch_count,
[+]     };
[+]     parent_gpu->fault_buffer.max_batch_size =
[+]         parent_gpu->bpf_ops->get_batch_size(&ctx);
[+] } else {
[+]     // Fallback to static policy
        parent_gpu->fault_buffer.max_batch_size = max(uvm_perf_fault_batch_count,
                                                      (NvU32)UVM_PERF_FAULT_BATCH_COUNT_MIN);
        parent_gpu->fault_buffer.max_batch_size = min(parent_gpu->fault_buffer.max_batch_size,
                                                      replayable_faults->max_faults);
[+] }
```

**eBPF Program Example**:
```c
SEC("struct_ops/uvm_get_batch_size")
u32 bpf_adaptive_batch_size(struct uvm_bpf_batch_context *ctx)
{
    // POLICY: Adaptive batch sizing based on GPU load
    u32 fill_level = ctx->current_fill_level;

    if (fill_level > 1000)
        return 512;  // High load → large batches
    else if (fill_level > 100)
        return ctx->default_batch_size;  // Normal load
    else
        return 64;   // Low load → small batches for latency
}
```

---

### 2.2 POLICY #2: Replay Policy Selection

**File**: `kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c:2986-3020`

**Current Code** (hardcoded policy):
```c
void uvm_parent_gpu_service_replayable_faults(uvm_parent_gpu_t *parent_gpu)
{
    // ... service faults ...

[=] if (batch_context->fatal_va_space) {
[=]     // Handle fatal faults
[=]     status = cancel_faults_precise(batch_context);
[=]     // ...
[=] }
[=]
[-] // HARDCODED POLICY: Check static replay_policy variable
[-] if (replayable_faults->replay_policy == UVM_PERF_FAULT_REPLAY_POLICY_BATCH) {
[-]     status = push_replay_on_parent_gpu(parent_gpu,
[-]                                       UVM_FAULT_REPLAY_TYPE_START,
[-]                                       batch_context);
[-]     ++num_replays;
[-] }
[-] else if (replayable_faults->replay_policy == UVM_PERF_FAULT_REPLAY_POLICY_BATCH_FLUSH) {
[-]     uvm_gpu_buffer_flush_mode_t flush_mode = UVM_GPU_BUFFER_FLUSH_MODE_CACHED_PUT;
[-]
[-]     // NESTED POLICY: Flush mode based on duplicate ratio
[-]     if (batch_context->num_duplicate_faults * 100 >
[-]         batch_context->num_cached_faults * replayable_faults->replay_update_put_ratio) {
[-]         flush_mode = UVM_GPU_BUFFER_FLUSH_MODE_UPDATE_PUT;
[-]     }
[-]
[-]     status = fault_buffer_flush_locked(parent_gpu, NULL, flush_mode,
[-]                                       UVM_FAULT_REPLAY_TYPE_START,
[-]                                       batch_context);
[-]     ++num_replays;
[-] }
```

**eBPF Hook Version**:
```c
void uvm_parent_gpu_service_replayable_faults(uvm_parent_gpu_t *parent_gpu)
{
    // ... service faults ...

[=] if (batch_context->fatal_va_space) {
[=]     // Handle fatal faults
[=]     status = cancel_faults_precise(batch_context);
[=]     // ...
[=] }
[=]
[+] // eBPF POLICY HOOK #2: Replay policy decision
[+] if (parent_gpu->bpf_ops && parent_gpu->bpf_ops->get_replay_policy) {
[+]     struct uvm_bpf_replay_context ctx = {
[+]         .parent_gpu = parent_gpu,
[+]         .batch_context = batch_context,
[+]         .num_faults = batch_context->num_cached_faults,
[+]         .num_duplicates = batch_context->num_duplicate_faults,
[+]     };
[+]
[+]     enum uvm_replay_action action = parent_gpu->bpf_ops->get_replay_policy(&ctx);
[+]
[+]     switch (action) {
[+]     case UVM_REPLAY_ACTION_BATCH:
[+]         status = push_replay_on_parent_gpu(parent_gpu,
[+]                                           UVM_FAULT_REPLAY_TYPE_START,
[+]                                           batch_context);
[+]         ++num_replays;
[+]         break;
[+]
[+]     case UVM_REPLAY_ACTION_FLUSH:
[+]         // eBPF program can specify flush mode
[+]         status = fault_buffer_flush_locked(parent_gpu, NULL, ctx.flush_mode,
[+]                                           UVM_FAULT_REPLAY_TYPE_START,
[+]                                           batch_context);
[+]         ++num_replays;
[+]         break;
[+]
[+]     case UVM_REPLAY_ACTION_DEFER:
[+]         // WIC-style: defer replay until data ready
[+]         break;
[+]
[+]     case UVM_REPLAY_ACTION_DEFAULT:
[+]         // Fall through to default policy
[+]         goto default_policy;
[+]     }
[+] } else {
[+] default_policy:
[+]     // Fallback to static policy
        if (replayable_faults->replay_policy == UVM_PERF_FAULT_REPLAY_POLICY_BATCH) {
            status = push_replay_on_parent_gpu(parent_gpu,
                                              UVM_FAULT_REPLAY_TYPE_START,
                                              batch_context);
            ++num_replays;
        }
        else if (replayable_faults->replay_policy == UVM_PERF_FAULT_REPLAY_POLICY_BATCH_FLUSH) {
            // ... original flush logic ...
        }
[+] }
```

**eBPF Program Example**:
```c
SEC("struct_ops/uvm_get_replay_policy")
u32 bpf_wic_replay_policy(struct uvm_bpf_replay_context *ctx)
{
    // POLICY: WIC-aware selective replay
    u32 num_wic_faults = 0;
    u32 num_wic_ready = 0;

    // Count WIC faults and check DAB
    #pragma unroll
    for (int i = 0; i < 16 && i < ctx->num_faults; i++) {
        struct uvm_fault_buffer_entry *fault = &ctx->batch_context->fault_cache[i];

        if (fault->is_wic_pcm_fault) {
            num_wic_faults++;

            // Check if producer data is ready
            u32 tag_id = fault->wic_tag_id;
            u64 *dab = ctx->wic_dab_bits;
            if (dab && ((dab[tag_id / 64] >> (tag_id % 64)) & 1)) {
                num_wic_ready++;
            }
        }
    }

    if (num_wic_faults > 0 && num_wic_ready == 0) {
        // All WIC faults waiting for data - defer replay
        return UVM_REPLAY_ACTION_DEFER;
    } else if (num_wic_faults > 0 && num_wic_ready > 0) {
        // Some WIC faults ready - selective replay
        return UVM_REPLAY_ACTION_BATCH;
    } else {
        // No WIC faults - use adaptive policy
        if (ctx->num_duplicates * 100 > ctx->num_faults * 50) {
            ctx->flush_mode = UVM_GPU_BUFFER_FLUSH_MODE_UPDATE_PUT;
            return UVM_REPLAY_ACTION_FLUSH;
        }
        return UVM_REPLAY_ACTION_DEFAULT;
    }
}
```

---

### 2.3 POLICY #3: Fault Filtering (WIC PCM Detection)

**File**: `kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c:844-900`

**Current Code** (no WIC support):
```c
static NV_STATUS fetch_fault_buffer_entries(uvm_parent_gpu_t *parent_gpu,
                                            uvm_fault_service_batch_context_t *batch_context,
                                            fault_fetch_mode_t fetch_mode)
{
[=] NvU32 get;
[=] NvU32 put;
[=] NvU32 i;
[=] uvm_replayable_fault_buffer_t *replayable_faults = &parent_gpu->fault_buffer.replayable;
[=]
[=] // Read fault buffer pointers
[=] put = parent_gpu->fault_buffer_hal->read_put(parent_gpu);
[=] get = parent_gpu->fault_buffer_hal->read_get(parent_gpu);
[=]
[=] for (i = get; i != put; i = (i + 1) % replayable_faults->max_faults) {
[=]     uvm_fault_buffer_entry_t *current_entry = &batch_context->fault_cache[num_faults];
[=]
[=]     // Parse fault entry
[=]     status = parent_gpu->fault_buffer_hal->parse_replayable_entry(parent_gpu, i,
[=]                                                                   current_entry);
[=]     if (status != NV_OK)
[=]         return status;
[=]
[-]     // NO POLICY HOOK: Cannot detect WIC faults
[-]     // Current code just checks validity
[=]     if (!parent_gpu->fault_buffer_hal->entry_is_valid(parent_gpu, i))
[=]         continue;
[=]
[=]     // ... continue processing ...
[=] }
```

**eBPF Hook Version**:
```c
static NV_STATUS fetch_fault_buffer_entries(uvm_parent_gpu_t *parent_gpu,
                                            uvm_fault_service_batch_context_t *batch_context,
                                            fault_fetch_mode_t fetch_mode)
{
[=] NvU32 get;
[=] NvU32 put;
[=] NvU32 i;
[=] uvm_replayable_fault_buffer_t *replayable_faults = &parent_gpu->fault_buffer.replayable;
[=]
[=] // Read fault buffer pointers
[=] put = parent_gpu->fault_buffer_hal->read_put(parent_gpu);
[=] get = parent_gpu->fault_buffer_hal->read_get(parent_gpu);
[=]
[=] for (i = get; i != put; i = (i + 1) % replayable_faults->max_faults) {
[=]     uvm_fault_buffer_entry_t *current_entry = &batch_context->fault_cache[num_faults];
[=]
[=]     // Parse fault entry
[=]     status = parent_gpu->fault_buffer_hal->parse_replayable_entry(parent_gpu, i,
[=]                                                                   current_entry);
[=]     if (status != NV_OK)
[=]         return status;
[=]
[=]     if (!parent_gpu->fault_buffer_hal->entry_is_valid(parent_gpu, i))
[=]         continue;
[=]
[+]     // eBPF POLICY HOOK #3: Fault classification and filtering
[+]     if (parent_gpu->bpf_ops && parent_gpu->bpf_ops->classify_fault) {
[+]         struct uvm_bpf_fault_context ctx = {
[+]             .fault_address = current_entry->fault_address,
[+]             .proc_id = current_entry->fault_source.proc_id,
[+]             .fault_type = current_entry->fault_type,
[+]             .is_write = (current_entry->fault_access_type == UVM_FAULT_ACCESS_TYPE_WRITE),
[+]             .wic_pcm_region_start = parent_gpu->wic_context.pcm_region,
[+]             .wic_pcm_region_end = parent_gpu->wic_context.pcm_region +
[+]                                   parent_gpu->wic_context.pcm_region_size,
[+]             .wic_pat_region_start = parent_gpu->wic_context.pat_region,
[+]             .wic_pat_region_end = parent_gpu->wic_context.pat_region +
[+]                                   parent_gpu->wic_context.pat_region_size,
[+]         };
[+]
[+]         enum uvm_fault_class class = parent_gpu->bpf_ops->classify_fault(&ctx);
[+]
[+]         switch (class) {
[+]         case UVM_FAULT_CLASS_WIC_PCM:
[+]             current_entry->is_wic_pcm_fault = true;
[+]             current_entry->wic_tag_id = ctx.wic_tag_id;
[+]             break;
[+]
[+]         case UVM_FAULT_CLASS_WIC_PAT:
[+]             current_entry->is_wic_pat_fault = true;
[+]             current_entry->wic_tag_id = ctx.wic_tag_id;
[+]             break;
[+]
[+]         case UVM_FAULT_CLASS_SKIP:
[+]             // Policy says skip this fault
[+]             continue;
[+]
[+]         case UVM_FAULT_CLASS_REGULAR:
[+]         default:
[+]             // Normal UVM fault
[+]             break;
[+]         }
[+]     }
[=]
[=]     // ... continue processing ...
[=] }
```

**eBPF Program Example**:
```c
SEC("struct_ops/uvm_classify_fault")
u32 bpf_wic_classify_fault(struct uvm_bpf_fault_context *ctx)
{
    // POLICY: Detect WIC PCM/PAT faults
    u64 addr = ctx->fault_address;

    // Check if fault is in PCM region
    if (addr >= ctx->wic_pcm_region_start && addr < ctx->wic_pcm_region_end) {
        // Lookup channel info to get tag_id
        struct wic_channel_info *channel =
            bpf_map_lookup_elem(&wic_channels, &addr);

        if (channel) {
            ctx->wic_tag_id = channel->tag_id;
            return UVM_FAULT_CLASS_WIC_PCM;
        }
    }

    // Check if fault is in PAT region
    if (addr >= ctx->wic_pat_region_start && addr < ctx->wic_pat_region_end) {
        // Calculate tag_id from PAT offset
        u32 offset = addr - ctx->wic_pat_region_start;
        ctx->wic_tag_id = offset / sizeof(u32);
        return UVM_FAULT_CLASS_WIC_PAT;
    }

    return UVM_FAULT_CLASS_REGULAR;
}
```

---

### 2.4 POLICY #4: Fault Servicing Decision (WIC Monitor)

**File**: `kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c:2232-2300`

**Current Code** (services all faults immediately):
```c
static NV_STATUS service_fault_batch(uvm_parent_gpu_t *parent_gpu,
                                     fault_service_mode_t service_mode,
                                     uvm_fault_service_batch_context_t *batch_context)
{
[=] NV_STATUS status = NV_OK;
[=] NvU32 i;
[=]
[=] for (i = 0; i < batch_context->num_coalesced_faults; i++) {
[=]     uvm_fault_buffer_entry_t **ordered_fault_cache = batch_context->ordered_fault_cache;
[=]     uvm_fault_buffer_entry_t *fault = ordered_fault_cache[i];
[=]
[-]     // NO POLICY: Services all faults immediately
[-]     // Cannot defer WIC faults waiting for producer data
[=]
[=]     // Get thrashing hint
[=]     thrashing_hint = uvm_perf_thrashing_get_hint(va_block,
[=]                                                 va_block_context,
[=]                                                 fault->fault_address,
[=]                                                 fault->fault_source.proc_id);
[=]
[-]     // HARDCODED POLICY: Always respect throttle hint
[-]     if (thrashing_hint.type == UVM_PERF_THRASHING_HINT_TYPE_THROTTLE) {
[-]         batch_context->has_throttled_faults = true;
[-]         // ... throttle logic ...
[-]     }
[=]
[=]     // Service the fault
[=]     status = service_fault_batch_block(gpu, va_block, batch_context,
[=]                                       first_fault_index, ...);
[=] }
```

**eBPF Hook Version**:
```c
static NV_STATUS service_fault_batch(uvm_parent_gpu_t *parent_gpu,
                                     fault_service_mode_t service_mode,
                                     uvm_fault_service_batch_context_t *batch_context)
{
[=] NV_STATUS status = NV_OK;
[=] NvU32 i;
[=]
[=] for (i = 0; i < batch_context->num_coalesced_faults; i++) {
[=]     uvm_fault_buffer_entry_t **ordered_fault_cache = batch_context->ordered_fault_cache;
[=]     uvm_fault_buffer_entry_t *fault = ordered_fault_cache[i];
[=]
[+]     // eBPF POLICY HOOK #4: Should we service this fault now?
[+]     if (parent_gpu->bpf_ops && parent_gpu->bpf_ops->should_service_fault) {
[+]         struct uvm_bpf_service_context ctx = {
[+]             .fault = fault,
[+]             .va_block = va_block,
[+]             .is_wic_pcm_fault = fault->is_wic_pcm_fault,
[+]             .wic_tag_id = fault->wic_tag_id,
[+]             .wic_dab_bits = parent_gpu->wic_context.dab_bits,
[+]         };
[+]
[+]         enum uvm_service_action action = parent_gpu->bpf_ops->should_service_fault(&ctx);
[+]
[+]         switch (action) {
[+]         case UVM_SERVICE_ACTION_DEFER:
[+]             // WIC: Data not ready, defer to PFQ
[+]             wic_enqueue_pending_fault(parent_gpu, fault, ctx.defer_reason);
[+]             continue;  // Skip servicing this fault
[+]
[+]         case UVM_SERVICE_ACTION_SKIP:
[+]             // Policy says skip entirely
[+]             continue;
[+]
[+]         case UVM_SERVICE_ACTION_SERVICE:
[+]         case UVM_SERVICE_ACTION_DEFAULT:
[+]             // Proceed to service
[+]             break;
[+]         }
[+]     }
[=]
[=]     // Get thrashing hint
[=]     thrashing_hint = uvm_perf_thrashing_get_hint(va_block,
[=]                                                 va_block_context,
[=]                                                 fault->fault_address,
[=]                                                 fault->fault_source.proc_id);
[=]
[+]     // eBPF POLICY HOOK #5: Override throttle decision?
[+]     if (thrashing_hint.type == UVM_PERF_THRASHING_HINT_TYPE_THROTTLE) {
[+]         if (parent_gpu->bpf_ops && parent_gpu->bpf_ops->override_throttle) {
[+]             struct uvm_bpf_throttle_context ctx = {
[+]                 .hint = &thrashing_hint,
[+]                 .fault = fault,
[+]             };
[+]
[+]             if (parent_gpu->bpf_ops->override_throttle(&ctx)) {
[+]                 // Policy overrides throttle
[+]             } else {
                    batch_context->has_throttled_faults = true;
                    // ... throttle logic ...
[+]             }
[+]         } else {
                batch_context->has_throttled_faults = true;
                // ... throttle logic ...
[+]         }
[+]     }
[=]
[=]     // Service the fault
[=]     status = service_fault_batch_block(gpu, va_block, batch_context,
[=]                                       first_fault_index, ...);
[=] }
```

**eBPF Program Example**:
```c
SEC("struct_ops/uvm_should_service_fault")
u32 bpf_wic_should_service(struct uvm_bpf_service_context *ctx)
{
    // POLICY: WIC Monitor - defer faults if producer data not ready
    if (!ctx->is_wic_pcm_fault)
        return UVM_SERVICE_ACTION_DEFAULT;

    // Check DAB for this tag
    u32 tag_id = ctx->wic_tag_id;
    u64 *dab = ctx->wic_dab_bits;

    if (!dab)
        return UVM_SERVICE_ACTION_DEFAULT;

    // Atomic read of DAB
    u32 idx = tag_id / 64;
    u64 bit = 1ULL << (tag_id % 64);
    u64 dab_word = READ_ONCE(dab[idx]);

    if (!(dab_word & bit)) {
        // Producer data NOT ready - defer to PFQ
        ctx->defer_reason = "WIC: Producer data not ready";
        return UVM_SERVICE_ACTION_DEFER;
    }

    // Data ready - service normally (WIC Activator will handle)
    return UVM_SERVICE_ACTION_SERVICE;
}
```

---

### 2.5 POLICY #5: Thrashing Detection

**File**: `kernel-open/nvidia-uvm/uvm_perf_thrashing.c` (called from fault service path)

**Current Code** (hardcoded thrashing heuristics):
```c
uvm_perf_thrashing_hint_t uvm_perf_thrashing_get_hint(uvm_va_block_t *va_block,
                                                      uvm_va_block_context_t *va_block_context,
                                                      NvU64 address,
                                                      uvm_processor_id_t requester)
{
[=] uvm_perf_thrashing_hint_t hint = { .type = UVM_PERF_THRASHING_HINT_TYPE_NONE };
[=] block_thrashing_info_t *block_thrashing;
[=] page_thrashing_info_t *page_thrashing;
[=]
[-] // HARDCODED POLICY: Thrashing detection thresholds
[-] const NvU64 thrash_threshold_ns = 100000;  // 100us
[-] const NvU32 thrash_count_threshold = 10;
[=]
[=] block_thrashing = thrashing_info_get(va_block);
[=] if (!block_thrashing)
[=]     return hint;
[=]
[=] page_thrashing = &block_thrashing->pages[page_index];
[=]
[-] // HARDCODED POLICY: Time-based thrashing detection
[-] NvU64 time_since_last = current_time - page_thrashing->last_time_stamp;
[-] if (time_since_last < thrash_threshold_ns) {
[-]     page_thrashing->num_thrashing_events++;
[-]
[-]     if (page_thrashing->num_thrashing_events > thrash_count_threshold) {
[-]         // HARDCODED POLICY: Decide to throttle or pin
[-]         if (should_pin(va_block, page_index)) {
[-]             hint.type = UVM_PERF_THRASHING_HINT_TYPE_PIN;
[-]             // ... set pin parameters ...
[-]         } else {
[-]             hint.type = UVM_PERF_THRASHING_HINT_TYPE_THROTTLE;
[-]             hint.throttle.end_time_stamp = current_time + 1000000; // 1ms
[-]         }
[-]     }
[-] }
[=]
[=] return hint;
}
```

**eBPF Hook Version**:
```c
uvm_perf_thrashing_hint_t uvm_perf_thrashing_get_hint(uvm_va_block_t *va_block,
                                                      uvm_va_block_context_t *va_block_context,
                                                      NvU64 address,
                                                      uvm_processor_id_t requester)
{
[=] uvm_perf_thrashing_hint_t hint = { .type = UVM_PERF_THRASHING_HINT_TYPE_NONE };
[=] block_thrashing_info_t *block_thrashing;
[=] page_thrashing_info_t *page_thrashing;
[=]
[=] block_thrashing = thrashing_info_get(va_block);
[=] if (!block_thrashing)
[=]     return hint;
[=]
[=] page_thrashing = &block_thrashing->pages[page_index];
[=]
[+] // eBPF POLICY HOOK #6: Custom thrashing detection
[+] if (va_block->va_space->bpf_ops && va_block->va_space->bpf_ops->detect_thrashing) {
[+]     struct uvm_bpf_thrashing_context ctx = {
[+]         .va_block = va_block,
[+]         .address = address,
[+]         .requester = requester,
[+]         .last_time_stamp = page_thrashing->last_time_stamp,
[+]         .num_thrashing_events = page_thrashing->num_thrashing_events,
[+]         .last_processor = page_thrashing->last_processor,
[+]         .current_time = NV_GETTIME(),
[+]     };
[+]
[+]     enum uvm_thrashing_action action =
[+]         va_block->va_space->bpf_ops->detect_thrashing(&ctx);
[+]
[+]     switch (action) {
[+]     case UVM_THRASHING_ACTION_PIN:
[+]         hint.type = UVM_PERF_THRASHING_HINT_TYPE_PIN;
[+]         hint.pin.residency = ctx.pin_target;
[+]         // ... set from ctx ...
[+]         break;
[+]
[+]     case UVM_THRASHING_ACTION_THROTTLE:
[+]         hint.type = UVM_PERF_THRASHING_HINT_TYPE_THROTTLE;
[+]         hint.throttle.end_time_stamp = ctx.throttle_until;
[+]         break;
[+]
[+]     case UVM_THRASHING_ACTION_NONE:
[+]     case UVM_THRASHING_ACTION_DEFAULT:
[+]         // Fall through to default heuristic
[+]         goto default_heuristic;
[+]     }
[+]
[+]     return hint;
[+] }
[+]
[+] default_heuristic:
[+]     // Fallback to original hardcoded heuristic
    const NvU64 thrash_threshold_ns = 100000;
    const NvU32 thrash_count_threshold = 10;
    NvU64 time_since_last = current_time - page_thrashing->last_time_stamp;

    if (time_since_last < thrash_threshold_ns) {
        page_thrashing->num_thrashing_events++;

        if (page_thrashing->num_thrashing_events > thrash_count_threshold) {
            if (should_pin(va_block, page_index)) {
                hint.type = UVM_PERF_THRASHING_HINT_TYPE_PIN;
                // ... set pin parameters ...
            } else {
                hint.type = UVM_PERF_THRASHING_HINT_TYPE_THROTTLE;
                hint.throttle.end_time_stamp = current_time + 1000000; // 1ms
            }
        }
    }
[=]
[=] return hint;
}
```

**eBPF Program Example**:
```c
SEC("struct_ops/uvm_detect_thrashing")
u32 bpf_ml_thrashing_detector(struct uvm_bpf_thrashing_context *ctx)
{
    // POLICY: ML-based thrashing detection with per-process learning
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u64 addr = ctx->address;
    u64 now = ctx->current_time;

    // Get per-address state
    struct thrashing_state *state = bpf_map_lookup_elem(&thrashing_map, &addr);
    if (!state) {
        // First fault on this address
        struct thrashing_state new_state = {
            .last_fault_time = now,
            .fault_count = 1,
            .last_processor = ctx->requester,
        };
        bpf_map_update_elem(&thrashing_map, &addr, &new_state, BPF_NOEXIST);
        return UVM_THRASHING_ACTION_NONE;
    }

    // Calculate fault rate
    u64 delta = now - state->last_fault_time;

    // Adaptive thresholds based on process behavior
    u64 threshold = 100000;  // 100us default

    if (delta < threshold) {
        state->fault_count++;

        // Processor ping-pong detection
        bool ping_pong = (state->last_processor != ctx->requester);

        if (state->fault_count > 20 && ping_pong) {
            // Severe thrashing with ping-pong - pin to neutral location
            ctx->pin_target = UVM_ID_CPU;
            ctx->throttle_until = now + 5000000;  // 5ms
            return UVM_THRASHING_ACTION_PIN;
        }
        else if (state->fault_count > 10) {
            // Moderate thrashing - throttle
            ctx->throttle_until = now + 1000000;  // 1ms
            return UVM_THRASHING_ACTION_THROTTLE;
        }
    } else {
        // Reset counter
        state->fault_count = 0;
    }

    state->last_fault_time = now;
    state->last_processor = ctx->requester;

    return UVM_THRASHING_ACTION_NONE;
}
```

---

### 2.6 POLICY #6: Migration Target Selection

**File**: `kernel-open/nvidia-uvm/uvm_va_block.c` (called during fault servicing)

**Current Code** (hardcoded migration logic):
```c
static NV_STATUS uvm_va_block_make_resident(...,
                                            uvm_processor_id_t dest_id,
                                            ...)
{
[=] NV_STATUS status;
[=] uvm_va_block_region_t region;
[=]
[-] // HARDCODED POLICY: Migrate to requested destination
[-] // No consideration of:
[-] //  - Current memory pressure
[-] //  - Interconnect bandwidth
[-] //  - Historical access patterns
[-] //  - Cost of migration vs. remote access
[=]
[=] status = block_copy_resident_pages(..., dest_id, ...);
[=] if (status != NV_OK)
[=]     return status;
[=]
[=] // Update residency
[=] uvm_page_mask_region_fill(&va_block->resident[dest_id], region);
[=]
[=] return NV_OK;
}
```

**eBPF Hook Version**:
```c
static NV_STATUS uvm_va_block_make_resident(...,
                                            uvm_processor_id_t dest_id,
                                            ...)
{
[=] NV_STATUS status;
[=] uvm_va_block_region_t region;
[+] uvm_processor_id_t actual_dest = dest_id;
[=]
[+] // eBPF POLICY HOOK #7: Override migration target
[+] if (va_block->va_space->bpf_ops && va_block->va_space->bpf_ops->select_migration_target) {
[+]     uvm_processor_id_t candidates[UVM_ID_MAX_PROCESSORS];
[+]     NvU32 num_candidates = 0;
[+]
[+]     // Build list of valid migration targets
[+]     for_each_id_in_mask(proc_id, &va_block->va_space->accessible_from[dest_id]) {
[+]         candidates[num_candidates++] = proc_id;
[+]     }
[+]
[+]     struct uvm_bpf_migration_context ctx = {
[+]         .va_block = va_block,
[+]         .address = address,
[+]         .bytes = region_size,
[+]         .requested_dest = dest_id,
[+]         .candidates = candidates,
[+]         .num_candidates = num_candidates,
[+]         .cause = cause,
[+]     };
[+]
[+]     uvm_processor_id_t selected =
[+]         va_block->va_space->bpf_ops->select_migration_target(&ctx);
[+]
[+]     if (selected != UVM_ID_INVALID) {
[+]         actual_dest = selected;
[+]     }
[+] }
[=]
    status = block_copy_resident_pages(..., actual_dest, ...);
[=] if (status != NV_OK)
[=]     return status;
[=]
    uvm_page_mask_region_fill(&va_block->resident[actual_dest], region);
[=]
[=] return NV_OK;
}
```

**eBPF Program Example**:
```c
SEC("struct_ops/uvm_select_migration_target")
u32 bpf_smart_migration_target(struct uvm_bpf_migration_context *ctx)
{
    // POLICY: Smart migration considering bandwidth and pressure
    u32 pid = bpf_get_current_pid_tgid() >> 32;

    // Get process memory stats
    struct process_memory_stats *stats = bpf_map_lookup_elem(&process_stats, &pid);
    if (!stats)
        return ctx->requested_dest;  // No data, use default

    // Check memory pressure on requested destination
    if (ctx->requested_dest == UVM_ID_CPU) {
        if (stats->cpu_memory_pressure > 80) {
            // CPU under pressure - try to keep on GPU
            return stats->last_gpu_used;
        }
    } else {
        // GPU destination
        struct gpu_memory_stats *gpu_stats =
            bpf_map_lookup_elem(&gpu_stats_map, &ctx->requested_dest);

        if (gpu_stats && gpu_stats->memory_pressure > 90) {
            // GPU under pressure - consider alternatives

            // Check if we can use peer GPU
            for (u32 i = 0; i < ctx->num_candidates; i++) {
                u32 candidate = ctx->candidates[i];
                if (candidate == UVM_ID_CPU)
                    continue;

                struct gpu_memory_stats *cand_stats =
                    bpf_map_lookup_elem(&gpu_stats_map, &candidate);

                if (cand_stats && cand_stats->memory_pressure < 70) {
                    // This GPU has space
                    return candidate;
                }
            }

            // All GPUs full, fall back to CPU
            return UVM_ID_CPU;
        }
    }

    // No pressure issues, use requested destination
    return ctx->requested_dest;
}
```

---

## 3. Existing Callback Structures

### 3.1 HAL Fault Buffer Operations

**Location**: `kernel-open/nvidia-uvm/uvm_hal.h:848-864`

```c
struct uvm_fault_buffer_hal_struct
{
[=] // Fault buffer control
[=] uvm_hal_enable_replayable_faults_t enable_replayable_faults;
[=] uvm_hal_disable_replayable_faults_t disable_replayable_faults;
[=]
[=] // Buffer access
[=] uvm_hal_fault_buffer_read_put_t read_put;
[=] uvm_hal_fault_buffer_read_get_t read_get;
[=] uvm_hal_fault_buffer_write_get_t write_get;
[=]
[+] // *** eBPF HOOK OPPORTUNITY #1 ***
[+] // Replace these with eBPF-callable versions
[=] uvm_hal_fault_buffer_parse_replayable_entry_t parse_replayable_entry;
[=] uvm_hal_fault_buffer_entry_is_valid_t entry_is_valid;
[=] uvm_hal_fault_buffer_entry_clear_valid_t entry_clear_valid;
[=]
[=] uvm_hal_fault_buffer_entry_size_t entry_size;
[=] uvm_hal_fault_buffer_parse_non_replayable_entry_t parse_non_replayable_entry;
[=] uvm_hal_fault_buffer_get_fault_type_t get_fault_type;
};
```

### 3.2 HAL Host Operations (Replay Control)

**Location**: `kernel-open/nvidia-uvm/uvm_hal.h:779-811`

```c
struct uvm_host_hal_struct
{
[=] // TLB operations
[=] uvm_hal_host_tlb_invalidate_all_t tlb_invalidate_all;
[=] uvm_hal_host_tlb_invalidate_va_t tlb_invalidate_va;
[=]
[+] // *** eBPF HOOK OPPORTUNITY #2 ***
[+] // Critical for WIC selective replay
[=] uvm_hal_fault_buffer_replay_t replay_faults;
[=] uvm_hal_fault_cancel_global_t cancel_faults_global;
[=] uvm_hal_fault_cancel_targeted_t cancel_faults_targeted;
[=] uvm_hal_fault_cancel_va_t cancel_faults_va;
[=]
[=] // Access counters
[=] uvm_hal_access_counter_clear_all_t access_counter_clear_all;
[=] uvm_hal_access_counter_clear_targeted_t access_counter_clear_targeted;
};
```

### 3.3 Perf Module Callbacks

**Location**: `kernel-open/nvidia-uvm/uvm_perf_module.h:61-69`

```c
struct uvm_perf_module_struct
{
[=] const char *name;
[=] uvm_perf_module_type_t type;
[=]
[+] // *** eBPF HOOK OPPORTUNITY #3 ***
[+] // Each callback can be eBPF program
[=] uvm_perf_event_callback_t callbacks[UVM_PERF_EVENT_COUNT];
};

// Event types (from uvm_perf_events.h:49-79)
enum {
[+]     UVM_PERF_EVENT_FAULT,        // *** Policy: fault handling ***
[+]     UVM_PERF_EVENT_MIGRATION,    // *** Policy: migration decision ***
[+]     UVM_PERF_EVENT_REVOCATION,   // *** Policy: revocation handling ***
[=]     // ... other events ...
};
```

---

## 4. Safe Hook Insertion Points

### Locking Context Requirements

| Hook Point | File:Line | Locks Held | Can Sleep? | eBPF Safe? |
|-----------|-----------|------------|------------|------------|
| **parse_replayable_entry** | uvm_gpu_replayable_faults.c:844 | `isr_lock` (read) | ❌ No | ✅ Yes (atomic) |
| **classify_fault** | uvm_gpu_replayable_faults.c:900 | `isr_lock` (read) | ❌ No | ✅ Yes (atomic) |
| **get_replay_policy** | uvm_gpu_replayable_faults.c:2986 | `isr_lock` (read) | ❌ No | ✅ Yes (atomic) |
| **should_service_fault** | uvm_gpu_replayable_faults.c:2250 | `va_space` (read), `va_block` (exclusive) | ⚠️ Brief | ⚠️ Limited |
| **detect_thrashing** | uvm_perf_thrashing.c | `va_block` (exclusive) | ⚠️ Brief | ⚠️ Limited |
| **select_migration_target** | uvm_va_block.c | `va_block` (exclusive) | ⚠️ Brief | ⚠️ Limited |

**Safety Guidelines**:

1. **ISR Context** (`isr_lock` held):
   ```c
   // ✅ SAFE: Atomic operations, BPF maps, non-sleeping helpers
   u64 value = READ_ONCE(shared_var);
   bpf_map_update_elem(&map, &key, &value, BPF_ANY);

   // ❌ UNSAFE: Sleeping, locks, blocking operations
   // msleep(1);           // NEVER in ISR context
   // mutex_lock(&lock);   // NEVER in ISR context
   ```

2. **Fault Service Context** (`va_block` held):
   ```c
   // ✅ SAFE: Brief operations, BPF maps
   struct data *d = bpf_map_lookup_elem(&map, &key);
   if (d) d->counter++;

   // ⚠️ LIMITED: Very brief sleeping OK
   // udelay(10);  // Microseconds OK

   // ❌ UNSAFE: Long sleeps
   // msleep(100);  // Too long!
   ```

---

## 5. eBPF Program Interface Design

### 5.1 Main struct_ops Definition

**New file**: `kernel-open/nvidia-uvm/uvm_bpf_ops.h`

```c
#ifndef __UVM_BPF_OPS_H__
#define __UVM_BPF_OPS_H__

#ifdef CONFIG_BPF_SYSCALL

// eBPF struct_ops for UVM policies
struct uvm_bpf_ops {
    // *** POLICY HOOK #1: Batch sizing ***
    u32 (*get_batch_size)(struct uvm_bpf_batch_context *ctx);

    // *** POLICY HOOK #2: Replay policy ***
    u32 (*get_replay_policy)(struct uvm_bpf_replay_context *ctx);

    // *** POLICY HOOK #3: Fault classification ***
    u32 (*classify_fault)(struct uvm_bpf_fault_context *ctx);

    // *** POLICY HOOK #4: Service decision ***
    u32 (*should_service_fault)(struct uvm_bpf_service_context *ctx);

    // *** POLICY HOOK #5: Throttle override ***
    bool (*override_throttle)(struct uvm_bpf_throttle_context *ctx);

    // *** POLICY HOOK #6: Thrashing detection ***
    u32 (*detect_thrashing)(struct uvm_bpf_thrashing_context *ctx);

    // *** POLICY HOOK #7: Migration target ***
    u32 (*select_migration_target)(struct uvm_bpf_migration_context *ctx);
};

#endif /* CONFIG_BPF_SYSCALL */
#endif /* __UVM_BPF_OPS_H__ */
```

### 5.2 Integration with UVM Structures

**Modify**: `kernel-open/nvidia-uvm/uvm_gpu.h`

```c
struct uvm_parent_gpu_struct {
[=]     // ... existing fields ...
[=]
[+]     // eBPF policy hooks
[+]     struct uvm_bpf_ops *bpf_ops;
[+]
[+]     // WIC context (accessible to eBPF)
[+]     struct {
[+]         void *pcm_region;
[+]         size_t pcm_region_size;
[+]         void *pat_region;
[+]         size_t pat_region_size;
[+]         u64 *dab_bits;
[+]     } wic_context;
};
```

---

## 6. WIC Integration via eBPF

### Complete WIC Implementation as eBPF Policy

**File**: `examples/wic_policy.bpf.c`

See full implementation in document...

---

## 7. Implementation Roadmap

### Phase 1: eBPF Infrastructure (3 weeks)

**Week 1**: Core struct_ops
- [ ] Define `struct uvm_bpf_ops` in `uvm_bpf_ops.h`
- [ ] Add BTF export to Makefile
- [ ] Create registration functions

**Week 2**: Hook integration
- [+] **Insert hook #1**: `get_batch_size` in `fault_buffer_init_replayable_faults()`
- [+] **Insert hook #2**: `get_replay_policy` in `uvm_parent_gpu_service_replayable_faults()`
- [+] **Insert hook #3**: `classify_fault` in `fetch_fault_buffer_entries()`

**Week 3**: Testing
- [ ] Write minimal test eBPF programs for each hook
- [ ] Verify hook invocation
- [ ] Test default fallback behavior

### Phase 2: WIC eBPF Policy (5 weeks)

**Week 4-5**: Data structures
- [+] **Insert WIC context** in `uvm_parent_gpu_struct`
- [+] Create BPF maps for DAB, PFQ, channels

**Week 6-7**: eBPF logic
- [+] **Implement Interrupter** in `classify_fault` hook
- [+] **Implement Monitor** in `should_service_fault` hook
- [+] **Implement Activator** in `get_replay_policy` hook

**Week 8**: Integration
- [ ] End-to-end WIC testing
- [ ] Performance benchmarking

---

## Appendix: Policy Hook Summary Table

| # | Policy Hook | Location (File:Line) | Marker | Purpose |
|---|-------------|----------------------|--------|---------|
| 1 | `get_batch_size` | uvm_gpu_replayable_faults.c:153 | `[+]` | Dynamic fault batch sizing |
| 2 | `get_replay_policy` | uvm_gpu_replayable_faults.c:2986 | `[+]` | WIC selective replay |
| 3 | `classify_fault` | uvm_gpu_replayable_faults.c:900 | `[+]` | WIC PCM/PAT detection |
| 4 | `should_service_fault` | uvm_gpu_replayable_faults.c:2250 | `[+]` | WIC Monitor (DAB check) |
| 5 | `override_throttle` | uvm_gpu_replayable_faults.c:2280 | `[+]` | Override thrashing throttle |
| 6 | `detect_thrashing` | uvm_perf_thrashing.c | `[+]` | ML-based thrashing detection |
| 7 | `select_migration_target` | uvm_va_block.c | `[+]` | Smart migration target selection |

**Legend**:
- `[-]` = Code to remove (hardcoded policy)
- `[+]` = Code to add (eBPF hook)
- `[=]` = Unchanged code (context)

---

**End of Document**
