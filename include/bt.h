/*
 * c-behavior-tree.h
 *
 * Public API for a simplified Behavior Tree (BT) core.
 * This header defines the BT node types, status codes, the core
 * node structure `bt_node_t`, and the public functions used to
 * initialize nodes and tick the tree. Advanced features (parallel, repeaters, timers)
 * are omitted. The `time_anchor_ms` field is provided as an
 * optional placeholder for user-defined timing/decorator logic.
 */

#ifndef C_BEHAVIOR_TREE_H
#define C_BEHAVIOR_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ===== Public constants and helpers ===== */

#ifndef BT_NULL
#define BT_NULL (NULL)
#endif

#ifndef BT_COUNT_OF
/* Count-of helper; for static arrays only */
#define BT_COUNT_OF(arr) (uint16_t)(sizeof(arr) / sizeof((arr)[0]))
#endif

/* ===== Status and type enumerations ===== */

typedef enum {
  BT_SUCCESS = 0U, /* Node succeeded */
  BT_FAILURE = 1U, /* Node failed */
  BT_RUNNING = 2U, /* Node is still running */
  BT_ERROR = 255U  /* Error state for invalid usage */
} bt_status_t;

typedef enum {
  BT_ACTION = 0U, /* Leaf node (perform an action)        */
  BT_CONDITION,   /* Leaf node (check a condition)        */
  BT_SEQUENCE,    /* Composite: run children in order     */
  BT_SELECTOR,    /* Composite: first child that succeeds */
  BT_INVERTER     /* Decorator: invert child status       */
} bt_node_type_t;

/* ===== Forward declarations ===== */

struct bt_node_s;

/* Tick/enter/exit callbacks for user-defined logic */
typedef bt_status_t (*bt_tick_fn)(struct bt_node_s* node);
typedef void (*bt_enter_fn)(struct bt_node_s* node);
typedef void (*bt_exit_fn)(struct bt_node_s* node);

/* ===== Core node structure =====
 * Notes:
 *  - No dynamic allocation is performed by the library.
 *  - Users create nodes statically or on stack and wire the tree manually.
 *  - time_anchor_ms is optional and unused by the core (reserved for user/decorators).
 */
typedef struct bt_node_s {
  bt_node_type_t type;
  bt_status_t status;

  /* Leaf behavior (ACTION/CONDITION) */
  bt_tick_fn tick;

  /* Children (for composites/decorators) */
  struct bt_node_s** children; /* Array of child pointers (may be NULL when children_count == 0) */
  uint16_t children_count;

  /* Runtime bookkeeping */
  uint16_t current_child; /* For SEQUENCE/SELECTOR progress */

  /* Optional lifecycle hooks (for any node type) */
  bt_enter_fn on_enter; /* Optional */
  bt_exit_fn on_exit;   /* Optional */

  /* Optional POSIX time anchor placeholder (unused by core) */
  uint32_t time_anchor_ms;

  /* User payloads */
  void* user_data;  /* Opaque per-node data (optional) */
  void* blackboard; /* Shared context pointer (optional) */
} bt_node_t;

/* ===== Public API ===== */

/* Initialize a node with given attributes.
 * children may be NULL when children_count == 0.
 */
void bt_init(bt_node_t* node, bt_node_type_t type, bt_tick_fn tick_fn, bt_node_t* const children[],
             uint16_t children_count, void* user_data);

/* Convenience helper to infer children_count for static arrays */
#define BT_INIT(nodePtr, typeVal, fn, arr, data) bt_init((nodePtr), (typeVal), (fn), (arr), BT_COUNT_OF(arr), (data))

/* Tick from the given node (usually the root) */
bt_status_t bt_tick(bt_node_t* root);

#endif /* C_BEHAVIOR_TREE_H */
