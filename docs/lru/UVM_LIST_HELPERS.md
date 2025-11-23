# NVIDIA UVM List 辅助函数完全指南

## 概述

NVIDIA UVM 使用 **Linux 内核标准的双向链表** (`struct list_head`) 来管理内存块。本文档详细列出所有可用的 list 辅助函数和使用示例。

---

## 1. 数据结构

### 1.1 Linux 内核 list_head

```c
// Linux kernel: include/linux/list.h
struct list_head {
    struct list_head *next, *prev;
};
```

**特点**:
- ✅ **侵入式链表** (Intrusive List): 嵌入到数据结构内部
- ✅ **双向循环** (Doubly Linked Circular): 头部的 `prev` 指向尾部，尾部的 `next` 指向头部
- ✅ **类型安全**: 通过 `container_of` 宏获取包含结构体

### 1.2 UVM 中的使用

```c
// uvm_pmm_gpu.h
typedef struct {
    uvm_gpu_chunk_t chunk;          // 包含 chunk 数据
    uvm_tracker_t tracker;
} uvm_gpu_root_chunk_t;

typedef struct uvm_gpu_chunk_struct {
    struct list_head list;          // ← 嵌入的 list_head
    uvm_gpu_address_t address;
    uvm_pmm_gpu_chunk_state_t state;
    // ... 其他字段
} uvm_gpu_chunk_t;
```

**优势**:
- 无需单独的节点结构
- 一个对象可以在多个链表中 (只要有多个 `list_head` 字段)
- O(1) 插入/删除操作

---

## 2. NVIDIA UVM 自定义辅助函数

### 2.1 list_first_chunk()

**定义**: `uvm_pmm_gpu.c:1157`

```c
static uvm_gpu_chunk_t *list_first_chunk(struct list_head *list)
{
    return list_first_entry_or_null(list, uvm_gpu_chunk_t, list);
}
```

**功能**: 获取链表第一个 chunk，如果链表为空返回 `NULL`

**使用示例**:
```c
// 从 LRU 列表头部获取最久未使用的 chunk
chunk = list_first_chunk(&pmm->root_chunks.va_block_used);
if (chunk) {
    // 驱逐这个 chunk
    chunk_start_eviction(pmm, chunk);
}
```

**等价于**:
```c
if (!list_empty(&pmm->root_chunks.va_block_used)) {
    chunk = list_first_entry(&pmm->root_chunks.va_block_used,
                             uvm_gpu_chunk_t, list);
}
```

---

## 3. Linux 内核标准 List API

NVIDIA UVM 直接使用 Linux 内核的 list API。以下是完整的函数列表:

### 3.1 初始化函数

#### INIT_LIST_HEAD()

```c
#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); \
    (ptr)->prev = (ptr); \
} while (0)
```

**功能**: 初始化一个空链表 (指向自己)

**使用示例**:
```c
// uvm_pmm_gpu.c:661
INIT_LIST_HEAD(&chunk->list);

// uvm_pmm_gpu.c:3435-3437 (初始化 PMM 的链表)
INIT_LIST_HEAD(&pmm->root_chunks.va_block_used);
INIT_LIST_HEAD(&pmm->root_chunks.va_block_unused);
INIT_LIST_HEAD(&pmm->root_chunks.va_block_lazy_free);
```

---

### 3.2 插入函数

#### list_add()

```c
static inline void list_add(struct list_head *new, struct list_head *head);
```

**功能**: 在链表**头部后面**插入新节点 (新节点成为第一个元素)

**示例**:
```c
list_add(&new_chunk->list, &pmm->root_chunks.va_block_used);
// 结果: new_chunk 在链表头部
```

#### list_add_tail()

```c
static inline void list_add_tail(struct list_head *new, struct list_head *head);
```

**功能**: 在链表**尾部**插入新节点 (新节点成为最后一个元素)

**使用示例**:
```c
// uvm_pmm_gpu.c:3127
list_add_tail(&chunk->list, &gpu->pmm.root_chunks.va_block_lazy_free);
```

**使用场景**:
- FIFO 队列: `list_add_tail()` 入队, `list_first_entry()` 出队
- LRU 更新: 新访问的 chunk 移到尾部

---

### 3.3 删除函数

#### list_del()

```c
static inline void list_del(struct list_head *entry);
```

**功能**: 从链表中删除节点 (但不初始化指针)

**警告**: 删除后节点的 `next/prev` 是未定义的，不能再次 `list_del()`

#### list_del_init()

```c
static inline void list_del_init(struct list_head *entry);
```

