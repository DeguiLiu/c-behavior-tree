# C Behavior Tree API 参考

## 类型定义

### bt_status_t

节点执行状态。

```c
typedef enum {
    BT_SUCCESS = 0U,   // 节点成功
    BT_FAILURE = 1U,   // 节点失败
    BT_RUNNING = 2U,   // 节点运行中
    BT_ERROR   = 255U  // 错误状态
} bt_status_t;
```

### bt_node_type_t

节点类型。

```c
typedef enum {
    BT_ACTION,      // 叶子节点：执行动作
    BT_CONDITION,   // 叶子节点：检查条件
    BT_SEQUENCE,    // 复合节点：顺序执行
    BT_SELECTOR,    // 复合节点：选择执行
    BT_INVERTER     // 装饰器：反转状态
} bt_node_type_t;
```

### bt_tick_fn

叶子节点的 tick 回调函数类型。

```c
typedef bt_status_t (*bt_tick_fn)(struct bt_node_s *node);
```

**参数**:
- `node`: 节点指针

**返回值**:
- `BT_SUCCESS`: 节点成功
- `BT_FAILURE`: 节点失败
- `BT_RUNNING`: 节点运行中
- `BT_ERROR`: 错误

### bt_enter_fn

节点进入钩子函数类型。

```c
typedef void (*bt_enter_fn)(struct bt_node_s *node);
```

**调用时机**: 节点首次从非运行态进入运行时

### bt_exit_fn

节点退出钩子函数类型。

```c
typedef void (*bt_exit_fn)(struct bt_node_s *node);
```

**调用时机**: 节点到达终态 (SUCCESS/FAILURE/ERROR) 时

### bt_node_t

行为树节点结构。

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

## 函数

### bt_init

初始化节点。

```c
void bt_init(bt_node_t *node,
             bt_node_type_t type,
             bt_tick_fn tick_fn,
             bt_node_t *const children[],
             uint16_t children_count,
             void *user_data);
```

**参数**:
- `node`: 节点指针（必须非 NULL）
- `type`: 节点类型
- `tick_fn`: 叶子节点回调（复合节点传 NULL）
- `children`: 子节点指针数组（无子节点传 NULL）
- `children_count`: 子节点数量
- `user_data`: 节点私有数据（可选，传 NULL）

**示例**:
```c
bt_node_t action_node;
bt_init(&action_node, BT_ACTION, my_action_fn, NULL, 0, NULL);

bt_node_t seq_node;
bt_node_t *children[] = {&action_node};
bt_init(&seq_node, BT_SEQUENCE, NULL, children, 1, NULL);
```

### bt_tick

执行一次 tick，推进树执行。

```c
bt_status_t bt_tick(bt_node_t *root);
```

**参数**:
- `root`: 根节点指针

**返回值**:
- `BT_SUCCESS`: 树执行成功
- `BT_FAILURE`: 树执行失败
- `BT_RUNNING`: 树仍在运行
- `BT_ERROR`: 错误

**示例**:
```c
bt_status_t status = bt_tick(&root);
if (status == BT_SUCCESS) {
    printf("Tree succeeded\n");
} else if (status == BT_RUNNING) {
    printf("Tree still running\n");
}
```

---

## 宏

### BT_NULL

空指针常量。

```c
#define BT_NULL (NULL)
```

### BT_COUNT_OF

计算静态数组元素数量。

```c
#define BT_COUNT_OF(arr) (uint16_t)(sizeof(arr) / sizeof((arr)[0]))
```

**示例**:
```c
bt_node_t *children[] = {&node1, &node2, &node3};
bt_init(&parent, BT_SEQUENCE, NULL, children, BT_COUNT_OF(children), NULL);
```

### BT_INIT

便利宏，自动计算子节点数量。

```c
#define BT_INIT(nodePtr, typeVal, fn, arr, data) \
    bt_init((nodePtr), (typeVal), (fn), (arr), BT_COUNT_OF(arr), (data))
```

**示例**:
```c
bt_node_t *children[] = {&node1, &node2};
BT_INIT(&parent, BT_SEQUENCE, NULL, children, NULL);
```

---

## 常见模式

### 模式 1: 简单顺序

```c
bt_node_t step1, step2, step3, seq;
bt_node_t *children[] = {&step1, &step2, &step3};

bt_init(&step1, BT_ACTION, action1, NULL, 0, NULL);
bt_init(&step2, BT_ACTION, action2, NULL, 0, NULL);
bt_init(&step3, BT_ACTION, action3, NULL, 0, NULL);
BT_INIT(&seq, BT_SEQUENCE, NULL, children, NULL);

bt_tick(&seq);  // 依次执行 action1, action2, action3
```

### 模式 2: 条件分支

```c
bt_node_t check, action1, action2, selector;
bt_node_t *children[] = {action1, action2};

bt_init(&check, BT_CONDITION, check_fn, NULL, 0, NULL);
bt_init(&action1, BT_ACTION, action1_fn, NULL, 0, NULL);
bt_init(&action2, BT_ACTION, action2_fn, NULL, 0, NULL);
BT_INIT(&selector, BT_SELECTOR, NULL, children, NULL);

bt_tick(&selector);  // 尝试 action1，失败则尝试 action2
```

### 模式 3: 反转条件

```c
bt_node_t check, inverter;
bt_node_t *children[] = {&check};

bt_init(&check, BT_CONDITION, check_fn, NULL, 0, NULL);
BT_INIT(&inverter, BT_INVERTER, NULL, children, NULL);

bt_tick(&inverter);  // 反转 check 的结果
```

---

## 线程安全性

**不线程安全**: 同一棵树不能被多个线程并发访问。

**建议**:
- 每个线程维护独立的树
- 或使用互斥锁保护树访问

---

## 性能建议

1. **避免深树**: 树深度过深会增加栈使用
2. **复用节点**: 不要频繁创建/销毁节点
3. **黑板设计**: 黑板应包含所有共享状态，避免全局变量
4. **回调优化**: 叶子节点回调应尽可能快

---
