/* SPDX-License-Identifier: MIT */

#include <linux/atomic.h>
#include <linux/capability.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "uvm_bpf_struct_ops.h"
#include "uvm_stale_state_v1.h"

#define UVM_STALE_STATE_V1_CONTROL "uvm_stale_state_v1"
#define UVM_STALE_STATE_V1_REQUEST_COOKIE 0x53545631U

struct uvm_stale_state_v1_snapshot_node
{
    nv_gpu_stale_state_v1_snapshot_t snapshot;
    NvU64 generation;
    struct rcu_head rcu;
};

struct uvm_stale_state_v1_stats
{
    atomic64_t snapshot_updates;
    atomic64_t snapshot_rejections;
    atomic64_t callback_invocations;
    atomic64_t snapshot_read_attempts;
    atomic64_t snapshot_read_successes;
    atomic64_t missing_snapshot_decisions;
    atomic64_t invalid_snapshot_decisions;
    atomic64_t native_callback_invocations;
    atomic64_t bpf_callback_invocations;
    atomic64_t decision_requests;
    atomic64_t decisions;
    atomic64_t decision_records;
    atomic64_t effect_requests;
    atomic64_t effect_records;
    atomic64_t dense_prefetch_decisions;
    atomic64_t discarded_prefetch_decisions;
    atomic64_t request_errors;
    atomic64_t effect_errors;
    atomic64_t selected_diagnostics;
    atomic64_t finished_diagnostics;
};

static DEFINE_MUTEX(g_uvm_stale_state_v1_control_lock);
static struct uvm_stale_state_v1_snapshot_node __rcu *g_uvm_stale_state_v1_snapshot;
static atomic_t g_uvm_stale_state_v1_mode = ATOMIC_INIT(UVM_STALE_STATE_V1_MODE_OFF);
static atomic_t g_uvm_stale_state_v1_active_callbacks = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(g_uvm_stale_state_v1_callback_waitq);
static atomic64_t g_uvm_stale_state_v1_generation = ATOMIC64_INIT(0);
static atomic64_t g_uvm_stale_state_v1_decision_sequence = ATOMIC64_INIT(0);
static struct uvm_stale_state_v1_stats g_uvm_stale_state_v1_stats;
static struct proc_dir_entry *g_uvm_stale_state_v1_control;

static void uvm_stale_state_v1_reset_stats(void)
{
#define RESET(field) atomic64_set(&g_uvm_stale_state_v1_stats.field, 0)
    RESET(snapshot_updates);
    RESET(snapshot_rejections);
    RESET(callback_invocations);
    RESET(snapshot_read_attempts);
    RESET(snapshot_read_successes);
    RESET(missing_snapshot_decisions);
    RESET(invalid_snapshot_decisions);
    RESET(native_callback_invocations);
    RESET(bpf_callback_invocations);
    RESET(decision_requests);
    RESET(decisions);
    RESET(decision_records);
    RESET(effect_requests);
    RESET(effect_records);
    RESET(dense_prefetch_decisions);
    RESET(discarded_prefetch_decisions);
    RESET(request_errors);
    RESET(effect_errors);
    RESET(selected_diagnostics);
    RESET(finished_diagnostics);
#undef RESET
    atomic64_set(&g_uvm_stale_state_v1_decision_sequence, 0);
}

/*
 * Control changes first close the callback gate and then wait for callbacks
 * that crossed the old gate. This keeps generation-scoped counters and
 * snapshot state from being mixed while avoiding a sleeping lock in the
 * page-fault path.
 */
static void uvm_stale_state_v1_quiesce_callbacks_locked(void)
{
    atomic_set(&g_uvm_stale_state_v1_mode, UVM_STALE_STATE_V1_MODE_OFF);
    smp_mb();
    wait_event(g_uvm_stale_state_v1_callback_waitq,
               atomic_read(&g_uvm_stale_state_v1_active_callbacks) == 0);
}

static bool uvm_stale_state_v1_callback_get(enum uvm_stale_state_v1_mode *mode)
{
    enum uvm_stale_state_v1_mode observed;

    for (;;) {
        observed = atomic_read_acquire(&g_uvm_stale_state_v1_mode);
        if (observed == UVM_STALE_STATE_V1_MODE_OFF)
            return false;

        atomic_inc(&g_uvm_stale_state_v1_active_callbacks);
        smp_mb__after_atomic();
        if (atomic_read_acquire(&g_uvm_stale_state_v1_mode) == observed) {
            *mode = observed;
            return true;
        }

        if (atomic_dec_and_test(&g_uvm_stale_state_v1_active_callbacks))
            wake_up_all(&g_uvm_stale_state_v1_callback_waitq);
    }
}