**功能**: 从链表删除节点，并将其初始化为空链表

**使用示例**:
```c
// uvm_pmm_gpu.c:637 (chunk 被 pin 时从列表移除)
list_del_init(&root_chunk->chunk.list);

// uvm_pmm_gpu.c:650 (temp pinned chunk 从列表移除)
list_del_init(&chunk->list);
```

**优势**:
- 安全: 可以对同一节点多次调用 `list_del_init()`
- 可以通过 `list_empty()` 检查节点是否在链表中

---

### 3.4 移动函数

#### list_move()

```c
static inline void list_move(struct list_head *list, struct list_head *head);
```

**功能**: 将节点从当前链表移到**另一个链表头部后面**

#### list_move_tail()

```c
static inline void list_move_tail(struct list_head *list, struct list_head *head);
```

**功能**: 将节点从当前链表移到**另一个链表尾部**

**使用示例** (LRU 核心操作):
```c
// uvm_pmm_gpu.c:642 (chunk 被使用后移到尾部 - Most Recently Used)
list_move_tail(&root_chunk->chunk.list, &pmm->root_chunks.va_block_used);

// uvm_pmm_gpu.c:648 (chunk 释放后移到 free list)
list_move_tail(&chunk->list, find_free_list_chunk(pmm, chunk));
```

**使用场景**:
- **LRU 更新**: 访问后移到尾部
- **状态转换**: chunk 在不同状态链表间移动

---

### 3.5 查询函数

#### list_empty()

```c
static inline int list_empty(const struct list_head *head);
```

**功能**: 检查链表是否为空

**使用示例**:
```c
// uvm_pmm_gpu.c:3394
while (!list_empty(&pmm->root_chunks.va_block_lazy_free)) {
    chunk = list_first_entry(&pmm->root_chunks.va_block_lazy_free,
                             uvm_gpu_chunk_t, list);
    list_del_init(&chunk->list);
    free_chunk(pmm, chunk);
}
```

#### list_is_last()

```c
static inline int list_is_last(const struct list_head *list,
                               const struct list_head *head);
```

**功能**: 检查节点是否是链表最后一个元素

#### list_is_singular()

```c
static inline int list_is_singular(const struct list_head *head);
```

**功能**: 检查链表是否只有一个元素

---

### 3.6 访问函数

#### list_entry()

```c
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)
```

**功能**: 通过 `list_head` 指针获取包含结构体指针

**使用示例**:
```c
struct list_head *pos;
uvm_gpu_chunk_t *chunk = list_entry(pos, uvm_gpu_chunk_t, list);
```

#### list_first_entry()

```c
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)
```

**功能**: 获取链表第一个元素 (**假设链表非空**)

**使用示例**:
```c
// uvm_pmm_gpu.c:3395
chunk = list_first_entry(&pmm->root_chunks.va_block_lazy_free,
                         uvm_gpu_chunk_t, list);
```

#### list_first_entry_or_null()

```c
#define list_first_entry_or_null(ptr, type, member) \
    (!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
```

**功能**: 获取链表第一个元素，如果为空返回 `NULL`

**使用示例** (NVIDIA 封装):
```c
// uvm_pmm_gpu.c:1159
static uvm_gpu_chunk_t *list_first_chunk(struct list_head *list)
{
    return list_first_entry_or_null(list, uvm_gpu_chunk_t, list);
}
```

#### list_last_entry()

```c
#define list_last_entry(ptr, type, member) \
    list_entry((ptr)->prev, type, member)
```

**功能**: 获取链表最后一个元素

#### list_next_entry()

```c
#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, typeof(*(pos)), member)
```

**功能**: 获取下一个元素

#### list_prev_entry()

```c
#define list_prev_entry(pos, member) \
    list_entry((pos)->member.prev, typeof(*(pos)), member)
```

**功能**: 获取上一个元素

---

### 3.7 遍历宏

#### list_for_each()

```c
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)
```

**功能**: 遍历链表的所有 `list_head` 节点

**使用示例**:
```c
struct list_head *pos;
list_for_each(pos, &pmm->root_chunks.va_block_used) {
    uvm_gpu_chunk_t *chunk = list_entry(pos, uvm_gpu_chunk_t, list);
    // 处理 chunk
}
```

#### list_for_each_entry()

```c
#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_next_entry(pos, member))
```

**功能**: 遍历链表的所有元素 (自动转换为包含结构体指针)

