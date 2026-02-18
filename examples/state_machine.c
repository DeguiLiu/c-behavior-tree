/*
 * state_machine.c - Finite state machine using behavior tree
 *
 * Demonstrates state-based decision making with SELECTOR
 */

#include <stdio.h>
#include "bt.h"

typedef enum { STATE_IDLE = 0, STATE_RUNNING = 1, STATE_ERROR = 2 } SystemState;

typedef struct {
    SystemState state;
    int counter;
} SystemContext;

static bt_status_t is_idle(bt_node_t *node)
{
    SystemContext *ctx = (SystemContext *)node->blackboard;
    return (ctx->state == STATE_IDLE) ? BT_SUCCESS : BT_FAILURE;
}

static bt_status_t idle_action(bt_node_t *node)
{
    SystemContext *ctx = (SystemContext *)node->blackboard;
    printf("[IDLE] waiting...\n");
    ctx->counter++;
    if (ctx->counter >= 3) {
        ctx->state = STATE_RUNNING;
        printf("[IDLE] transitioning to RUNNING\n");
    }
    return BT_SUCCESS;
}

static bt_status_t is_running(bt_node_t *node)
{
    SystemContext *ctx = (SystemContext *)node->blackboard;
    return (ctx->state == STATE_RUNNING) ? BT_SUCCESS : BT_FAILURE;
}

static bt_status_t running_action(bt_node_t *node)
{
    SystemContext *ctx = (SystemContext *)node->blackboard;
    printf("[RUNNING] processing...\n");
    ctx->counter++;
    if (ctx->counter >= 6) {
        ctx->state = STATE_IDLE;
        ctx->counter = 0;
        printf("[RUNNING] done, back to IDLE\n");
    }
    return BT_SUCCESS;
}

static bt_status_t is_error(bt_node_t *node)
{
    SystemContext *ctx = (SystemContext *)node->blackboard;
    return (ctx->state == STATE_ERROR) ? BT_SUCCESS : BT_FAILURE;
}

static bt_status_t error_action(bt_node_t *node)
{
    SystemContext *ctx = (SystemContext *)node->blackboard;
    printf("[ERROR] recovering...\n");
    ctx->state = STATE_IDLE;
    ctx->counter = 0;
    return BT_SUCCESS;
}

int main(void)
{
    /* Idle state sequence */
    bt_node_t idle_check, idle_act, idle_seq;
    bt_node_t *idle_children[] = {&idle_check, &idle_act};

    bt_init(&idle_check, BT_CONDITION, is_idle, NULL, 0, NULL);
    bt_init(&idle_act, BT_ACTION, idle_action, NULL, 0, NULL);
    BT_INIT(&idle_seq, BT_SEQUENCE, NULL, idle_children, NULL);

    /* Running state sequence */
    bt_node_t run_check, run_act, run_seq;
    bt_node_t *run_children[] = {&run_check, &run_act};

    bt_init(&run_check, BT_CONDITION, is_running, NULL, 0, NULL);
    bt_init(&run_act, BT_ACTION, running_action, NULL, 0, NULL);
    BT_INIT(&run_seq, BT_SEQUENCE, NULL, run_children, NULL);

    /* Error state sequence */
    bt_node_t err_check, err_act, err_seq;
    bt_node_t *err_children[] = {&err_check, &err_act};

    bt_init(&err_check, BT_CONDITION, is_error, NULL, 0, NULL);
    bt_init(&err_act, BT_ACTION, error_action, NULL, 0, NULL);
    BT_INIT(&err_seq, BT_SEQUENCE, NULL, err_children, NULL);

    /* Root selector */
    bt_node_t root;
    bt_node_t *root_children[] = {&idle_seq, &run_seq, &err_seq};
    BT_INIT(&root, BT_SELECTOR, NULL, root_children, NULL);

    /* Create context */
    SystemContext ctx = {STATE_IDLE, 0};

    /* Set blackboard for all nodes */
    idle_check.blackboard = &ctx;
    idle_act.blackboard = &ctx;
    idle_seq.blackboard = &ctx;
    run_check.blackboard = &ctx;
    run_act.blackboard = &ctx;
    run_seq.blackboard = &ctx;
    err_check.blackboard = &ctx;
    err_act.blackboard = &ctx;
    err_seq.blackboard = &ctx;
    root.blackboard = &ctx;

    /* Execute multiple ticks */
    printf("=== State Machine Example ===\n");
    for (int i = 0; i < 10; i++) {
        printf("\n[tick %d]\n", i);
        bt_tick(&root);
    }

    return 0;
}
