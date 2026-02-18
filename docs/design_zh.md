# C Behavior Tree 设计文档

> 版本: 1.0
> 日期: 2026-02-18
> 状态: 已实现

---

## 目录

1. [设计目标](#1-设计目标)
2. [核心概念](#2-核心概念)
3. [架构设计](#3-架构设计)
4. [节点类型](#4-节点类型)
5. [生命周期](#5-生命周期)
6. [黑板与用户数据](#6-黑板与用户数据)
7. [性能特性](#7-性能特性)
8. [使用示例](#8-使用示例)

---

## 1. 设计目标

为嵌入式系统提供轻量级行为树实现，满足以下需求：

- **零动态分配**: 所有节点由调用者静态分配或栈分配
- **单次 tick 驱动**: 用户通过 `bt_tick(root)` 推进树执行
- **无依赖**: 纯 C11，不依赖任何外部库
- **MISRA C 合规**: 工业级代码质量
- **低开销**: 适合实时系统

---

## 2. 核心概念

### 2.1 节点状态 (bt_status_t)

```c
typedef enum {
    BT_SUCCESS = 0U,   // 节点成功
    BT_FAILURE = 1U,   // 节点失败
    BT_RUNNING = 2U,   // 节点运行中
    BT_ERROR   = 255U  // 错误状态
} bt_status_t;
```

### 2.2 节点类型 (bt_node_type_t)

```c
typedef enum {
    BT_ACTION,      // 叶子节点：执行动作
    BT_CONDITION,   // 叶子节点：检查条件
    BT_SEQUENCE,    // 复合节点：顺序执行
    BT_SELECTOR,    // 复合节点：选择执行
    BT_INVERTER     // 装饰器：反转状态
} bt_node_type_t;
```

### 2.3 节点结构 (bt_node_t)

```c
typedef struct bt_node_s {
    bt_node_type_t     type;           // 节点类型
    bt_status_t        status;         // 当前状态

    bt_tick_fn         tick;           // 叶子节点回调

    struct bt_node_s **children;       // 子节点指针数组
    uint16_t           children_count; // 子节点数量

    uint16_t           current_child;  // 复合节点进度

    bt_enter_fn        on_enter;       // 进入钩子
    bt_exit_fn         on_exit;        // 退出钩子

    uint32_t           time_anchor_ms; // 时间锚点

    void *             user_data;      // 节点私有数据
    void *             blackboard;     // 共享黑板
} bt_node_t;
```

---

## 3. 架构设计

### 3.1 执行流程

```
bt_tick(root)
    ↓
root.tick() 或 root 复合节点处理
    ↓
递归处理子节点
    ↓
返回状态 (SUCCESS/FAILURE/RUNNING/ERROR)
```

### 3.2 复合节点语义

**SEQUENCE (顺序)**:
- 从 `current_child` 开始依次执行子节点
- 若子节点返回 `RUNNING`，记录进度并返回 `RUNNING`
- 若子节点返回 `FAILURE`，立即返回 `FAILURE`
- 所有子节点返回 `SUCCESS` 时，Sequence 返回 `SUCCESS`

**SELECTOR (选择)**:
- 尝试子节点直到有子节点返回 `SUCCESS`
- 若子节点返回 `RUNNING`，记录进度并返回 `RUNNING`
- 若所有子节点返回 `FAILURE`，Selector 返回 `FAILURE`

**INVERTER (反转)**:
- 仅允许 1 个子节点
- `SUCCESS` ↔ `FAILURE` 互换
- `RUNNING` 和 `ERROR` 透传

### 3.3 生命周期钩子

- `on_enter`: 节点首次从非运行态进入运行时调用
- `on_exit`: 节点到达终态 (SUCCESS/FAILURE/ERROR) 时调用

---

## 4. 节点类型

### 4.1 叶子节点 (ACTION/CONDITION)

用户提供 `bt_tick_fn` 回调：

```c
typedef bt_status_t (*bt_tick_fn)(struct bt_node_s *node);
```

回调返回 `SUCCESS`、`FAILURE` 或 `RUNNING`。

### 4.2 复合节点 (SEQUENCE/SELECTOR)

自动处理子节点执行顺序和状态转换。

### 4.3 装饰器 (INVERTER)

修改子节点的返回状态。

---

## 5. 生命周期

### 5.1 节点初始化

```c
void bt_init(bt_node_t *node,
             bt_node_type_t type,
             bt_tick_fn tick_fn,
             bt_node_t *const children[],
             uint16_t children_count,
             void *user_data);
```

### 5.2 执行流程

```
初始化 → tick(root) → 递归处理 → 返回状态
                ↓
            on_enter (首次进入)
                ↓
            执行逻辑
                ↓
            on_exit (到达终态)
```

---

## 6. 黑板与用户数据

### 6.1 黑板 (Blackboard)

共享上下文指针，所有节点可访问：

```c
typedef struct {
    int battery_level;
    int obstacle_detected;
} Blackboard;

Blackboard bb = {100, 0};
node.blackboard = &bb;
```

### 6.2 用户数据 (user_data)

节点私有数据：

```c
typedef struct {
    int progress;
    int retry_count;
} NodeContext;

NodeContext ctx = {0, 0};
node.user_data = &ctx;
```

---

## 7. 性能特性

| 特性 | 值 |
|------|-----|
| 内存占用 | ~80 字节/节点 (bt_node_t) |
| 堆分配 | 0 字节 (完全静态) |
| 单次 tick 时间复杂度 | O(n)，n = 活跃节点数 |
| 栈深度 | O(树深度) |

---

## 8. 使用示例

### 8.1 简单机器人

```c
// 定义黑板
typedef struct {
    int battery;
    int obstacle;
} Blackboard;

// 定义节点回调
bt_status_t check_battery(bt_node_t *node) {
    Blackboard *bb = (Blackboard *)node->blackboard;
    return (bb->battery > 20) ? BT_SUCCESS : BT_FAILURE;
}

bt_status_t move_forward(bt_node_t *node) {
    // 执行移动逻辑
    return BT_SUCCESS;
}

// 构建树
bt_node_t check_node, move_node, root;
bt_node_t *seq_children[] = {&check_node, &move_node};

bt_init(&check_node, BT_CONDITION, check_battery, NULL, 0, NULL);
bt_init(&move_node, BT_ACTION, move_forward, NULL, 0, NULL);
bt_init(&root, BT_SEQUENCE, NULL, seq_children, 2, NULL);

// 执行
Blackboard bb = {100, 0};
root.blackboard = &bb;
bt_status_t status = bt_tick(&root);
```

---

## 9. 扩展方向

### 9.1 新增装饰器

- `BT_REPEATER`: 重复 N 次
- `BT_UNTIL_SUCCESS`: 直到成功
- `BT_TIMEOUT`: 超时装饰器

### 9.2 新增复合节点

- `BT_PARALLEL`: 并行执行
- `BT_RANDOM_SELECTOR`: 随机选择

### 9.3 调试工具

- 树可视化
- 执行追踪
- 性能分析

---