static void uvm_stale_state_v1_callback_put(void)
{
    if (atomic_dec_and_test(&g_uvm_stale_state_v1_active_callbacks))
        wake_up_all(&g_uvm_stale_state_v1_callback_waitq);
}

static const char *uvm_stale_state_v1_mode_name(enum uvm_stale_state_v1_mode mode)
{
    switch (mode) {
        case UVM_STALE_STATE_V1_MODE_NATIVE:
            return "native";
        case UVM_STALE_STATE_V1_MODE_BPF:
            return "bpf";
        default:
            return "off";
    }
}

static bool uvm_stale_state_v1_read_snapshot(
    NvU64 generation,
    nv_gpu_stale_state_v1_snapshot_t *snapshot)
{
    struct uvm_stale_state_v1_snapshot_node *node;
    bool found = false;

    rcu_read_lock();
    node = rcu_dereference(g_uvm_stale_state_v1_snapshot);
    if ((node != NULL) && (node->generation == generation)) {
        *snapshot = node->snapshot;
        found = true;
    }
    rcu_read_unlock();
    return found;
}

static void uvm_stale_state_v1_replace_snapshot_locked(
    struct uvm_stale_state_v1_snapshot_node *replacement)
{
    struct uvm_stale_state_v1_snapshot_node *old;

    old = rcu_dereference_protected(g_uvm_stale_state_v1_snapshot,
                                    lockdep_is_held(&g_uvm_stale_state_v1_control_lock));
    rcu_assign_pointer(g_uvm_stale_state_v1_snapshot, replacement);
    if (old != NULL)
        kfree_rcu(old, rcu);
}

static int uvm_stale_state_v1_configure(enum uvm_stale_state_v1_mode mode,
                                        NvU64 generation)
{
    if ((generation == 0) ||
        ((mode != UVM_STALE_STATE_V1_MODE_NATIVE) &&
         (mode != UVM_STALE_STATE_V1_MODE_BPF)))
        return -EINVAL;

    mutex_lock(&g_uvm_stale_state_v1_control_lock);
    uvm_stale_state_v1_quiesce_callbacks_locked();
    uvm_stale_state_v1_replace_snapshot_locked(NULL);
    uvm_stale_state_v1_reset_stats();
    atomic64_set(&g_uvm_stale_state_v1_generation, generation);
    atomic_set_release(&g_uvm_stale_state_v1_mode, mode);
    mutex_unlock(&g_uvm_stale_state_v1_control_lock);
    return 0;
}

static int uvm_stale_state_v1_disable(NvU64 generation)
{
    int ret = 0;

    mutex_lock(&g_uvm_stale_state_v1_control_lock);
    if ((generation == 0) ||
        (generation != (NvU64)atomic64_read(&g_uvm_stale_state_v1_generation))) {
        ret = -ESTALE;
        goto out;
    }

    uvm_stale_state_v1_quiesce_callbacks_locked();
    uvm_stale_state_v1_replace_snapshot_locked(NULL);
out:
    mutex_unlock(&g_uvm_stale_state_v1_control_lock);
    return ret;
}

