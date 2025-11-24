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
struct uvm_gpu_ext {
	int (*uvm_bpf_test_trigger_kfunc)(const char *buf, int len);

	/* Prefetch hooks */
	int (*uvm_prefetch_before_compute)(
		uvm_page_index_t page_index,
		uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
		uvm_va_block_region_t *max_prefetch_region,
		uvm_va_block_region_t *result_region);

	int (*uvm_prefetch_on_tree_iter)(
		uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
		uvm_va_block_region_t *max_prefetch_region,
		uvm_va_block_region_t *current_region,
		unsigned int counter,
		uvm_va_block_region_t *prefetch_region);

	/* PMM eviction policy hooks */
	int (*uvm_pmm_chunk_activate)(
		uvm_pmm_gpu_t *pmm,
		uvm_gpu_chunk_t *chunk,
		struct list_head *list);

	int (*uvm_pmm_chunk_populate)(
		uvm_pmm_gpu_t *pmm,
		uvm_gpu_chunk_t *chunk,
		struct list_head *list);

	int (*uvm_pmm_eviction_prepare)(
		uvm_pmm_gpu_t *pmm,
		struct list_head *va_block_used,
		struct list_head *va_block_unused);
};


/* Define our custom struct_ops operations */
/* Global instance that BPF programs will implement */
static struct uvm_gpu_ext __rcu *uvm_ops;

/* Proc file to trigger the struct_ops */
static struct proc_dir_entry *trigger_file;

/* CFI stub functions - required for struct_ops */
static int uvm_gpu_ext__uvm_bpf_test_trigger_kfunc(const char *buf, int len)
{
	return 0;
}