**使用示例**:
```c
// uvm_pmm_gpu.c:3168
struct uvm_devmem *devmem;
list_for_each_entry(devmem, &g_uvm_global.devmem_ranges.list, list_node) {
    // 处理 devmem
}
```

**优势**: 不需要手动调用 `list_entry()`

#### list_for_each_entry_safe()

```c
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member), \
         n = list_next_entry(pos, member); \
         &pos->member != (head); \
         pos = n, n = list_next_entry(n, member))
```

**功能**: 安全遍历 (可以在遍历中删除当前节点)

**使用示例**:
```c
// uvm_pmm_gpu.c:1587
uvm_gpu_chunk_t *chunk, *tmp;
list_for_each_entry_safe(chunk, tmp, free_list, list) {
    if (should_free(chunk)) {
        list_del_init(&chunk->list);  // 安全删除
        free_chunk(pmm, chunk);
    }
}
```

**关键**: 使用临时变量 `tmp` 保存下一个节点

#### list_for_each_prev()

```c
#define list_for_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)
```

**功能**: 反向遍历链表

#### list_for_each_entry_reverse()

```c
#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_prev_entry(pos, member))
```

**功能**: 反向遍历 (从尾部到头部)

---

## 4. UVM 中的 List 使用模式

### 4.1 LRU 管理模式

```c
// 初始化 (uvm_pmm_gpu.c:3435)
INIT_LIST_HEAD(&pmm->root_chunks.va_block_used);

// 插入新 chunk (首次分配)
if (list_empty(&root_chunk->chunk.list)) {
    list_add_tail(&root_chunk->chunk.list, &pmm->root_chunks.va_block_used);
}

// 更新 LRU (uvm_pmm_gpu.c:642 - 移到尾部表示最近使用)
list_move_tail(&root_chunk->chunk.list, &pmm->root_chunks.va_block_used);

// 驱逐 (uvm_pmm_gpu.c:1490 - 从头部取最久未使用)
chunk = list_first_chunk(&pmm->root_chunks.va_block_used);
if (chunk)
    chunk_start_eviction(pmm, chunk);
```

**LRU 语义**:
- **HEAD** (next): 最久未使用 (Least Recently Used)
- **TAIL** (prev): 最近使用 (Most Recently Used)

### 4.2 FIFO 队列模式

```c
// 入队 (加到尾部)
list_add_tail(&chunk->list, &pmm->root_chunks.va_block_lazy_free);

// 出队 (从头部取)
if (!list_empty(&pmm->root_chunks.va_block_lazy_free)) {
    chunk = list_first_entry(&pmm->root_chunks.va_block_lazy_free,
                             uvm_gpu_chunk_t, list);
    list_del_init(&chunk->list);
    process_chunk(chunk);
}
```

### 4.3 状态转换模式

```c
// Chunk 在不同状态链表间移动
void chunk_update_state(uvm_pmm_gpu_t *pmm, uvm_gpu_chunk_t *chunk)
{
    if (chunk->state == UVM_PMM_GPU_CHUNK_STATE_FREE) {
        // 移到 free list
        list_move_tail(&chunk->list, find_free_list_chunk(pmm, chunk));
    }
    else if (chunk_is_root_chunk_pinned(pmm, chunk)) {
        // Pinned chunk 从列表移除
        list_del_init(&chunk->list);
    }
    else {
        // 移到 used list
        list_move_tail(&chunk->list, &pmm->root_chunks.va_block_used);
    }
}
```

### 4.4 安全删除模式

```c
// 遍历并删除满足条件的节点
uvm_gpu_chunk_t *chunk, *tmp;
list_for_each_entry_safe(chunk, tmp, &some_list, list) {
    if (should_remove(chunk)) {
        list_del_init(&chunk->list);  // 安全删除
        cleanup_chunk(chunk);
    }
}
```

---

## 5. 常见陷阱和最佳实践

### 5.1 ⚠️ 陷阱 1: 删除后再次删除

**错误示例**:
```c
list_del(&chunk->list);
// ... 一些代码 ...
list_del(&chunk->list);  // ❌ 崩溃! 指针已损坏
```

**正确做法**:
```c
list_del_init(&chunk->list);
// ... 一些代码 ...
list_del_init(&chunk->list);  // ✅ 安全 (no-op)
```

### 5.2 ⚠️ 陷阱 2: 遍历中删除当前节点

**错误示例**:
```c
uvm_gpu_chunk_t *chunk;
list_for_each_entry(chunk, &list, list) {
    if (should_remove(chunk)) {
        list_del(&chunk->list);  // ❌ 破坏遍历！下一次迭代会崩溃
    }
}
```

