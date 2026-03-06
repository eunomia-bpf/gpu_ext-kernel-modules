#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/bpf_verifier.h>
#include "uvm_bpf_struct_ops.h"
#include "uvm_migrate.h"

/* Compatibility definitions for lower kernel versions */
#ifndef BTF_SET8_KFUNCS
/* This flag implies BTF_SET8 holds kfunc(s) */
#define BTF_SET8_KFUNCS		(1 << 0)
#endif

#ifndef BTF_KFUNCS_START
#define BTF_KFUNCS_START(name) static struct btf_id_set8 __maybe_unused name = { .flags = BTF_SET8_KFUNCS };
#endif

#ifndef BTF_KFUNCS_END
#define BTF_KFUNCS_END(name)
#endif

/* Shared struct_ops definition between kernel module and BPF program */
struct gpu_mem_ops {
	int (*gpu_test_trigger)(const char *buf, int len);

	/* Prefetch hooks */
	int (*gpu_page_prefetch)(
		uvm_page_index_t page_index,
		uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
		uvm_va_block_region_t *max_prefetch_region,
		uvm_va_block_region_t *result_region);

	int (*gpu_page_prefetch_iter)(
		uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
		uvm_va_block_region_t *max_prefetch_region,
		uvm_va_block_region_t *current_region,
		unsigned int counter,
		uvm_va_block_region_t *prefetch_region);

	/* PMM eviction policy hooks */
	int (*gpu_block_activate)(
		uvm_pmm_gpu_t *pmm,
		uvm_gpu_chunk_t *chunk,
		struct list_head *list);

	int (*gpu_block_access)(
		uvm_pmm_gpu_t *pmm,
		uvm_gpu_chunk_t *chunk,
		struct list_head *list);

	int (*gpu_evict_prepare)(
		uvm_pmm_gpu_t *pmm,
		struct list_head *va_block_used,
		struct list_head *va_block_unused);
};


/* Define our custom struct_ops operations */
/* Global instance that BPF programs will implement */
static struct gpu_mem_ops __rcu *uvm_ops;

/* Proc file to trigger the struct_ops */
static struct proc_dir_entry *trigger_file;

/* CFI stub functions - required for struct_ops */
static int gpu_mem_ops__gpu_test_trigger(const char *buf, int len)
{
	return 0;
}