static int uvm_stale_state_v1_publish(NvU64 generation,
                                      NvU64 sequence,
                                      NvU32 phase,
                                      NvU64 source_mono_ns)
{
    struct uvm_stale_state_v1_snapshot_node *replacement;
    struct uvm_stale_state_v1_snapshot_node *current_node;
    NvU64 now;
    int ret = 0;

    replacement = kzalloc(sizeof(*replacement), GFP_KERNEL);
    if (replacement == NULL)
        return -ENOMEM;

    mutex_lock(&g_uvm_stale_state_v1_control_lock);
    if ((atomic_read(&g_uvm_stale_state_v1_mode) == UVM_STALE_STATE_V1_MODE_OFF) ||
        (generation == 0) ||
        (generation != (NvU64)atomic64_read(&g_uvm_stale_state_v1_generation))) {
        ret = -ESTALE;
        goto reject;
    }

    if ((sequence == 0) || (source_mono_ns == 0) ||
        ((phase != NV_GPU_STALE_STATE_V1_PHASE_DENSE) &&
         (phase != NV_GPU_STALE_STATE_V1_PHASE_SPARSE))) {
        ret = -EINVAL;
        goto reject;
    }

    current_node = rcu_dereference_protected(g_uvm_stale_state_v1_snapshot,
                                             lockdep_is_held(&g_uvm_stale_state_v1_control_lock));
    if (!nv_gpu_stale_state_v1_publication_follows(
            current_node == NULL ? NULL : &current_node->snapshot,
            sequence,
            source_mono_ns)) {
        ret = -ERANGE;
        goto reject;
    }

    now = ktime_get_ns();
    if (source_mono_ns > now) {
        ret = -ERANGE;
        goto reject;
    }

    replacement->generation = generation;
    replacement->snapshot.sequence = sequence;
    replacement->snapshot.phase = phase;
    replacement->snapshot.source_mono_ns = source_mono_ns;
    replacement->snapshot.published_mono_ns = now;
    uvm_stale_state_v1_replace_snapshot_locked(replacement);
    atomic64_inc(&g_uvm_stale_state_v1_stats.snapshot_updates);
    mutex_unlock(&g_uvm_stale_state_v1_control_lock);
    return 0;

reject:
    atomic64_inc(&g_uvm_stale_state_v1_stats.snapshot_rejections);
    mutex_unlock(&g_uvm_stale_state_v1_control_lock);
    kfree(replacement);
    return ret;
}

static int uvm_stale_state_v1_control_show(struct seq_file *seq, void *unused)
{
    nv_gpu_stale_state_v1_snapshot_t snapshot = {0};
    enum uvm_stale_state_v1_mode mode;
    NvU64 generation;
    bool present;

    (void)unused;
    mutex_lock(&g_uvm_stale_state_v1_control_lock);
    mode = atomic_read(&g_uvm_stale_state_v1_mode);
    generation = (NvU64)atomic64_read(&g_uvm_stale_state_v1_generation);
    present = uvm_stale_state_v1_read_snapshot(generation, &snapshot);
    seq_printf(seq,
               "abi_version=%u mode=%s generation=%llu snapshot_present=%u "
               "snapshot_sequence=%llu snapshot_phase=%u source_mono_ns=%llu "
               "published_mono_ns=%llu\n",
               NV_GPU_STALE_STATE_V1_ABI_VERSION,
               uvm_stale_state_v1_mode_name(mode),
               (unsigned long long)generation,
               present ? 1U : 0U,
               (unsigned long long)snapshot.sequence,
               snapshot.phase,
               (unsigned long long)snapshot.source_mono_ns,
               (unsigned long long)snapshot.published_mono_ns);
#define SHOW(field)                                                              \
    seq_printf(seq, #field "=%llu\n",                                           \
               (unsigned long long)atomic64_read(&g_uvm_stale_state_v1_stats.field))
    SHOW(snapshot_updates);
    SHOW(snapshot_rejections);
    SHOW(callback_invocations);
    SHOW(snapshot_read_attempts);
    SHOW(snapshot_read_successes);
    SHOW(missing_snapshot_decisions);
    SHOW(invalid_snapshot_decisions);
    SHOW(native_callback_invocations);
    SHOW(bpf_callback_invocations);
    SHOW(decision_requests);
    SHOW(decisions);
    SHOW(decision_records);
    SHOW(effect_requests);
    SHOW(effect_records);
    SHOW(dense_prefetch_decisions);
    SHOW(discarded_prefetch_decisions);
    SHOW(request_errors);
    SHOW(effect_errors);
    SHOW(selected_diagnostics);
    SHOW(finished_diagnostics);
    seq_printf(seq, "active_callbacks=%d\n",
               atomic_read(&g_uvm_stale_state_v1_active_callbacks));
#undef SHOW
    mutex_unlock(&g_uvm_stale_state_v1_control_lock);
    return 0;
}

static int uvm_stale_state_v1_control_open(struct inode *inode, struct file *file)
{
    return single_open(file, uvm_stale_state_v1_control_show, NULL);
}