**正确做法**:
```c
uvm_gpu_chunk_t *chunk, *tmp;
list_for_each_entry_safe(chunk, tmp, &list, list) {
    if (should_remove(chunk)) {
        list_del(&chunk->list);  // ✅ 安全
    }
}
```

### 5.3 ⚠️ 陷阱 3: 未初始化就使用

**错误示例**:
```c
uvm_gpu_chunk_t *chunk = kmalloc(sizeof(*chunk), GFP_KERNEL);
list_add(&chunk->list, &some_list);  // ❌ chunk->list 未初始化
```

**正确做法**:
```c
uvm_gpu_chunk_t *chunk = kmalloc(sizeof(*chunk), GFP_KERNEL);
INIT_LIST_HEAD(&chunk->list);  // ✅ 初始化
list_add(&chunk->list, &some_list);
```

### 5.4 ✅ 最佳实践 1: 使用 list_empty() 检查

```c
// 好的模式
if (!list_empty(&chunk->list)) {
    // chunk 在某个链表中
    list_del_init(&chunk->list);
}
```

### 5.5 ✅ 最佳实践 2: 使用 _safe 变体进行删除

```c
// 总是在可能删除节点的遍历中使用 _safe
list_for_each_entry_safe(pos, tmp, &list, member) {
    // 可以安全地 list_del(pos)
}
```

### 5.6 ✅ 最佳实践 3: 使用 list_first_entry_or_null

```c
// 避免
if (!list_empty(&list)) {
    chunk = list_first_entry(&list, uvm_gpu_chunk_t, list);
}

// 推荐
chunk = list_first_entry_or_null(&list, uvm_gpu_chunk_t, list);
if (chunk) {
    // 处理 chunk
}
```

---

## 6. BPF 中需要的 Kfunc 封装

如果要在 BPF 中操作 list，需要提供以下 kfunc:

```c
/* 基础操作 */
__bpf_kfunc void bpf_list_add(struct list_head *new, struct list_head *head);
__bpf_kfunc void bpf_list_add_tail(struct list_head *new, struct list_head *head);
__bpf_kfunc void bpf_list_del_init(struct list_head *entry);
__bpf_kfunc void bpf_list_move_tail(struct list_head *list, struct list_head *head);

/* 查询操作 */
__bpf_kfunc bool bpf_list_empty(const struct list_head *head);
__bpf_kfunc bool bpf_list_is_last(const struct list_head *list,
                                  const struct list_head *head);

/* 获取 chunk (UVM 特定) */
__bpf_kfunc uvm_gpu_chunk_t *bpf_list_first_chunk(struct list_head *list);
__bpf_kfunc uvm_gpu_chunk_t *bpf_list_next_chunk(uvm_gpu_chunk_t *pos);

/* 遍历辅助 (类似 bpf_for_each_map_elem) */
__bpf_kfunc long bpf_for_each_chunk(struct list_head *head,
                                    void *callback_fn,
                                    void *callback_ctx);
```

**使用示例**:
```c
// BPF 程序中实现自定义 LRU
SEC("struct_ops/on_chunk_allocated")
int BPF_PROG(on_chunk_allocated,
             uvm_pmm_gpu_t *pmm,
             uvm_gpu_chunk_t *chunk,
             uvm_va_block_t *va_block)
{
    /* 通过 kfunc 移到尾部 */
    bpf_list_move_tail(&chunk->list, &pmm->root_chunks.va_block_used);
    return 1;
}
```

---

## 7. 快速参考表

| 操作 | 函数 | 时间复杂度 | 说明 |
|------|------|-----------|------|
| **初始化** | `INIT_LIST_HEAD(ptr)` | O(1) | 初始化空链表 |
| **头部插入** | `list_add(new, head)` | O(1) | 新节点成为第一个 |
| **尾部插入** | `list_add_tail(new, head)` | O(1) | 新节点成为最后一个 |
| **删除** | `list_del(entry)` | O(1) | 不初始化指针 |
| **删除+初始化** | `list_del_init(entry)` | O(1) | 安全删除 |
| **移动到头部** | `list_move(list, head)` | O(1) | 从当前位置移到新链表头 |
| **移动到尾部** | `list_move_tail(list, head)` | O(1) | **LRU 核心操作** |
| **判空** | `list_empty(head)` | O(1) | 检查链表是否为空 |
| **获取第一个** | `list_first_entry(head, type, member)` | O(1) | 假设非空 |
| **安全获取第一个** | `list_first_entry_or_null(...)` | O(1) | 为空返回 NULL |
| **获取最后一个** | `list_last_entry(head, type, member)` | O(1) | |
| **遍历** | `list_for_each_entry(pos, head, member)` | O(n) | 不能删除当前节点 |
| **安全遍历** | `list_for_each_entry_safe(pos, n, head, member)` | O(n) | 可删除当前节点 |

