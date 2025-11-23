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

/* Define our custom struct_ops operations */
/* Global instance that BPF programs will implement */
static struct bpf_testmod_ops __rcu *testmod_ops;

/* Proc file to trigger the struct_ops */
static struct proc_dir_entry *trigger_file;

/* CFI stub functions - required for struct_ops */
static int bpf_testmod_ops__test_1(void)
{
	return 0;
}

static int bpf_testmod_ops__test_2(int a, int b)
{
	return 0;
}

static int bpf_testmod_ops__test_3(const char *buf, int len)
{
	return 0;
}

/* CFI stubs structure */
static struct bpf_testmod_ops __bpf_ops_bpf_testmod_ops = {
	.test_1 = bpf_testmod_ops__test_1,
	.test_2 = bpf_testmod_ops__test_2,
	.test_3 = bpf_testmod_ops__test_3,
};

/* Begin kfunc definitions */
__bpf_kfunc_start_defs();

/* Define the bpf_uvm_strstr kfunc */
__bpf_kfunc int bpf_uvm_strstr(const char *str, u32 str__sz, const char *substr, u32 substr__sz)
{
	// Edge case: if substr is empty, return 0 (assuming empty string is found at the start)
	if (substr__sz == 0)
	{
		return 0;
	}
	// Edge case: if the substring is longer than the main string, it's impossible to find
	if (substr__sz > str__sz)
	{
		return -1; // Return -1 to indicate not found
	}

	// Iterate through the main string, considering the size limit
	for (size_t i = 0; i <= str__sz - substr__sz; i++)
	{
		size_t j = 0;
		// Compare the substring with the current position in the string
		while (j < substr__sz && str[i + j] == substr[j])
		{
			j++;
		}
		// If the entire substring was found
		if (j == substr__sz)
		{
			return i; // Return the index of the first match
		}
	}
	// Return -1 if the substring is not found
	return -1;
}

/* End kfunc definitions */
__bpf_kfunc_end_defs();

/* Define the BTF kfuncs ID set */
BTF_KFUNCS_START(uvm_bpf_kfunc_ids_set)
BTF_ID_FLAGS(func, bpf_uvm_strstr)
BTF_KFUNCS_END(uvm_bpf_kfunc_ids_set)

/* Register the kfunc ID set */
static const struct btf_kfunc_id_set uvm_bpf_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &uvm_bpf_kfunc_ids_set,
};

/* BTF and verifier callbacks */
static int bpf_testmod_ops_init(struct btf *btf)
{
	/* Initialize BTF if needed */
	return 0;
}

static bool bpf_testmod_ops_is_valid_access(int off, int size,
					    enum bpf_access_type type,
					    const struct bpf_prog *prog,
					    struct bpf_insn_access_aux *info)
{
	/* Allow all accesses for now */
	return true;
}

/* Allow specific BPF helpers to be used in struct_ops programs */
static const struct bpf_func_proto *
bpf_testmod_ops_get_func_proto(enum bpf_func_id func_id,
			       const struct bpf_prog *prog)
{
	/* Use base func proto which includes trace_printk and other basic helpers */
	return bpf_base_func_proto(func_id, prog);
}

static const struct bpf_verifier_ops bpf_testmod_verifier_ops = {
	.is_valid_access = bpf_testmod_ops_is_valid_access,
	.get_func_proto = bpf_testmod_ops_get_func_proto,
};

static int bpf_testmod_ops_init_member(const struct btf_type *t,
				       const struct btf_member *member,
				       void *kdata, const void *udata)
{
	/* No special member initialization needed */
	return 0;
}

/* Registration function */
static int bpf_testmod_ops_reg(void *kdata, struct bpf_link *link)
{
	struct bpf_testmod_ops *ops = kdata;

	/* Only one instance at a time */
	if (cmpxchg(&testmod_ops, NULL, ops) != NULL)
		return -EEXIST;

	pr_info("bpf_testmod_ops registered in nvidia-uvm\n");
	return 0;
}

/* Unregistration function */
static void bpf_testmod_ops_unreg(void *kdata, struct bpf_link *link)
{
	struct bpf_testmod_ops *ops = kdata;

	if (cmpxchg(&testmod_ops, ops, NULL) != ops) {
		pr_warn("bpf_testmod_ops: unexpected unreg in nvidia-uvm\n");
		return;
	}

	pr_info("bpf_testmod_ops unregistered from nvidia-uvm\n");
}

/* Struct ops definition */
static struct bpf_struct_ops bpf_testmod_ops_struct_ops = {
	.verifier_ops = &bpf_testmod_verifier_ops,
	.init = bpf_testmod_ops_init,
	.init_member = bpf_testmod_ops_init_member,
	.reg = bpf_testmod_ops_reg,
	.unreg = bpf_testmod_ops_unreg,
	.cfi_stubs = &__bpf_ops_bpf_testmod_ops,
	.name = "bpf_testmod_ops",
	.owner = THIS_MODULE,
};

/* Proc file write handler to trigger struct_ops */
static ssize_t trigger_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct bpf_testmod_ops *ops;
	char kbuf[64];
	int ret = 0;

	if (count >= sizeof(kbuf))
		count = sizeof(kbuf) - 1;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	rcu_read_lock();
	ops = rcu_dereference(testmod_ops);
	if (ops) {
		pr_info("UVM: Calling struct_ops callbacks:\n");

		if (ops->test_1) {
			ret = ops->test_1();
			pr_info("UVM: test_1() returned: %d\n", ret);
		}

		if (ops->test_2) {
			ret = ops->test_2(10, 20);
			pr_info("UVM: test_2(10, 20) returned: %d\n", ret);
		}

		if (ops->test_3) {
			ops->test_3(kbuf, count);
			pr_info("UVM: test_3() called with buffer\n");
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
	pr_info("UVM: kfunc bpf_uvm_strstr registered successfully\n");

	/* Register the struct_ops */
	ret = register_bpf_struct_ops(&bpf_testmod_ops_struct_ops, bpf_testmod_ops);
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