static ssize_t uvm_stale_state_v1_control_write(struct file *file,
                                                const char __user *buffer,
                                                size_t count,
                                                loff_t *position)
{
    char input[160];
    char mode_name[8];
    char trailing;
    unsigned long long generation;
    unsigned long long sequence;
    unsigned long long source_mono_ns;
    unsigned int phase;
    int ret;

    (void)file;
    (void)position;
    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;
    if ((count == 0) || (count >= sizeof(input)))
        return -E2BIG;
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    input[count] = '\0';
    strim(input);

    if (sscanf(input, "configure %7s %llu %c", mode_name, &generation,
               &trailing) == 2) {
        enum uvm_stale_state_v1_mode mode;

        if (strcmp(mode_name, "native") == 0)
            mode = UVM_STALE_STATE_V1_MODE_NATIVE;
        else if (strcmp(mode_name, "bpf") == 0)
            mode = UVM_STALE_STATE_V1_MODE_BPF;
        else
            return -EINVAL;
        ret = uvm_stale_state_v1_configure(mode, (NvU64)generation);
    }
    else if (sscanf(input, "publish %llu %llu %u %llu %c",
                    &generation, &sequence, &phase, &source_mono_ns,
                    &trailing) == 4) {
        ret = uvm_stale_state_v1_publish((NvU64)generation,
                                         (NvU64)sequence,
                                         (NvU32)phase,
                                         (NvU64)source_mono_ns);
    }
    else if (sscanf(input, "disable %llu %c", &generation, &trailing) == 1) {
        ret = uvm_stale_state_v1_disable((NvU64)generation);
    }
    else {
        return -EINVAL;
    }

    return ret == 0 ? (ssize_t)count : ret;
}