static int uvm_gpu_ext__uvm_prefetch_before_compute(
	uvm_page_index_t page_index,
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *result_region)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int uvm_gpu_ext__uvm_prefetch_on_tree_iter(
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *current_region,
	unsigned int counter,
	uvm_va_block_region_t *prefetch_region)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int uvm_gpu_ext__uvm_pmm_chunk_activate(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int uvm_gpu_ext__uvm_pmm_chunk_populate(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	return UVM_BPF_ACTION_DEFAULT;
}

static int uvm_gpu_ext__uvm_pmm_eviction_prepare(
	uvm_pmm_gpu_t *pmm,
	struct list_head *va_block_used,
	struct list_head *va_block_unused)
{
	return UVM_BPF_ACTION_DEFAULT;
}

/* CFI stubs structure */
static struct uvm_gpu_ext __bpf_ops_uvm_gpu_ext = {
	.uvm_bpf_test_trigger_kfunc = uvm_gpu_ext__uvm_bpf_test_trigger_kfunc,
	.uvm_prefetch_before_compute = uvm_gpu_ext__uvm_prefetch_before_compute,
	.uvm_prefetch_on_tree_iter = uvm_gpu_ext__uvm_prefetch_on_tree_iter,
	.uvm_pmm_chunk_activate = uvm_gpu_ext__uvm_pmm_chunk_activate,
	.uvm_pmm_chunk_populate = uvm_gpu_ext__uvm_pmm_chunk_populate,
	.uvm_pmm_eviction_prepare = uvm_gpu_ext__uvm_pmm_eviction_prepare,
};

/* Begin kfunc definitions */
__bpf_kfunc_start_defs();

/* Define the bpf_uvm_strstr kfunc */
__bpf_kfunc int bpf_uvm_strstr(const char *str, u32 str__sz, const char *substr, u32 substr__sz)
{
	// For test only, not functional
	return -1;
}

/* Set the prefetch region - allows BPF to read and modify the region */
__bpf_kfunc void bpf_uvm_set_va_block_region(uvm_va_block_region_t *region,
					     uvm_page_index_t first,
					     uvm_page_index_t outer)
{
	if (!region)
		return;
	region->first = first;
	region->outer = outer;
}

/* Move chunk to head of the list (makes it highest priority for eviction) */
__bpf_kfunc void bpf_uvm_pmm_chunk_move_head(uvm_gpu_chunk_t *chunk,
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
__bpf_kfunc void bpf_uvm_pmm_chunk_move_tail(uvm_gpu_chunk_t *chunk,
					     struct list_head *list)
{
	if (!chunk || !list)
		return;

	/* Verify the chunk is already in a list before moving */
	if (list_empty(&chunk->list))
		return;

	list_move_tail(&chunk->list, list);
}

/* End kfunc definitions */
__bpf_kfunc_end_defs();

/* Define the BTF kfuncs ID set */
BTF_KFUNCS_START(uvm_bpf_kfunc_ids_set)
BTF_ID_FLAGS(func, bpf_uvm_strstr)
BTF_ID_FLAGS(func, bpf_uvm_set_va_block_region, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_uvm_pmm_chunk_move_head, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_uvm_pmm_chunk_move_tail, KF_TRUSTED_ARGS)
BTF_KFUNCS_END(uvm_bpf_kfunc_ids_set)

/* Register the kfunc ID set */
static const struct btf_kfunc_id_set uvm_bpf_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &uvm_bpf_kfunc_ids_set,
};

/* BTF and verifier callbacks */
static int uvm_gpu_ext_init(struct btf *btf)
{
	/* Initialize BTF if needed */
	return 0;
}

static bool uvm_gpu_ext_is_valid_access(int off, int size,
					    enum bpf_access_type type,
					    const struct bpf_prog *prog,
					    struct bpf_insn_access_aux *info)
{
	/* Use BTF-based context access to properly handle pointer types */
	return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

/* Allow specific BPF helpers to be used in struct_ops programs */
static const struct bpf_func_proto *
uvm_gpu_ext_get_func_proto(enum bpf_func_id func_id,
			       const struct bpf_prog *prog)
{
	/* Use base func proto which includes trace_printk and other basic helpers */
	return bpf_base_func_proto(func_id, prog);
}

static const struct bpf_verifier_ops uvm_gpu_ext_verifier_ops = {
	.is_valid_access = uvm_gpu_ext_is_valid_access,
	.get_func_proto = uvm_gpu_ext_get_func_proto,
};

static int uvm_gpu_ext_init_member(const struct btf_type *t,
				       const struct btf_member *member,
				       void *kdata, const void *udata)
{
	/* No special member initialization needed */
	return 0;
}

/* Registration function */
static int uvm_gpu_ext_reg(void *kdata, struct bpf_link *link)
{
	struct uvm_gpu_ext *ops = kdata;

	/* Only one instance at a time */
	if (cmpxchg(&uvm_ops, NULL, ops) != NULL)
		return -EEXIST;

	pr_info("uvm_gpu_ext registered in nvidia-uvm\n");
	return 0;
}

/* Unregistration function */
static void uvm_gpu_ext_unreg(void *kdata, struct bpf_link *link)
{
	struct uvm_gpu_ext *ops = kdata;

	if (cmpxchg(&uvm_ops, ops, NULL) != ops) {
		pr_warn("uvm_gpu_ext: unexpected unreg in nvidia-uvm\n");
		return;
	}

	pr_info("uvm_gpu_ext unregistered from nvidia-uvm\n");
}

/* Struct ops definition */
static struct bpf_struct_ops uvm_gpu_ext_struct_ops = {
	.verifier_ops = &uvm_gpu_ext_verifier_ops,
	.init = uvm_gpu_ext_init,
	.init_member = uvm_gpu_ext_init_member,
	.reg = uvm_gpu_ext_reg,
	.unreg = uvm_gpu_ext_unreg,
	.cfi_stubs = &__bpf_ops_uvm_gpu_ext,
	.name = "uvm_gpu_ext",
	.owner = THIS_MODULE,
};

/* Proc file write handler to trigger struct_ops */
static ssize_t trigger_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct uvm_gpu_ext *ops;
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

		if (ops->uvm_bpf_test_trigger_kfunc) {
			ret = ops->uvm_bpf_test_trigger_kfunc(kbuf, count);
			pr_info("UVM: uvm_bpf_test_trigger_kfunc() returned: %d\n", ret);
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

	/* Register the struct_ops */
	ret = register_bpf_struct_ops(&uvm_gpu_ext_struct_ops, uvm_gpu_ext);
	if (ret) {
		pr_err("UVM: Failed to register struct_ops: %d\n", ret);
		return ret;
	}

	/* Create proc file for triggering */
	trigger_file = proc_create("bpf_testmod_trigger", 0222, NULL, &trigger_proc_ops);
	if (!trigger_file) {
		/* Note: No unregister function available in this kernel version */
		return -ENOMEM;
	}

	pr_info("UVM: bpf_struct_ops initialized with struct_ops support\n");
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
enum uvm_bpf_action uvm_bpf_call_before_compute_prefetch(
	uvm_page_index_t page_index,
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *result_region)
{
	struct uvm_gpu_ext *ops;
	int ret = UVM_BPF_ACTION_DEFAULT;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->uvm_prefetch_before_compute) {
		ret = ops->uvm_prefetch_before_compute(page_index, bitmap_tree,
						       max_prefetch_region, result_region);
	}
	rcu_read_unlock();

	return (enum uvm_bpf_action)ret;
}

enum uvm_bpf_action uvm_bpf_call_on_tree_iter(
	uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
	uvm_va_block_region_t *max_prefetch_region,
	uvm_va_block_region_t *current_region,
	unsigned int counter,
	uvm_va_block_region_t *prefetch_region)
{
	struct uvm_gpu_ext *ops;
	int ret = UVM_BPF_ACTION_DEFAULT;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->uvm_prefetch_on_tree_iter) {
		ret = ops->uvm_prefetch_on_tree_iter(bitmap_tree,
						     max_prefetch_region, current_region,
						     counter, prefetch_region);
	}
	rcu_read_unlock();

	return (enum uvm_bpf_action)ret;
}

/* PMM eviction policy hook wrappers */
void uvm_bpf_call_pmm_chunk_activate(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	struct uvm_gpu_ext *ops;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->uvm_pmm_chunk_activate) {
		ops->uvm_pmm_chunk_activate(pmm, chunk, list);
	}
	rcu_read_unlock();
}

void uvm_bpf_call_pmm_chunk_populate(
	uvm_pmm_gpu_t *pmm,
	uvm_gpu_chunk_t *chunk,
	struct list_head *list)
{
	struct uvm_gpu_ext *ops;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->uvm_pmm_chunk_populate) {
		ops->uvm_pmm_chunk_populate(pmm, chunk, list);
	}
	rcu_read_unlock();
}

void uvm_bpf_call_pmm_eviction_prepare(
	uvm_pmm_gpu_t *pmm,
	struct list_head *va_block_used,
	struct list_head *va_block_unused)
{
	struct uvm_gpu_ext *ops;

	rcu_read_lock();
	ops = rcu_dereference(uvm_ops);
	if (ops && ops->uvm_pmm_eviction_prepare) {
		ops->uvm_pmm_eviction_prepare(pmm, va_block_used, va_block_unused);
	}
	rcu_read_unlock();
}