static int gpu_mem_ops__gpu_page_prefetch(
	uvm_page_index_t page_index,
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *result_region)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int gpu_mem_ops__gpu_page_prefetch_iter(
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *current_region,
	unsigned int counter,
	uvm_va_block_region_t *prefetch_region)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int gpu_mem_ops__gpu_block_activate(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int gpu_mem_ops__gpu_block_access(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int gpu_mem_ops__gpu_evict_prepare(
	uvm_pmm_gpu_t *pmm,
	struct list_head *va_block_used,
	struct list_head *va_block_unused)
{
	return UVM_BPF_ACTION_DEFAULT;
}

/* CFI stubs structure */
static struct gpu_mem_ops __bpf_ops_gpu_mem_ops = {
	.gpu_test_trigger = gpu_mem_ops__gpu_test_trigger,
	.gpu_page_prefetch = gpu_mem_ops__gpu_page_prefetch,
	.gpu_page_prefetch_iter = gpu_mem_ops__gpu_page_prefetch_iter,
	.gpu_block_activate = gpu_mem_ops__gpu_block_activate,
	.gpu_block_access = gpu_mem_ops__gpu_block_access,
	.gpu_evict_prepare = gpu_mem_ops__gpu_evict_prepare,
};

/* Begin kfunc definitions */
__bpf_kfunc_start_defs();

/* Define the bpf_gpu_strstr kfunc */
__bpf_kfunc int bpf_gpu_strstr(const char *str, u32 str__sz, const char *substr, u32 substr__sz)
{
	// For test only, not functional
	return -1;
}

/* Set the prefetch region - allows BPF to read and modify the region */
__bpf_kfunc void bpf_gpu_set_prefetch_region(uvm_va_block_region_t *region,
					     uvm_page_index_t first,
					     uvm_page_index_t outer)
{
	if (!region)
		return;
	region->first = first;
	region->outer = outer;
}

/* Move chunk to head of the list (makes it highest priority for eviction) */
__bpf_kfunc void bpf_gpu_block_move_head(uvm_gpu_chunk_t *chunk,
					     struct list_head *list)
{
	if (!chunk || !list)
		return;

	/* Verify the chunk is already in a list before moving */
	if (list_empty(&chunk->list))
		return;

	list_move(&chunk->list, list);
}

/* Move chunk to tail of the list (makes it lowest priority for eviction) */
__bpf_kfunc void bpf_gpu_block_move_tail(uvm_gpu_chunk_t *chunk,
					     struct list_head *list)
{
	if (!chunk || !list)
		return;

	/* Verify the chunk is already in a list before moving */
	if (list_empty(&chunk->list))
		return;

	list_move_tail(&chunk->list, list);
}

/* ===== Cross-block prefetch kfunc ===== */

/* Migrate a VA range to GPU — sleepable kfunc.
 * Must be called from sleepable BPF context (e.g., bpf_wq callback).
 * Acquires va_space read lock internally via uvm_migrate_bpf().
 * Caller must ensure va_space_handle is valid (obtained from same process
 * context, e.g., during active benchmark). */
__bpf_kfunc int bpf_gpu_migrate_range(u64 va_space_handle, u64 addr, u64 length)
{
	uvm_va_space_t *va_space = (uvm_va_space_t *)va_space_handle;
	if (!va_space || !length)
		return -EINVAL;
	return (int)uvm_migrate_bpf(va_space, addr, length);
}

/* End kfunc definitions */
__bpf_kfunc_end_defs();

/* Define the BTF kfuncs ID set */
BTF_KFUNCS_START(uvm_bpf_kfunc_ids_set)
BTF_ID_FLAGS(func, bpf_gpu_strstr)
BTF_ID_FLAGS(func, bpf_gpu_set_prefetch_region, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_gpu_block_move_head, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_gpu_block_move_tail, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_gpu_migrate_range, KF_SLEEPABLE)
BTF_KFUNCS_END(uvm_bpf_kfunc_ids_set)

/* Register the kfunc ID set */
static const struct btf_kfunc_id_set uvm_bpf_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &uvm_bpf_kfunc_ids_set,
};

/* BTF and verifier callbacks */
static int gpu_mem_ops_init(struct btf *btf)
{
	/* Initialize BTF if needed */
	return 0;
}

static bool gpu_mem_ops_is_valid_access(int off, int size,
					    enum bpf_access_type type,
					    const struct bpf_prog *prog,
					    struct bpf_insn_access_aux *info)
{
	/* Use BTF-based context access to properly handle pointer types */
	return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

/* Allow specific BPF helpers to be used in struct_ops programs */
static const struct bpf_func_proto *
gpu_mem_ops_get_func_proto(enum bpf_func_id func_id,
			       const struct bpf_prog *prog)
{
	/* Use base func proto which includes trace_printk and other basic helpers */
	return bpf_base_func_proto(func_id, prog);
}

static const struct bpf_verifier_ops gpu_mem_ops_verifier_ops = {
	.is_valid_access = gpu_mem_ops_is_valid_access,
	.get_func_proto = gpu_mem_ops_get_func_proto,
};

static int gpu_mem_ops_init_member(const struct btf_type *t,
				       const struct btf_member *member,
				       void *kdata, const void *udata)
{
	/* No special member initialization needed */
	return 0;
}

/* Registration function */
static int gpu_mem_ops_reg(void *kdata, struct bpf_link *link)
{
	struct gpu_mem_ops *ops = kdata;

	/* Only one instance at a time */
	if (cmpxchg(&uvm_ops, NULL, ops) != NULL)
		return -EEXIST;

	pr_info("gpu_mem_ops registered in nvidia-uvm\n");
	return 0;
}

/* Unregistration function */
static void gpu_mem_ops_unreg(void *kdata, struct bpf_link *link)
{
	struct gpu_mem_ops *ops = kdata;

	if (cmpxchg(&uvm_ops, ops, NULL) != ops) {
		pr_warn("gpu_mem_ops: unexpected unreg in nvidia-uvm\n");
		return;
	}

	pr_info("gpu_mem_ops unregistered from nvidia-uvm\n");
}

/* Struct ops definition */
static struct bpf_struct_ops gpu_mem_ops_struct_ops = {
	.verifier_ops = &gpu_mem_ops_verifier_ops,
	.init = gpu_mem_ops_init,
	.init_member = gpu_mem_ops_init_member,
	.reg = gpu_mem_ops_reg,
	.unreg = gpu_mem_ops_unreg,
	.cfi_stubs = &__bpf_ops_gpu_mem_ops,
	.name = "gpu_mem_ops",
	.owner = THIS_MODULE,
};

/* Proc file write handler to trigger struct_ops */
static ssize_t trigger_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct gpu_mem_ops *ops;
	char kbuf[64];
	int ret = 0;

	if (count >= sizeof(kbuf))
		count = sizeof(kbuf) - 1;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops) {
		pr_info("UVM: Calling struct_ops callbacks:\n");

		if (ops->gpu_test_trigger) {
			ret = ops->gpu_test_trigger(kbuf, count);
			pr_info("UVM: gpu_test_trigger() returned: %d\n", ret);
		}
	} else {
		pr_info("UVM: No struct_ops registered\n");
	}
	rcu_read_unlock();

	return count;
}