---

## 8. 实际代码示例

### 8.1 LRU 驱逐选择

```c
// uvm_pmm_gpu.c:1460-1500
static uvm_gpu_root_chunk_t *pick_root_chunk_to_evict(uvm_pmm_gpu_t *pmm)
{
    uvm_gpu_chunk_t *chunk;

    uvm_spin_lock(&pmm->list_lock);

    // 优先级 1: Free list (non-zero)
    chunk = list_first_chunk(find_free_list(pmm, ...));

    // 优先级 2: Unused list
    if (!chunk)
        chunk = list_first_chunk(&pmm->root_chunks.va_block_unused);

    // 优先级 3: LRU (从头部取最久未使用)
    if (!chunk)
        chunk = list_first_chunk(&pmm->root_chunks.va_block_used);

    if (chunk)
        chunk_start_eviction(pmm, chunk);

    uvm_spin_unlock(&pmm->list_lock);

    return chunk ? root_chunk_from_chunk(pmm, chunk) : NULL;
}
```

### 8.2 LRU 更新

```c
// uvm_pmm_gpu.c:627-651
static void chunk_update_lists_locked(uvm_pmm_gpu_t *pmm, uvm_gpu_chunk_t *chunk)
{
    uvm_gpu_root_chunk_t *root_chunk = root_chunk_from_chunk(pmm, chunk);

    if (uvm_gpu_chunk_is_user(chunk)) {
        if (chunk_is_root_chunk_pinned(pmm, chunk)) {
            // Pinned: 从列表移除
            list_del_init(&root_chunk->chunk.list);
        }
        else if (root_chunk->chunk.state != UVM_PMM_GPU_CHUNK_STATE_FREE) {
            // 移到尾部 (最近使用)
            list_move_tail(&root_chunk->chunk.list,
                          &pmm->root_chunks.va_block_used);
        }
    }

    if (chunk->state == UVM_PMM_GPU_CHUNK_STATE_FREE)
        list_move_tail(&chunk->list, find_free_list_chunk(pmm, chunk));
    else if (chunk->state == UVM_PMM_GPU_CHUNK_STATE_TEMP_PINNED)
        list_del_init(&chunk->list);
}
```

### 8.3 延迟释放队列

```c
// uvm_pmm_gpu.c:3391-3404
static void lazy_free_worker(void *args)
{
    uvm_pmm_gpu_t *pmm = (uvm_pmm_gpu_t *)args;
    uvm_gpu_chunk_t *chunk;

    uvm_spin_lock(&pmm->list_lock);

    // 处理延迟释放队列
    while (!list_empty(&pmm->root_chunks.va_block_lazy_free)) {
        chunk = list_first_entry(&pmm->root_chunks.va_block_lazy_free,
                                 uvm_gpu_chunk_t, list);
        list_del_init(&chunk->list);
        uvm_spin_unlock(&pmm->list_lock);

        free_chunk(pmm, chunk);  // 耗时操作

        uvm_spin_lock(&pmm->list_lock);
    }

    uvm_spin_unlock(&pmm->list_lock);
}
```

---

## 9. 总结

### 核心要点

1. **NVIDIA UVM 使用标准 Linux 内核 list API**
   - 无需学习特殊 API
   - 所有 Linux 内核 list 文档都适用

2. **NVIDIA 自定义辅助函数**
   - `list_first_chunk()`: 封装 `list_first_entry_or_null()`
   - 简化代码，提高可读性

3. **关键操作**
   - **LRU 更新**: `list_move_tail()` 移到尾部
   - **LRU 驱逐**: `list_first_chunk()` 从头部取
   - **安全删除**: 使用 `list_del_init()` 或 `list_for_each_entry_safe()`

4. **BPF 扩展**
   - 需要封装 kfuncs 供 BPF 使用
   - 保持类型安全 (`KF_TRUSTED_ARGS`)

---

**文档版本**: v1.0
**创建时间**: 2025-11-23
**参考资源**:
- Linux 内核文档: `Documentation/core-api/kernel-api.rst`
- NVIDIA UVM 代码: `kernel-open/nvidia-uvm/uvm_pmm_gpu.c`
- Linux 源码: `include/linux/list.h`
