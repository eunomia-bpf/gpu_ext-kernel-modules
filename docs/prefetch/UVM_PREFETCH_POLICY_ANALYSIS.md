# NVIDIA UVM Prefetch Policy 完整分析与 BPF 扩展方案

## 目录
1. [Driver 当前实现分析](#1-driver-当前实现分析)
2. [现有 BPF Policy 实现](#2-现有-bpf-policy-实现)
3. [符合 OSDI/IPDPS 标准的 Policy 设计](#3-符合-osdiipdps-标准的-policy-设计)
4. [推荐实现方案](#4-推荐实现方案)

---

## 1. Driver 当前实现分析

### 1.1 核心算法：Tree-based Prefetcher

**文件位置**: `kernel-open/nvidia-uvm/uvm_perf_prefetch.c:103-173`

#### 算法流程

```
uvm_perf_prefetch_get_hint_va_block()                [Line 474]
  └─> uvm_perf_prefetch_prenotify_fault_migrations() [Line 354]
      ├─> init_bitmap_tree_from_region()             [初始化二叉树]
      ├─> update_bitmap_tree_from_va_block()         [更新树节点计数]
      └─> compute_prefetch_region()                   [Line 103] ← 核心算法
          └─> uvm_perf_prefetch_bitmap_tree_traverse_counters()
              ├─> 从叶子节点向上遍历二叉树
              ├─> 检查每个子区域的 occupancy (counter / subregion_pages)
              └─> 如果 occupancy > threshold (51%)，选择该子区域
```

#### 关键代码分析

**compute_prefetch_region() 的 BPF 集成** (Line 112-147):

```c
static uvm_va_block_region_t compute_prefetch_region(
    uvm_page_index_t page_index,
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t max_prefetch_region)
{
    // 第一个 BPF hook: before_compute (可以完全绕过原算法)
    action = uvm_bpf_call_before_compute_prefetch(page_index, bitmap_tree,
                                                   &max_prefetch_region, &prefetch_region);

    if (action == UVM_BPF_ACTION_BYPASS) {
        // BPF 直接设置了结果，跳过所有计算
        return prefetch_region;
    }
    else if (action == UVM_BPF_ACTION_ENTER_LOOP) {
        // 使用树遍历，但每次迭代调用 BPF
        uvm_perf_prefetch_bitmap_tree_traverse_counters(counter, bitmap_tree, ...) {
            uvm_bpf_call_on_tree_iter(..., counter, subregion_pages);
        }
    }
    else {
        // UVM_BPF_ACTION_DEFAULT: 使用原始内核逻辑
        uvm_perf_prefetch_bitmap_tree_traverse_counters(counter, bitmap_tree, ...) {
            if (counter * 100 > subregion_pages * g_uvm_perf_prefetch_threshold)
                prefetch_region = subregion;
        }
    }
}
```

#### 数据结构：Bitmap Tree

**定义**: `kernel-open/nvidia-uvm/uvm_perf_prefetch.h`

```c
typedef struct {
    // 满二叉树结构
    unsigned level_count;        // 树的层数
    unsigned leaf_count;         // 叶子节点数（页面数）
    uvm_page_index_t offset;     // 相对于 VA block 的偏移

    // 每个节点的位图
    uvm_page_mask_t pages;       // 标记哪些页面已经在目标处理器上
} uvm_perf_prefetch_bitmap_tree_t;
```

**树结构示例** (对于 2MB block / 4KB page):
```
Level 0 (Root):     [0-511]                    (512 pages = 2MB)
                       |
Level 1:        [0-255] [256-511]              (256 pages = 1MB)
                  |         |
Level 2:      [0-127] ... [384-511]            (128 pages = 512KB)
                ...        ...
Level N (Leaf): [0-7] ... [504-511]            (8 pages = 32KB)
```

### 1.2 Prefetch 触发条件

**位置**: `uvm_perf_prefetch.c:354-409`

#### 条件 1: 最小 Fault 次数
```c
// 默认：1 次 fault 就触发
unsigned uvm_perf_prefetch_min_faults = 1;  // Line 57
```

#### 条件 2: 单一目标处理器
```c
// uvm_va_block.c:11837
if (uvm_processor_mask_get_count(&service_context->resident_processors) == 1) {
    // 只有当所有 faults 迁移到同一个处理器时才启用 prefetch
    uvm_perf_prefetch_get_hint_va_block(...);
}
```

#### 条件 3: First-touch 优化
```c
// Line 390-393
if (uvm_processor_mask_empty(&va_block->resident) &&
    uvm_id_equal(new_residency, policy->preferred_location)) {
    // 如果是第一次访问且目标是 preferred location，预取整个区域
    uvm_page_mask_region_fill(prefetch_pages, max_prefetch_region);
}
```

#### 条件 4: Thrashing 检测集成
```c
// Line 404-408
const uvm_page_mask_t *thrashing_pages = uvm_perf_thrashing_get_thrashing_pages(va_block);

// 排除 thrashing 页面
if (thrashing_pages)
    uvm_page_mask_andnot(&va_block_context->scratch_page_mask, faulted_pages, thrashing_pages);
```

### 1.3 可配置参数

| 参数 | 默认值 | 范围 | 位置 | 说明 |
|------|--------|------|------|------|
| `uvm_perf_prefetch_enable` | 1 | 0/1 | Line 40 | 全局开关 |
| `uvm_perf_prefetch_threshold` | 51% | 1-100 | Line 49 | Occupancy 阈值 |
| `uvm_perf_prefetch_min_faults` | 1 | 1-20 | Line 57 | 最小 fault 数 |

### 1.4 与 IPDPS'20 论文的对应

**论文**: "Adaptive Page Migration for Irregular Data-Intensive Applications under GPU Memory Oversubscription"

| 论文描述 | Driver 实现 | 验证状态 |
|---------|-----------|---------|
| **Tree-based neighborhood prefetcher** | `compute_prefetch_region()` | ✅ 完全一致 |
| **2MB block → 64KB subblocks** | `bitmap_tree` 多级树 | ✅ |
| **50% occupancy 阈值** | `threshold = 51%` | ✅ |
| **自底向上遍历** | `traverse_counters` 向上 | ✅ |
| **First-touch 全区域预取** | Line 390-393 | ✅ |
| **Thrashing 检测** | `thrashing_pages` 排除 | ✅ |

---

## 2. 现有 BPF Policy 实现

### 2.1 Policy 1: `prefetch_none.bpf.c`

**策略**: 完全禁用 prefetch

```c
SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute, ..., uvm_va_block_region_t *result_region)
{
    // 设置空区域
    bpf_uvm_set_va_block_region(result_region, 0, 0);

    return 1; // UVM_BPF_ACTION_BYPASS
}
```

**适用场景**:
- 随机访问模式（无空间局部性）
- 内存受限环境（避免不必要的迁移）
- Benchmark baseline（对比 prefetch 效果）

**性能特征**:
- ✅ 零预取开销
- ❌ 无法利用空间局部性
- ❌ 每个页面都需要 fault

### 2.2 Policy 2: `prefetch_always_max.bpf.c`

**策略**: 总是预取最大区域

```c
SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute,
             uvm_page_index_t page_index,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *result_region)
{
    // 读取 max_prefetch_region
    uvm_page_index_t max_first = BPF_CORE_READ(max_prefetch_region, first);
    uvm_page_index_t max_outer = BPF_CORE_READ(max_prefetch_region, outer);

    // 设置为整个最大区域
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);

    return 1; // UVM_BPF_ACTION_BYPASS
}
```

**适用场景**:
- 顺序访问模式（高空间局部性）
- GPU 内存充足（可以容纳大量预取数据）
- 流式计算（如矩阵乘法、卷积）

**性能特征**:
- ✅ 最大化利用空间局部性
- ✅ 最小化后续 fault 次数
- ❌ 可能预取不会使用的页面
- ❌ 高内存带宽消耗

### 2.3 Policy 3: `struct_ops.bpf.c`

**策略**: 测试用 policy（只实现 test trigger）

**说明**: 这个文件只实现了 `uvm_bpf_test_trigger_kfunc`，没有实现 prefetch hooks。
主要用于验证 struct_ops 框架和 kfunc 调用。

---

## 3. 符合 OSDI/IPDPS 标准的 Policy 设计

### 3.1 标准 Policy 分类

根据 OSDI/IPDPS 论文（GPU 内存管理领域），prefetch policy 通常分为以下几类：

#### 类别 1: **Reactive Policies** (反应式策略)
- **特点**: 基于已发生的 fault 历史做决策
- **代表**:
  - LRU-based prefetch
  - Fault frequency-based prefetch
  - Driver 当前的 tree-based prefetcher

#### 类别 2: **Predictive Policies** (预测式策略)
- **特点**: 基于访问模式预测未来访问
- **代表**:
  - Stride-based prefetch (步长预测)
  - Markov-based prefetch (马尔可夫链)
  - ML-based prefetch (机器学习)

#### 类别 3: **Hybrid Policies** (混合策略)
- **特点**: 结合多种策略的优势
- **代表**:
  - Adaptive prefetch (根据负载动态调整)
  - Multi-level prefetch (不同粒度组合)

### 3.2 Driver 当前策略评估

**Driver 的 Tree-based Prefetcher 属于**: **Reactive + Adaptive**

**优点**:
- ✅ 符合 IPDPS'20 论文标准实现
- ✅ 自适应阈值（51% occupancy）
- ✅ 多级粒度（二叉树结构）
- ✅ Thrashing 检测集成

**局限性**:
- ⚠️ 只考虑当前 fault batch（无历史信息）
- ⚠️ 固定阈值（51%）不适应所有工作负载
- ⚠️ 不支持跨 VA block 的模式识别
- ⚠️ 无 stride detection（步长预测）

### 3.3 推荐的 OSDI 标准 Policies

基于文献综述和 driver 能力，以下 5 个 policy 值得实现：

---

#### Policy A: **Adaptive Threshold Prefetch** (自适应阈值)

**论文依据**: ASPLOS'14 "Mosaic: A GPU Memory Manager..."

**核心思想**: 根据 GPU 内存压力动态调整 threshold

```c
// 伪代码
if (gpu_memory_usage > 90%) {
    threshold = 75%;  // 更保守，减少不必要的预取
} else if (gpu_memory_usage < 50%) {
    threshold = 25%;  // 更激进，充分利用空闲内存
} else {
    threshold = 51%;  // 默认值
}
```

**BPF 实现方式**:
- 使用 `ENTER_LOOP` 模式
- 在 `on_tree_iter` hook 中动态计算阈值
- 通过 BPF map 维护 GPU 内存使用率

**适用场景**:
- 内存压力变化的工作负载
- 多 GPU 环境
- 共享 GPU 场景

---

#### Policy B: **Stride-based Prefetch** (步长预测)

**论文依据**: MICRO'12 "Stride Directed Prefetching..."

**核心思想**: 检测连续 fault 的步长模式，预测下一个访问

```c
// 检测步长
stride = current_fault_page - last_fault_page;

if (stride == detected_stride) {
    // 预测下一个访问
    next_page = current_fault_page + stride;
    prefetch_region = [next_page, next_page + stride * prefetch_degree];
}
```

**BPF 实现方式**:
- 使用 `BYPASS` 模式
- 通过 BPF hash map 记录每个 VA block 的访问历史
- 检测连续 fault 的步长

**适用场景**:
- 规律的顺序/跳跃访问（矩阵行/列遍历）
- Stencil 计算
- 图遍历（固定步长）

---

#### Policy C: **Multi-level Prefetch** (多级预取)

**论文依据**: ISCA'19 "Tigr: Transforming Irregular Graphs..."

**核心思想**: 对于不同置信度使用不同预取粒度

```c
if (occupancy > 90%) {
    // 高置信度：预取大区域 (256KB)
    prefetch_level = 2;
} else if (occupancy > 60%) {
    // 中置信度：预取中等区域 (64KB)
    prefetch_level = 1;
} else if (occupancy > 30%) {
    // 低置信度：预取小区域 (16KB)
    prefetch_level = 0;
}
```

**BPF 实现方式**:
- 使用 `ENTER_LOOP` 模式
- 在树遍历中根据 counter 动态选择粒度
- 可以在 `on_tree_iter` 中修改 `current_region`

**适用场景**:
- 不规则访问模式
- 图算法
- 稀疏矩阵计算

---

#### Policy D: **Thrashing-aware Conservative Prefetch** (Thrashing 感知)

**论文依据**: HPDC'18 "Efficient Memory Virtualization for GPUs"

**核心思想**: 在 thrashing 区域完全禁用 prefetch，避免加剧 thrashing

```c
if (page_in_thrashing_region(page_index, thrashing_map)) {
    prefetch_region = empty;  // 不预取
} else {
    // 使用默认策略
    prefetch_region = compute_default_region(...);
}
```

**BPF 实现方式**:
- 使用 `BYPASS` 模式
- 通过 kfunc 查询 thrashing pages (driver 已有接口)
- 完全跳过 thrashing 区域

**适用场景**:
- 内存超额订阅 (oversubscription)
- 多进程竞争 GPU 内存
- Working set > GPU 内存

---

#### Policy E: **Probabilistic Prefetch** (概率式预取)

**论文依据**: SIGMETRICS'15 "Probability-based Prefetching"

**核心思想**: 根据历史命中率计算预取概率

```c
// 维护每个子区域的预取命中率
hit_rate = prefetch_hits / prefetch_total;

// 根据命中率决定是否预取
if (random() < hit_rate * aggressiveness) {
    prefetch_region = subregion;
}
```

**BPF 实现方式**:
- 使用 `ENTER_LOOP` 模式
- BPF map 记录每个子区域的命中率统计
- `bpf_get_prandom_u32()` 生成随机数

**适用场景**:
- 访问模式随时间变化
- A/B 测试不同 policy
- 在线学习最优策略

---

## 4. 推荐实现方案

### 4.1 优先级排序

根据**实现复杂度**、**性能收益**和**论文引用频率**：

| Priority | Policy | 复杂度 | 预期收益 | OSDI 相关性 |
|----------|--------|--------|---------|-----------|
| **P0** | **Adaptive Threshold** | 低 | 高 | ⭐⭐⭐⭐⭐ |
| **P1** | **Thrashing-aware Conservative** | 低 | 中 | ⭐⭐⭐⭐ |
| **P2** | **Multi-level Prefetch** | 中 | 高 | ⭐⭐⭐⭐⭐ |
| **P3** | **Stride-based** | 中 | 中 | ⭐⭐⭐ |
| **P4** | **Probabilistic** | 高 | 中 | ⭐⭐ |

### 4.2 P0: Adaptive Threshold 实现示例

**文件**: `prefetch_adaptive_threshold.bpf.c`

```c
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "uvm_types.h"

// BPF map: 维护全局统计
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32);
    __type(value, u64);
} gpu_stats SEC(".maps");

// 根据 GPU 内存使用率动态调整阈值
static inline unsigned int get_adaptive_threshold(void)
{
    u32 key = 0;
    u64 *mem_usage = bpf_map_lookup_elem(&gpu_stats, &key);

    if (!mem_usage)
        return 51;  // 默认阈值

    // 简化的自适应逻辑
    if (*mem_usage > 90)
        return 75;  // 内存紧张：更保守
    else if (*mem_usage < 50)
        return 30;  // 内存充足：更激进
    else
        return 51;
}

SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute,
             uvm_page_index_t page_index,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *result_region)
{
    // 返回 ENTER_LOOP，让 driver 遍历树
    return 2; // UVM_BPF_ACTION_ENTER_LOOP
}

SEC("struct_ops/uvm_prefetch_on_tree_iter")
int BPF_PROG(uvm_prefetch_on_tree_iter,
             uvm_page_index_t page_index,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *current_region,
             unsigned int counter,
             unsigned int subregion_pages)
{
    unsigned int threshold = get_adaptive_threshold();

    // 动态阈值判断
    if (counter * 100 > subregion_pages * threshold) {
        bpf_printk("Adaptive: counter=%u/%u, threshold=%u%%, prefetch!\n",
                   counter, subregion_pages, threshold);
        // 返回非零值表示选择这个区域
        // （需要 driver 支持从 on_tree_iter 返回结果）
    }

    return 0;
}

SEC(".struct_ops")
struct uvm_gpu_ext uvm_ops_adaptive = {
    .uvm_prefetch_before_compute = (void *)uvm_prefetch_before_compute,
    .uvm_prefetch_on_tree_iter = (void *)uvm_prefetch_on_tree_iter,
};
```

### 4.3 需要的 Driver 改进

当前 driver 的 BPF 集成已经很完善，但以下改进可以支持更复杂的 policy：

#### 改进 1: 支持从 `on_tree_iter` 修改 `prefetch_region`

**当前**: `on_tree_iter` 只能观察，不能修改结果

**建议**:
```c
// uvm_perf_prefetch.c:130
int ret = uvm_bpf_call_on_tree_iter(..., &subregion, ...);
if (ret > 0) {
    // BPF 选择了这个子区域
    prefetch_region = subregion;
}
```

#### 改进 2: 暴露更多上下文信息给 BPF

**当前**: BPF 只能看到 `bitmap_tree` 和 region

**建议**: 通过 kfunc 暴露：
- GPU 内存使用率
- 当前 VA block 的 thrashing 状态
- 历史 prefetch 命中率

```c
__bpf_kfunc u32 bpf_uvm_get_gpu_memory_usage(void);
__bpf_kfunc bool bpf_uvm_is_page_thrashing(uvm_page_index_t page_index);
```

#### 改进 3: BPF map 持久化支持

**建议**: 允许 BPF 程序在 `uvm_gpu_ext` 中定义自己的 map，用于跨 fault 维护状态

### 4.4 测试和评估方案

#### Benchmark Suite

| Benchmark | 访问模式 | 预期最优 Policy |
|-----------|---------|----------------|
| **LULESH** | Stencil (规律) | Adaptive Threshold / Stride |
| **BFS** | 随机跳跃 | Conservative / None |
| **SpMV** | 稀疏不规则 | Multi-level |
| **Matrix Multiply** | 顺序块状 | Always Max / Adaptive |
| **PageRank** | 图遍历 | Thrashing-aware |

#### 评估指标

1. **Page Fault Rate**: faults per second
2. **Prefetch Accuracy**: useful prefetches / total prefetches
3. **Memory Bandwidth**: GB/s (lower is better for prefetch overhead)
4. **Application Performance**: execution time
5. **GPU Memory Utilization**: peak usage

---

## 5. 总结

### 5.1 Driver 现状

NVIDIA UVM driver 的 prefetch 实现：
- ✅ 完全符合 IPDPS'20 论文标准
- ✅ 已集成 BPF struct_ops 扩展点
- ✅ 支持三种 BPF action 模式（BYPASS, ENTER_LOOP, DEFAULT）
- ⚠️ 固定阈值 (51%) 不适应所有场景
- ⚠️ 缺少跨 fault 的历史信息

### 5.2 现有 BPF Policy

1. **prefetch_none**: 基线对比，禁用 prefetch
2. **prefetch_always_max**: 激进策略，适合顺序访问
3. ⚠️ **缺少中间地带的策略**（如自适应、多级）

### 5.3 推荐实施路径

**Phase 1** (符合 OSDI 基础标准):
1. 实现 **Adaptive Threshold Prefetch**
2. 实现 **Thrashing-aware Conservative Prefetch**
3. 编写测试用例和 benchmark

**Phase 2** (高级优化):
1. 实现 **Multi-level Prefetch**
2. 实现 **Stride-based Prefetch**
3. 性能对比和论文撰写

**Phase 3** (可选/研究方向):
1. 实现 **Probabilistic Prefetch**
2. ML-based policy (需要离线训练)

### 5.4 论文贡献点

如果要投稿 OSDI/SOSP/ATC:

**主要贡献**:
1. **首个 GPU UVM prefetch 的 BPF 扩展框架** (系统贡献)
2. **5+ 种符合标准的 policy 实现** (算法贡献)
3. **真实工作负载评估** (实验贡献)
4. **零内核修改的可插拔设计** (工程贡献)

**Novelty**:
- BPF struct_ops 在 GPU 内存管理中的首次应用
- 用户态可编程的 prefetch policy
- 对比 NVIDIA 闭源 driver 的改进

---

**文档版本**: v1.0
**创建时间**: 2025-11-23
**作者**: UVM BPF Extension Project
**参考代码**:
- `kernel-open/nvidia-uvm/uvm_perf_prefetch.c`
- `gpu_ext_policy/src/*.bpf.c`