static const struct proc_ops trigger_proc_ops = {
	.proc_write = trigger_write,
};

int uvm_bpf_struct_ops_init(void)
{
	int ret;

	/* Register the kfunc ID set for struct_ops programs */
	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &uvm_bpf_kfunc_set);
	if (ret) {
		pr_err("UVM: Failed to register BTF kfunc ID set: %d\n", ret);
		return ret;
	}
	pr_info("UVM: kfunc ID set registered successfully\n");

	/* Also register for tracing programs (kprobe/uprobe) so they can call gpu kfuncs */
	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_KPROBE, &uvm_bpf_kfunc_set);
	if (ret) {
		pr_warn("UVM: Failed to register kfunc for kprobe progs: %d (non-fatal)\n", ret);
		/* Non-fatal: struct_ops still works */
	}

	/* Register the struct_ops */
	ret = register_bpf_struct_ops(&gpu_mem_ops_struct_ops, gpu_mem_ops);
	if (ret) {
		pr_err("UVM: Failed to register struct_ops: %d\n", ret);
		return ret;
	}

	/* Create proc file for triggering */
	trigger_file = proc_create("bpf_testmod_trigger", 0222, NULL, &trigger_proc_ops);
	if (!trigger_file)
		return -ENOMEM;

	pr_info("UVM: bpf_struct_ops initialized\n");
	return 0;
}

void uvm_bpf_struct_ops_exit(void)
{
	if (trigger_file)
		proc_remove(trigger_file);
	/* Note: struct_ops unregister happens automatically on module unload */
	pr_info("UVM: bpf_struct_ops cleaned up\n");
}

/* Wrapper functions for calling BPF hooks */
enum uvm_bpf_action uvm_bpf_call_gpu_page_prefetch(
	uvm_page_index_t page_index,
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *result_region)
{
	struct gpu_mem_ops *ops;
	int ret = UVM_BPF_ACTION_DEFAULT;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->gpu_page_prefetch) {
		ret = ops->gpu_page_prefetch(page_index, bitmap_tree,
						       max_prefetch_region,
						       result_region);
	}
	rcu_read_unlock();

	return (enum uvm_bpf_action)ret;
}

enum uvm_bpf_action uvm_bpf_call_gpu_page_prefetch_iter(
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *current_region,
	unsigned int counter,
	uvm_va_block_region_t *prefetch_region)
{
	struct gpu_mem_ops *ops;
	int ret = UVM_BPF_ACTION_DEFAULT;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->gpu_page_prefetch_iter) {
		ret = ops->gpu_page_prefetch_iter(bitmap_tree,
						     max_prefetch_region, current_region,
						     counter, prefetch_region);
	}
	rcu_read_unlock();

	return (enum uvm_bpf_action)ret;
}

/* PMM eviction policy hook wrappers */
void uvm_bpf_call_gpu_block_activate(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	struct gpu_mem_ops *ops;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->gpu_block_activate) {
		ops->gpu_block_activate(pmm, chunk, list);
	}
	rcu_read_unlock();
}

enum uvm_bpf_action uvm_bpf_call_gpu_block_access(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	struct gpu_mem_ops *ops;
	int ret = UVM_BPF_ACTION_DEFAULT;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->gpu_block_access) {
		ret = ops->gpu_block_access(pmm, chunk, list);
	}
	rcu_read_unlock();

	return (enum uvm_bpf_action)ret;
}

void uvm_bpf_call_gpu_evict_prepare(
	uvm_pmm_gpu_t *pmm,
	struct list_head *va_block_used,
	struct list_head *va_block_unused)
{
	struct gpu_mem_ops *ops;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->gpu_evict_prepare) {
		ops->gpu_evict_prepare(pmm, va_block_used, va_block_unused);
	}
	rcu_read_unlock();
}
