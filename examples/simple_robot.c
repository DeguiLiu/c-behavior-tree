/*
 * simple_robot.c - Minimal behavior tree example
 *
 * Demonstrates basic robot task: check battery, move, collect data
 */

#include <stdio.h>
#include "bt.h"

typedef struct {
    int battery;
    int position;
    int data_collected;
} RobotState;

static bt_status_t check_battery(bt_node_t *node)
{
    RobotState *state = (RobotState *)node->blackboard;
    printf("[check_battery] battery=%d%%\n", state->battery);
    return (state->battery > 20) ? BT_SUCCESS : BT_FAILURE;
}

static bt_status_t move_forward(bt_node_t *node)
{
    RobotState *state = (RobotState *)node->blackboard;
    state->position++;
    state->battery--;
    printf("[move_forward] position=%d, battery=%d%%\n", state->position,
           state->battery);
    return BT_SUCCESS;
}

static bt_status_t collect_data(bt_node_t *node)
{
    RobotState *state = (RobotState *)node->blackboard;
    state->data_collected++;
    state->battery -= 2;
    printf("[collect_data] collected=%d, battery=%d%%\n", state->data_collected,
           state->battery);
    return BT_SUCCESS;
}

int main(void)
{
    bt_node_t check_node, move_node, collect_node, root_node;
    bt_node_t *children[] = {&check_node, &move_node, &collect_node};

    /* Initialize nodes */
    bt_init(&check_node, BT_CONDITION, check_battery, NULL, 0, NULL);
    bt_init(&move_node, BT_ACTION, move_forward, NULL, 0, NULL);
    bt_init(&collect_node, BT_ACTION, collect_data, NULL, 0, NULL);
    BT_INIT(&root_node, BT_SEQUENCE, NULL, children, NULL);

    /* Create robot state */
    RobotState state = {100, 0, 0};

    /* Set blackboard for all nodes */
    check_node.blackboard = &state;
    move_node.blackboard = &state;
    collect_node.blackboard = &state;
    root_node.blackboard = &state;

    /* Execute tree */
    printf("=== Simple Robot Example ===\n");
    bt_status_t status = bt_tick(&root_node);

    printf("\n=== Result ===\n");
    printf("Status: %d (0=SUCCESS, 1=FAILURE, 2=RUNNING)\n", status);
    printf("Final state: position=%d, battery=%d%%, data=%d\n", state.position,
           state.battery, state.data_collected);

    return (status == BT_SUCCESS) ? 0 : 1;
}
