# C Behavior Tree 示例说明

## 示例 1: bt_example_posix.c - 机器人任务管理

### 场景

模拟一个移动机器人的任务管理系统：
- 检查电池电量
- 收集数据
- 避开障碍物
- 上传数据
- 电池不足时充电

### 树结构

```
Root (SELECTOR)
├── Work (SEQUENCE)
│   ├── check_battery (CONDITION)
│   └── work_inner (SEQUENCE)
│       ├── collect_pipeline (ACTION)
│       ├── avoid (SELECTOR)
│       │   ├── handle_obstacle (ACTION)
│       │   └── pass_through (ACTION)
│       └── upload_once (ACTION)
└── Recharge (ACTION)
```

### 执行流程

1. **首次 tick**: 电池充足 → 执行 Work 序列
2. **Work 序列**:
   - 检查电池 → SUCCESS
   - 执行 collect_pipeline → RUNNING (多次 tick 完成)
   - 避开障碍物 → SUCCESS
   - 上传数据 → 可能失败
3. **电池不足**: 切换到 Recharge 动作

### 关键特性

- **黑板**: 共享电池、障碍物等状态
- **生命周期钩子**: on_enter 重置进度，on_exit 清理资源
- **时间锚点**: 用于延迟执行
- **RUNNING 状态**: 支持多 tick 完成的长操作

### 运行

```bash
cmake -B build
cmake --build build
./build/bt_example_posix
```

### 输出示例

```
>> work sequence enter: reset collect_progress
[collect] progress=1/3, battery=35%
[main] tick=0 => root status=2, battery=34%
[collect] progress=2/3, battery=34%
[collect] progress=3/3, battery=33%
[collect] done.
[avoid] obstacle detected -> avoiding...
[upload] attempt #1 -> FAILURE
[recharge] charging...
[main] tick=3 => root status=0, battery=100%
```

---

## 示例 2: 简单机器人 (simple_robot.c)

### 场景

最小化的机器人示例，演示基本的行为树用法。

### 树结构

```
Root (SEQUENCE)
├── check_battery (CONDITION)
├── move_forward (ACTION)
└── collect_data (ACTION)
```

### 代码框架

```c
#include "bt.h"

typedef struct {
    int battery;
    int position;
} RobotState;

bt_status_t check_battery(bt_node_t *node) {
    RobotState *state = (RobotState *)node->blackboard;
    return (state->battery > 20) ? BT_SUCCESS : BT_FAILURE;
}

bt_status_t move_forward(bt_node_t *node) {
    RobotState *state = (RobotState *)node->blackboard;
    state->position++;
    return BT_SUCCESS;
}

int main(void) {
    bt_node_t check, move, collect, root;
    bt_node_t *children[] = {&check, &move, &collect};

    bt_init(&check, BT_CONDITION, check_battery, NULL, 0, NULL);
    bt_init(&move, BT_ACTION, move_forward, NULL, 0, NULL);
    bt_init(&collect, BT_ACTION, collect_data, NULL, 0, NULL);
    BT_INIT(&root, BT_SEQUENCE, NULL, children, NULL);

    RobotState state = {100, 0};
    root.blackboard = &state;

    bt_status_t status = bt_tick(&root);
    return (status == BT_SUCCESS) ? 0 : 1;
}
```

---

## 示例 3: 状态机 (state_machine.c)

### 场景

使用行为树实现有限状态机。

### 树结构

```
Root (SELECTOR)
├── Idle (SEQUENCE)
│   ├── is_idle (CONDITION)
│   └── idle_action (ACTION)
├── Running (SEQUENCE)
│   ├── is_running (CONDITION)
│   └── running_action (ACTION)
└── Error (SEQUENCE)
    ├── is_error (CONDITION)
    └── error_action (ACTION)
```

### 特点

- 每个状态是一个 SEQUENCE
- 条件检查当前状态
- 动作执行状态逻辑
- SELECTOR 自动选择活跃状态

---

## 示例 4: 游戏 AI (game_ai.c)

### 场景

游戏 NPC 的 AI 行为树。

### 树结构

```
Root (SELECTOR)
├── Combat (SEQUENCE)
│   ├── is_enemy_near (CONDITION)
│   ├── aim (ACTION)
│   └── shoot (ACTION)
├── Patrol (SEQUENCE)
│   ├── is_idle (CONDITION)
│   └── patrol (ACTION)
└── Idle (ACTION)
```

### 特点

- 优先级决策（SELECTOR）
- 多步骤动作（SEQUENCE）
- 条件检查（CONDITION）
- 实时响应（每 tick 重新评估）

---

## 构建所有示例

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 运行示例
./build/bt_example_posix
./build/simple_robot
./build/state_machine
./build/game_ai
```

---

## 测试

```bash
# 运行所有测试
./build/bt_test

# 列出测试用例
./build/bt_test list

# 运行特定测试
./build/bt_test 1

# 打印测试状态
./build/bt_test status
```

---

## 最佳实践

### 1. 黑板设计

```c
typedef struct {
    // 传感器数据
    int battery;
    int distance;

    // 状态
    int mode;
    int error_code;

    // 计数器
    int retry_count;
} Blackboard;
```

### 2. 节点命名

```c
bt_node_t check_battery_node;
bt_node_t move_forward_node;
bt_node_t work_sequence_node;
```

### 3. 回调实现

```c
bt_status_t my_action(bt_node_t *node) {
    Blackboard *bb = (Blackboard *)node->blackboard;

    // 检查前置条件
    if (bb->battery < 10) {
        return BT_FAILURE;
    }

    // 执行动作
    // ...

    // 返回状态
    return BT_SUCCESS;
}
```

### 4. 生命周期钩子

```c
void on_enter_work(bt_node_t *node) {
    // 初始化工作状态
    NodeContext *ctx = (NodeContext *)node->user_data;
    ctx->progress = 0;
}

void on_exit_work(bt_node_t *node) {
    // 清理工作状态
    NodeContext *ctx = (NodeContext *)node->user_data;
    ctx->progress = 0;
}
```

---
