#ifndef _UVM_BPF_STRUCT_OPS_H
#define _UVM_BPF_STRUCT_OPS_H

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
struct bpf_testmod_ops {
	int (*test_1)(void);
	int (*test_2)(int a, int b);
	int (*test_3)(const char *buf, int len);
};

/* Function declarations for BPF struct_ops initialization */
int uvm_bpf_struct_ops_init(void);
void uvm_bpf_struct_ops_exit(void);

#endif /* _UVM_BPF_STRUCT_OPS_H */