static const struct proc_ops uvm_stale_state_v1_control_ops = {
    .proc_open = uvm_stale_state_v1_control_open,
    .proc_read = seq_read,
    .proc_write = uvm_stale_state_v1_control_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

int uvm_stale_state_v1_init(void)
{
    atomic_set(&g_uvm_stale_state_v1_mode, UVM_STALE_STATE_V1_MODE_OFF);
    atomic_set(&g_uvm_stale_state_v1_active_callbacks, 0);
    atomic64_set(&g_uvm_stale_state_v1_generation, 0);
    uvm_stale_state_v1_reset_stats();
    RCU_INIT_POINTER(g_uvm_stale_state_v1_snapshot, NULL);
    g_uvm_stale_state_v1_control =
        proc_create(UVM_STALE_STATE_V1_CONTROL, 0600, NULL,
                    &uvm_stale_state_v1_control_ops);
    return g_uvm_stale_state_v1_control == NULL ? -ENOMEM : 0;
}

void uvm_stale_state_v1_exit(void)
{
    struct uvm_stale_state_v1_snapshot_node *old;

    if (g_uvm_stale_state_v1_control != NULL) {
        proc_remove(g_uvm_stale_state_v1_control);
        g_uvm_stale_state_v1_control = NULL;
    }

    mutex_lock(&g_uvm_stale_state_v1_control_lock);
    uvm_stale_state_v1_quiesce_callbacks_locked();
    old = rcu_dereference_protected(g_uvm_stale_state_v1_snapshot,
                                    lockdep_is_held(&g_uvm_stale_state_v1_control_lock));
    RCU_INIT_POINTER(g_uvm_stale_state_v1_snapshot, NULL);
    mutex_unlock(&g_uvm_stale_state_v1_control_lock);
    synchronize_rcu();
    kfree(old);
}

static int uvm_stale_state_v1_record_action(
    uvm_stale_state_v1_decision_ctx_t *decision_ctx,
    NvU32 action)
{
    if ((decision_ctx == NULL) ||
        (decision_ctx->input.abi_version != NV_GPU_STALE_STATE_V1_ABI_VERSION) ||
        (decision_ctx->input.reserved != 0))
        return NV_GPU_TRANSITION_REJECT_IDENTITY;

    ++decision_ctx->request_calls;
    decision_ctx->request_cookie = UVM_STALE_STATE_V1_REQUEST_COOKIE;
    atomic64_inc(&g_uvm_stale_state_v1_stats.decision_requests);

    if ((action != NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX) &&
        (action != NV_GPU_STALE_STATE_V1_ACTION_DISCARD_PREFETCH))
        return NV_GPU_TRANSITION_REJECT_ACTION;

    return nv_gpu_transition_record_u32(&decision_ctx->action_request, action);
}

int uvm_stale_state_v1_record_bpf_action(
    uvm_stale_state_v1_decision_ctx_t *decision_ctx,
    NvU32 action)
{
    return uvm_stale_state_v1_record_action(decision_ctx, action);
}

bool uvm_stale_state_v1_begin(uvm_page_index_t page_index,
                              const uvm_va_block_region_t *maximum,
                              uvm_stale_state_v1_decision_ctx_t *decision_ctx,
                              struct uvm_stale_state_v1_diagnostic *diagnostic,
                              NvS64 *raw_action,
                              nv_gpu_prefetch_decision_t *request)
{
    enum uvm_stale_state_v1_mode mode;
    enum nv_gpu_stale_state_v1_action action;
    nv_gpu_stale_state_v1_decision_t model_decision = {0};
    struct uvm_stale_state_v1_input input_before;
    NvU64 generation;
    int request_result;

    if (!uvm_stale_state_v1_callback_get(&mode))
        return false;

    memset(decision_ctx, 0, sizeof(*decision_ctx));
    memset(diagnostic, 0, sizeof(*diagnostic));
    *raw_action = NV_GPU_TRANSITION_ACTION_DEFAULT;
    generation = (NvU64)atomic64_read(&g_uvm_stale_state_v1_generation);
    diagnostic->mode = mode;
    diagnostic->input.abi_version = NV_GPU_STALE_STATE_V1_ABI_VERSION;
    diagnostic->input.generation = generation;
    diagnostic->input.decision_sequence =
        (NvU64)atomic64_inc_return(&g_uvm_stale_state_v1_decision_sequence);
    diagnostic->input.page_index = page_index;
    diagnostic->input.max_first = maximum->first;
    diagnostic->input.max_outer = maximum->outer;
    atomic64_inc(&g_uvm_stale_state_v1_stats.callback_invocations);
    atomic64_inc(&g_uvm_stale_state_v1_stats.snapshot_read_attempts);

    if (!uvm_stale_state_v1_read_snapshot(generation,
                                          &diagnostic->input.snapshot)) {
        diagnostic->status = UVM_STALE_STATE_V1_STATUS_MISSING_SNAPSHOT;
        atomic64_inc(&g_uvm_stale_state_v1_stats.missing_snapshot_decisions);
        return true;
    }

    atomic64_inc(&g_uvm_stale_state_v1_stats.snapshot_read_successes);
    diagnostic->input.decision_mono_ns = ktime_get_ns();
    if (!nv_gpu_stale_state_v1_snapshot_valid(&diagnostic->input.snapshot,
                                               diagnostic->input.decision_mono_ns)) {
        diagnostic->status = UVM_STALE_STATE_V1_STATUS_INVALID_SNAPSHOT;
        atomic64_inc(&g_uvm_stale_state_v1_stats.invalid_snapshot_decisions);
        return true;
    }

    decision_ctx->input = diagnostic->input;
    input_before = decision_ctx->input;
    if (mode == UVM_STALE_STATE_V1_MODE_NATIVE) {
        action = nv_gpu_stale_state_v1_choose(&decision_ctx->input.snapshot,
                                              decision_ctx->input.decision_mono_ns,
                                              &model_decision);
        diagnostic->callback_return = action;
        request_result = uvm_stale_state_v1_record_action(decision_ctx,
                                                          action);
        atomic64_inc(&g_uvm_stale_state_v1_stats.native_callback_invocations);
    }
    else {
        atomic64_inc(&g_uvm_stale_state_v1_stats.bpf_callback_invocations);
        diagnostic->callback_return =
            uvm_bpf_call_gpu_stale_state_v1(decision_ctx);
        request_result = decision_ctx->action_request.attempted ?
                             NV_GPU_TRANSITION_APPLY :
                             NV_GPU_TRANSITION_NOOP_DEFAULT;
    }

    diagnostic->action_attempted = decision_ctx->action_request.attempted;
    diagnostic->action_conflict = decision_ctx->action_request.conflict;
    diagnostic->action_request_calls = decision_ctx->request_calls;
    diagnostic->action = decision_ctx->action_request.value;

    if ((memcmp(&input_before, &decision_ctx->input, sizeof(input_before)) != 0) ||
        (request_result != NV_GPU_TRANSITION_APPLY) ||
        (decision_ctx->request_cookie != UVM_STALE_STATE_V1_REQUEST_COOKIE) ||
        (decision_ctx->request_calls != 1) ||
        !decision_ctx->action_request.attempted ||
        decision_ctx->action_request.conflict ||
        ((NvU64)diagnostic->callback_return !=
         (NvU64)decision_ctx->action_request.value) ||
        ((decision_ctx->action_request.value !=
          NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX) &&
         (decision_ctx->action_request.value !=
          NV_GPU_STALE_STATE_V1_ACTION_DISCARD_PREFETCH))) {
        diagnostic->status = UVM_STALE_STATE_V1_STATUS_REQUEST_ERROR;
        atomic64_inc(&g_uvm_stale_state_v1_stats.request_errors);
        return true;
    }

    action = decision_ctx->action_request.value;
    request_result = action == NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX ?
                         nv_gpu_transition_record_prefetch(request,
                                                           maximum->first,
                                                           maximum->outer) :
                         nv_gpu_transition_record_prefetch(request, 0, 0);
    if (request_result != NV_GPU_TRANSITION_APPLY) {
        diagnostic->status = UVM_STALE_STATE_V1_STATUS_REQUEST_ERROR;
        atomic64_inc(&g_uvm_stale_state_v1_stats.request_errors);
        return true;
    }

    diagnostic->decision_age_ns = model_decision.decision_age_ns;
    if (mode == UVM_STALE_STATE_V1_MODE_BPF)
        diagnostic->decision_age_ns = diagnostic->input.decision_mono_ns -
                                      diagnostic->input.snapshot.source_mono_ns;
    diagnostic->requested_first = request->first;
    diagnostic->requested_outer = request->outer;
    diagnostic->status = UVM_STALE_STATE_V1_STATUS_DECISION_READY;
    *raw_action = NV_GPU_TRANSITION_ACTION_BYPASS;
    atomic64_inc(&g_uvm_stale_state_v1_stats.decisions);
    atomic64_inc(&g_uvm_stale_state_v1_stats.effect_requests);
    if (action == NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX)
        atomic64_inc(&g_uvm_stale_state_v1_stats.dense_prefetch_decisions);
    else
        atomic64_inc(&g_uvm_stale_state_v1_stats.discarded_prefetch_decisions);
    return true;
}

void uvm_stale_state_v1_selected(
    struct uvm_stale_state_v1_diagnostic *diagnostic,
    enum nv_gpu_transition_result region_result,
    enum nv_gpu_prefetch_initial_effect initial_effect)
{
    diagnostic->diagnostic_phase = UVM_STALE_STATE_V1_DIAG_SELECTED;
    diagnostic->region_result = region_result;
    diagnostic->initial_effect = initial_effect;
    if (diagnostic->status == UVM_STALE_STATE_V1_STATUS_DECISION_READY) {
        atomic64_inc(&g_uvm_stale_state_v1_stats.decision_records);
        if ((region_result != NV_GPU_TRANSITION_APPLY) ||
            (initial_effect != NV_GPU_PREFETCH_INITIAL_BYPASS)) {
            diagnostic->status = UVM_STALE_STATE_V1_STATUS_EFFECT_ERROR;
            atomic64_inc(&g_uvm_stale_state_v1_stats.effect_errors);
        }
    }
    atomic64_inc(&g_uvm_stale_state_v1_stats.selected_diagnostics);
    uvm_stale_state_v1_diagnostic(diagnostic);
}

void uvm_stale_state_v1_finished(
    struct uvm_stale_state_v1_diagnostic *diagnostic,
    const uvm_va_block_region_t *output)
{
    bool output_matches;

    diagnostic->diagnostic_phase = UVM_STALE_STATE_V1_DIAG_FINISHED;
    diagnostic->output_first = output->first;
    diagnostic->output_outer = output->outer;
    if (diagnostic->status == UVM_STALE_STATE_V1_STATUS_DECISION_READY) {
        output_matches =
            (diagnostic->action == NV_GPU_STALE_STATE_V1_ACTION_PREFETCH_MAX) ?
                ((diagnostic->output_first == diagnostic->input.max_first) &&
                 (diagnostic->output_outer == diagnostic->input.max_outer)) :
                ((diagnostic->output_first == 0) &&
                 (diagnostic->output_outer == 0));
        if (output_matches) {
            diagnostic->status = UVM_STALE_STATE_V1_STATUS_EFFECT_APPLIED;
            atomic64_inc(&g_uvm_stale_state_v1_stats.effect_records);
        }
        else {
            diagnostic->status = UVM_STALE_STATE_V1_STATUS_EFFECT_ERROR;
            atomic64_inc(&g_uvm_stale_state_v1_stats.effect_errors);
        }
    }
    atomic64_inc(&g_uvm_stale_state_v1_stats.finished_diagnostics);
    uvm_stale_state_v1_diagnostic(diagnostic);
    uvm_stale_state_v1_callback_put();
}

noinline void uvm_stale_state_v1_diagnostic(
    const struct uvm_stale_state_v1_diagnostic *diagnostic)
{
    if (diagnostic != NULL)
        barrier();
}
