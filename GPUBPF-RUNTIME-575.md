# gpubpf 575 runtime preparation

Date: 2026-08-31

Branch `test-sched` is the gpubpf-enabled NVIDIA open-kernel-module source at
driver version 575.57.08. It contains the six-member `gpu_mem_ops` interface,
the three-member `nv_gpu_sched_ops` interface, and their registered kfuncs.

## Target-kernel qualification

The source does not compile against Linux 7.1.12 without a broad NVIDIA kernel
compatibility port. GCC 15 reaches the real compatibility failures, including
changed user-page pinning, read-only VMA flags, removed DMA helpers, and changed
platform/backlight interfaces. This path is not the shortest route to the
revision experiments.

The same source builds all five modules successfully for the installed Ubuntu
Linux 6.14.0-37 kernel with its matching GCC 13 compiler:

```sh
make clean
make modules -j8 KERNEL_UNAME=6.14.0-37-generic CC=/usr/bin/gcc-13
```

Ubuntu's headers omit `vmlinux`, so a normal out-of-tree build skips complete
module BTF. For this target kernel, extract its own base image, temporarily
expose it as the header tree's `vmlinux`, remove only the five generated module
files, and relink. Kbuild then runs pahole and resolve_btfids for every module.
Do not use a live or extracted base BTF from a different kernel.

The resulting modules report version 575.57.08 and vermagic
`6.14.0-37-generic SMP preempt mod_unload modversions`. Split-BTF inspection
against the extracted 6.14 base finds 6,287 core-module types and 12,411 UVM
types. The core module exposes `struct nv_gpu_sched_ops` and
`bpf_nv_gpu_preempt_tsg`; the UVM module exposes `struct gpu_mem_ops` and its
six callbacks.

These checks use versions, structured type inspection, exact commands, and
ordinary file metadata. Do not generate, refresh, compare, or record file or
content hashes, checksums, or digests as build or runtime evidence.

## Runtime boundary

No 575 module has been installed or loaded. The host currently runs Linux
7.1.12 with NVIDIA userspace and kernel modules 610.43.02, and GDM holds the
core display stack. NVIDIA requires matching kernel and userspace driver
releases, so the prepared 575 modules must not be mixed with the live 610
userspace.

The next runtime step requires an explicit maintenance-window authorization:

1. install matching 575.57.08 userspace without replacing these custom kernel
   modules;
2. install the five custom modules for Linux 6.14.0-37 and regenerate its
   initramfs;
3. reboot into Linux 6.14.0-37;
4. verify driver version, module BTF types/kfuncs, GPU idleness, and CUDA before
   either XSched or NVBit preflight.

This step changes the active graphics stack and reboots the machine. Do not
perform it as an unattended experiment action, do not stop GDM to hot-swap the
core module, and do not mix versions.
