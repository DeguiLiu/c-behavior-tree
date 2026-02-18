/*
 * c-behavior-tree.c
 *
 * Implementation of the simplified Behavior Tree (BT) core.
 * Provides the tick dispatcher and node traversal logic for
 * ACTION, CONDITION, SEQUENCE, SELECTOR and INVERTER node types.
 * The implementation is small, portable and avoids dynamic memory
 * allocation; users create nodes and wire the tree manually.
 */

#include "bt.h"

/* ===== Internal constants ===== */
#define UINT16_ZERO ((uint16_t)0)
#define UINT16_ONE ((uint16_t)1)

/* ===== Internal helpers ===== */

/* Call the node's on_enter hook if it exists.
 * Parameters:
 *   - node: pointer to the node (may be NULL). If NULL or no hook set, nothing happens.
 */
static void bt_call_enter(bt_node_t* node) {
  if ((node != BT_NULL) && (node->on_enter != BT_NULL)) {
    node->on_enter(node);
  } else {
    /* No action */
  }
}

/* Call the node's on_exit hook if it exists.
 * Parameters:
 *   - node: pointer to the node (may be NULL). If NULL or no hook set, nothing happens.
 */
static void bt_call_exit(bt_node_t* node) {
  if ((node != BT_NULL) && (node->on_exit != BT_NULL)) {
    node->on_exit(node);
  } else {
    /* No action */
  }
}

/* Forward declaration for dispatcher */
static bt_status_t bt_tick_internal(bt_node_t* node);

/* Defensive child fetch (returns NULL if out-of-range or array is NULL)
 * Parameters:
 *   - node: parent node pointer
 *   - index: child index to fetch
 * Returns:
 *   - pointer to child node or NULL if invalid
 */
static bt_node_t* bt_child_at(const bt_node_t* node, uint16_t index) {
  bt_node_t* child = BT_NULL;

  if ((node != BT_NULL) && (node->children != BT_NULL) && (index < node->children_count)) {
    child = node->children[index];
  } else {
    /* Keep child as NULL */
  }

  return child;
}

/* ===== Public API ===== */

/* Initialize a node with given attributes.
 * Parameters:
 *   - node: pointer to node to initialize (must not be NULL)
 *   - type: node type (ACTION/CONDITION/SEQUENCE/...)
 *   - tick_fn: leaf tick callback (for ACTION/CONDITION) or NULL
 *   - children: array of child pointers (may be NULL if children_count == 0)
 *   - children_count: number of children in the array
 *   - user_data: opaque pointer stored in node->user_data
 * Behavior:
 *   Sets sensible defaults for hooks, bookkeeping and time anchor.
 */
void bt_init(bt_node_t* node, bt_node_type_t type, bt_tick_fn tick_fn, bt_node_t* const children[],
             uint16_t children_count, void* user_data) {
  if (node != BT_NULL) {
    node->type = type;
    node->status = BT_FAILURE; /* Default until first tick */
    node->tick = tick_fn;
    node->on_enter = BT_NULL;
    node->on_exit = BT_NULL;
    node->children = (children_count > UINT16_ZERO) ? (bt_node_t**)children : BT_NULL;
    node->children_count = children_count;
    node->current_child = UINT16_ZERO;
    node->time_anchor_ms = 0U; /* Optional, unused by core */
    node->user_data = user_data;
    node->blackboard = BT_NULL;
  } else {
    /* No action */
  }
}

/* Tick a leaf node (ACTION or CONDITION).
 * Parameters:
 *   - node: leaf node pointer
 * Returns:
 *   - bt_status_t returned by the node's tick callback, or BT_ERROR on invalid usage
 * Notes:
 *   - Updates node->status with the resulting status.
 */
static bt_status_t bt_tick_leaf(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if (node == BT_NULL) {
    result = BT_ERROR;
  } else {
    if (node->tick == BT_NULL) {
      result = BT_ERROR;
      node->status = BT_ERROR;
    } else {
      /* User code decides status */
      result = node->tick(node);
      node->status = result;
    }
  }

  return result;
}

/* Tick a SEQUENCE composite node.
 * Behavior:
 *   - Runs children in order starting from node->current_child.
 *   - If a child returns RUNNING, the sequence stays RUNNING and remembers the child.
 *   - If a child fails, the sequence returns FAILURE.
 *   - If all children succeed, the sequence returns SUCCESS.
 * Notes:
 *   - on_enter is called when sequence transitions from non-RUNNING to running.
 *   - on_exit is called when the sequence reaches a terminal state (SUCCESS/FAILURE/ERROR).
 */
static bt_status_t bt_tick_sequence(bt_node_t* node) {
  bt_status_t result = BT_ERROR;
  uint16_t i = UINT16_ZERO;

  if (node == BT_NULL) {
    result = BT_ERROR;
  } else {
    if (node->status != BT_RUNNING) {
      node->current_child = UINT16_ZERO;
      bt_call_enter(node);
    }

    result = BT_SUCCESS; /* Assume success unless a child says otherwise */

    for (i = node->current_child; i < node->children_count; i++) {
      bt_node_t* child = bt_child_at(node, i);
      bt_status_t cs = BT_ERROR;

      if (child == BT_NULL) {
        result = BT_ERROR;
        node->status = BT_ERROR;
        break;
      } else {
        cs = bt_tick_internal(child);
      }

      if (cs == BT_RUNNING) {
        node->current_child = i; /* Stay on this child */
        node->status = BT_RUNNING;
        result = BT_RUNNING;
        break;
      } else if (cs == BT_FAILURE) {
        node->current_child = i;
        node->status = BT_FAILURE;
        result = BT_FAILURE;
        break;
      } else if (cs == BT_ERROR) {
        node->current_child = i;
        node->status = BT_ERROR;
        result = BT_ERROR;
        break;
      } else {
        /* Child succeeded; continue with next */
        node->current_child = (uint16_t)(i + UINT16_ONE);
        /* Continue loop */
      }
    }

    /* If all children consumed without RUNNING/FAILURE/ERROR, sequence succeeded */
    if ((result != BT_RUNNING) && (node->current_child >= node->children_count)) {
      node->status = BT_SUCCESS;
      result = BT_SUCCESS;
    }

    /* Leave hook on terminal states */
    if ((result == BT_SUCCESS) || (result == BT_FAILURE) || (result == BT_ERROR)) {
      bt_call_exit(node);
    } else {
      /* Still running */
    }
  }

  return result;
}

