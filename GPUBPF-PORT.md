# gpubpf port to NVIDIA 610.43.02

Branch: `port/nvidia-610.43.02`, based on NVIDIA commit `57130a27`.
The memory and scheduling integrations come from `test-sched` at `921567e1`
(575.57.08). Policy logic and the six-member `gpu_mem_ops` and three-member
`nv_gpu_sched_ops` interfaces are preserved.

## Port-specific changes

- Map PMM used/unused hooks to 610's `root_chunks.alloc_list[]`, retaining
  upstream's unused, discarded, then used eviction order. Activation callbacks
  receive the root chunk whose list node was moved.
- Preserve 610's BAR firewall and devfreq implementations when adding the
  cross-process preemption entry point.
- Use `NV_VERSION_STRING` in custom-module startup logs and unwind UVM tools
  if subsequent BPF initialization fails.
- Keep `KF_TRUSTED_ARGS` on older kernels. On kernels that removed the flag,
  trusted arguments are the default, so its compatibility value is zero.
  See Linux commit `7646c7afd9a95db0b0cb4ad066ed90f6024da67d`.

## Build evidence

On 2026-08-31, all five modules built successfully for
`6.15.11-061511-generic`, with BTF. After the host changed to
`7.1.12-070112-generic`, all five modules also compiled and linked using the
same GCC 14 toolchain recorded in the installed 610 DKMS build:

```sh
make -C kernel-open clean_conftest CC=/usr/bin/gcc-14
make modules -j"$(nproc)" CC=/usr/bin/gcc-14 IGNORE_CC_MISMATCH=1
```

The 7.1 kernel itself was built with GCC 15. GCC 14 emits compiler-version,
`counted_by`, and vendor-object objtool warnings; successful linking does not
establish runtime correctness. Cleaning conftests is necessary after a failed
build with a different compiler because cached negative feature tests persist.

These 7.1 headers omit `vmlinux`, so ordinary Kbuild skips module BTF. For
modules built for the **currently running kernel only**, use that kernel's
native BTF generation script and its live base BTF (pahole 1.30):

```sh
for mod in kernel-open/nvidia.ko kernel-open/nvidia-modeset.ko \
           kernel-open/nvidia-drm.ko kernel-open/nvidia-uvm.ko \
           kernel-open/nvidia-peermem.ko; do
  env PAHOLE="$PWD/kernel-open/pahole.sh" \
    PAHOLE_FLAGS='-j8 --btf_features=encode_force,var,float,enum64,decl_tag,type_tag,optimized_func,consistent_func,decl_tag_kfuncs,attributes --lang_exclude=rust' \
    RESOLVE_BTFIDS=/usr/src/linux-headers-7.1.12-070112-generic/tools/bpf/resolve_btfids/resolve_btfids \
    RESOLVE_BTFIDS_FLAGS=--distill_base OBJCOPY=objcopy \
    sh /usr/src/linux-headers-7.1.12-070112-generic/scripts/gen-btf.sh \
      --btf_base /sys/kernel/btf/vmlinux "$mod"
done
```

Run this once on newly linked modules without existing BTF sections. Never use
the live base BTF for a different target kernel. `bpftool btf dump file ...`
confirmed both struct_ops types and all nine gpubpf kfuncs; `modinfo` confirmed
610.43.02 and the target kernel's vermagic. No system headers or installed
modules were overwritten.

## Runtime status and safe next step

Runtime load/attach testing is pending. The host's GPU became idle briefly,
but unrelated SGLang compute processes returned before the final load check;
no module was unloaded or replaced. GDM also uses the core NVIDIA module.

When no process uses UVM and its reference count is zero, memory-hook testing
can replace only `nvidia_uvm`, retaining the matching official 610 core module.
Full scheduling-hook testing requires an authorized idle/display maintenance
window. Use temporary `insmod` only; do not run `modules_install`, overwrite
`/lib/modules`, kill unrelated processes, or clear their BPF registrations.