/* Tick a SELECTOR composite node.
 * Behavior:
 *   - Runs children in order starting from node->current_child.
 *   - If a child returns RUNNING, the selector stays RUNNING and remembers the child.
 *   - If a child succeeds, the selector returns SUCCESS.
 *   - If all children fail, the selector returns FAILURE.
 * Notes:
 *   - on_enter is called when selector transitions from non-RUNNING to running.
 *   - on_exit is called when the selector reaches a terminal state (SUCCESS/FAILURE/ERROR).
 */
static bt_status_t bt_tick_selector(bt_node_t* node) {
  bt_status_t result = BT_ERROR;
  uint16_t i = UINT16_ZERO;

  if (node == BT_NULL) {
    result = BT_ERROR;
  } else {
    if (node->status != BT_RUNNING) {
      node->current_child = UINT16_ZERO;
      bt_call_enter(node);
    }

    result = BT_FAILURE; /* Assume failure until a child succeeds or runs */

    for (i = node->current_child; i < node->children_count; i++) {
      bt_node_t* child = bt_child_at(node, i);
      bt_status_t cs = BT_ERROR;

      if (child == BT_NULL) {
        result = BT_ERROR;
        node->status = BT_ERROR;
        break;
      } else {
        cs = bt_tick_internal(child);
      }

      if (cs == BT_RUNNING) {
        node->current_child = i;
        node->status = BT_RUNNING;
        result = BT_RUNNING;
        break;
      } else if (cs == BT_SUCCESS) {
        node->current_child = i;
        node->status = BT_SUCCESS;
        result = BT_SUCCESS;
        break;
      } else if (cs == BT_ERROR) {
        node->current_child = i;
        node->status = BT_ERROR;
        result = BT_ERROR;
        break;
      } else {
        /* Child failed; try next */
        node->current_child = (uint16_t)(i + UINT16_ONE);
        /* Continue loop */
      }
    }

    /* If we ran out of children and none succeeded or ran, selector fails */
    if ((result != BT_RUNNING) && (node->current_child >= node->children_count)) {
      node->status = BT_FAILURE;
      result = BT_FAILURE;
    }

    if ((result == BT_SUCCESS) || (result == BT_FAILURE) || (result == BT_ERROR)) {
      bt_call_exit(node);
    } else {
      /* Still running */
    }
  }

  return result;
}

/* Tick an INVERTER decorator node.
 * Behavior:
 *   - Inverts SUCCESS <-> FAILURE of its single child.
 *   - RUNNING and ERROR propagate unchanged.
 * Requirements:
 *   - node->children_count must be exactly 1.
 */
static bt_status_t bt_tick_inverter(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if ((node == BT_NULL) || (node->children_count != UINT16_ONE)) {
    result = BT_ERROR;
  } else {
    bt_node_t* child = bt_child_at(node, UINT16_ZERO);
    bt_status_t cs = BT_ERROR;

    if (node->status != BT_RUNNING) {
      bt_call_enter(node);
    }

    if (child == BT_NULL) {
      result = BT_ERROR;
    } else {
      cs = bt_tick_internal(child);

      if (cs == BT_SUCCESS) {
        result = BT_FAILURE;
      } else if (cs == BT_FAILURE) {
        result = BT_SUCCESS;
      } else {
        /* RUNNING and ERROR propagate */
        result = cs;
      }

      node->status = result;

      if ((result == BT_SUCCESS) || (result == BT_FAILURE) || (result == BT_ERROR)) {
        bt_call_exit(node);
      } else {
        /* Still running */
      }
    }
  }

  return result;
}

/* Internal dispatcher: call appropriate tick based on node->type. */
static bt_status_t bt_tick_internal(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if (node == BT_NULL) {
    result = BT_ERROR;
  } else {
    switch (node->type) {
      case BT_ACTION:
      case BT_CONDITION: {
        result = bt_tick_leaf(node);
        break;
      }

      case BT_SEQUENCE: {
        result = bt_tick_sequence(node);
        break;
      }

      case BT_SELECTOR: {
        result = bt_tick_selector(node);
        break;
      }

      case BT_INVERTER: {
        result = bt_tick_inverter(node);
        break;
      }

      default: {
        result = BT_ERROR;
        node->status = BT_ERROR;
        break;
      }
    }
  }

  return result;
}

/* Public API: tick from the given root node. Returns node status after tick. */
bt_status_t bt_tick(bt_node_t* root) {
  bt_status_t result = BT_ERROR;

  if (root != BT_NULL) {
    result = bt_tick_internal(root);
  } else {
    result = BT_ERROR;
  }

  return result;
}
