#define _POSIX_C_SOURCE 199309L
/* test_c-behavior-tree.c
 * RT-Thread test suite for c-behavior-tree (MISRA-style).
 *
 * Build: include this file in RT-Thread app. Exposes finsh commands:
 *   bt_test_all
 *   bt_test <case_index>
 *   bt_bt_status
 */

#include "bt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* POSIX replacements for RT-Thread primitives used by the tests */
typedef int rt_err_t;
#define RT_EOK 0
#define RT_ERROR 1

typedef uint64_t rt_tick_t;

static inline rt_tick_t rt_tick_get(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (rt_tick_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static inline void rt_thread_delay(unsigned ms) {
  struct timespec req;
  req.tv_sec = ms / 1000U;
  req.tv_nsec = (long)(ms % 1000U) * 1000000L;
  (void)nanosleep(&req, NULL);
}
static inline void rt_thread_mdelay(unsigned ms) {
  /* millisecond delay wrapper */
  struct timespec req;
  req.tv_sec = ms / 1000U;
  req.tv_nsec = (long)(ms % 1000U) * 1000000L;
  (void)nanosleep(&req, NULL);
}

#define rt_kprintf printf

/* ===== Test config ===== */
#define BT_MAX_TEST_NODES (16U)
#define BT_TEST_TICKS_SHORT (8U)
#define BT_TEST_TICKS_LONG (64U)

/* ===== Test blackboard/context ===== */
typedef struct {
  uint32_t counter;
  uint32_t progress;
  uint32_t flag;
  uint32_t last_enter_calls;
  uint32_t last_exit_calls;
} bt_test_ctx_t;

static bt_test_ctx_t g_ctx;

/* ===== Common enter/exit hooks for visibility and counting ===== */
static void hook_on_enter(bt_node_t* node) {
  (void)node;
  g_ctx.last_enter_calls++;
}

static void hook_on_exit(bt_node_t* node) {
  (void)node;
  g_ctx.last_exit_calls++;
}

/* ===== Leaf callbacks (use blackboard + user_data) ===== */

/* CONDITION: returns SUCCESS if ctx->counter > *(uint32_t*)user_data */
static bt_status_t leaf_cond_counter_gt(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if ((node != BT_NULL) && (node->blackboard != BT_NULL)) {
    const bt_test_ctx_t* ctx = (const bt_test_ctx_t*)node->blackboard;
    const uint32_t* limit = (const uint32_t*)node->user_data;
    uint32_t th = 0U;

    if (limit != BT_NULL) {
      th = *limit;
    }

    result = (ctx->counter > th) ? BT_SUCCESS : BT_FAILURE;
  } else {
    result = BT_ERROR;
  }

  return result;
}

/* CONDITION: always SUCCESS */
static bt_status_t leaf_cond_true(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if (node != BT_NULL) {
    result = BT_SUCCESS;
  } else {
    result = BT_ERROR;
  }

  return result;
}

/* CONDITION: always FAILURE */
static bt_status_t leaf_cond_false(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if (node != BT_NULL) {
    result = BT_FAILURE;
  } else {
    result = BT_ERROR;
  }

  return result;
}

/* ACTION: progresses until *(uint32_t*)user_data ticks reached; then SUCCESS */
static bt_status_t leaf_action_progress(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if ((node != BT_NULL) && (node->blackboard != BT_NULL)) {
    bt_test_ctx_t* ctx = (bt_test_ctx_t*)node->blackboard;
    const uint32_t* need_ptr = (const uint32_t*)node->user_data;
    uint32_t need = 3U;

    if (need_ptr != BT_NULL) {
      need = *need_ptr;
    }

    if (ctx->progress < need) {
      ctx->progress++;
      result = BT_RUNNING;
    } else {
      result = BT_SUCCESS;
    }
  } else {
    result = BT_ERROR;
  }

  return result;
}

/* ACTION: first call FAILURE, subsequent calls SUCCESS (per cycle controlled by ctx->flag) */
static bt_status_t leaf_action_fail_then_success(bt_node_t* node) {
  bt_status_t result = BT_ERROR;

  if ((node != BT_NULL) && (node->blackboard != BT_NULL)) {
    bt_test_ctx_t* ctx = (bt_test_ctx_t*)node->blackboard;
    if (ctx->flag == 0U) {
      ctx->flag = 1U;
      result = BT_FAILURE;
    } else {
      result = BT_SUCCESS;
    }
  } else {
    result = BT_ERROR;
  }

  return result;
}

/* ===== Helpers to (re)build small trees for each test ===== */

typedef struct {
  /* Nodes used by test */
  bt_node_t n_root;
  bt_node_t n_seq_outer;
  bt_node_t n_seq_inner;
  bt_node_t n_selector;
  bt_node_t n_cond_true;
  bt_node_t n_cond_false;
  bt_node_t n_cond_counter;
  bt_node_t n_action_progress;
  bt_node_t n_action_fail_succ;
} bt_test_tree_t;

static void bt_test_reset_ctx(void) {
  g_ctx.counter = 0U;
  g_ctx.progress = 0U;
  g_ctx.flag = 0U;
  g_ctx.last_enter_calls = 0U;
  g_ctx.last_exit_calls = 0U;
}

/* Build:
 *   root = SELECTOR(
 *             SEQUENCE(
 *               cond_counter_gt(threshold),
 *               SEQUENCE(
 *                 action_progress(N ticks),
 *                 SELECTOR(
 *                   cond_false,
 *                   action_fail_then_success
 *                 )
 *               )
 *             ),
 *             cond_true
 *          )
 */
static void bt_build_tree(bt_test_tree_t* t, uint32_t threshold, uint32_t progress_ticks) {
  /* Children arrays */
  static bt_node_t* sel_children[2];
  static bt_node_t* seq_inner_children[2];
  static bt_node_t* seq_outer_children[2];
  static bt_node_t* root_children[2];

  /* Init leaves */
  bt_init(&t->n_cond_true, BT_CONDITION, leaf_cond_true, BT_NULL, 0U, BT_NULL);
  bt_init(&t->n_cond_false, BT_CONDITION, leaf_cond_false, BT_NULL, 0U, BT_NULL);
  bt_init(&t->n_cond_counter, BT_CONDITION, leaf_cond_counter_gt, BT_NULL, 0U, &threshold);
  bt_init(&t->n_action_progress, BT_ACTION, leaf_action_progress, BT_NULL, 0U, &progress_ticks);
  bt_init(&t->n_action_fail_succ, BT_ACTION, leaf_action_fail_then_success, BT_NULL, 0U, BT_NULL);

  /* Compose selector child: (cond_false, action_fail_then_success) */
  sel_children[0] = &t->n_cond_false;
  sel_children[1] = &t->n_action_fail_succ;
  bt_init(&t->n_selector, BT_SELECTOR, BT_NULL, sel_children, 2U, BT_NULL);

  /* Compose inner sequence: (action_progress, selector) */
  seq_inner_children[0] = &t->n_action_progress;
  seq_inner_children[1] = &t->n_selector;
  bt_init(&t->n_seq_inner, BT_SEQUENCE, BT_NULL, seq_inner_children, 2U, BT_NULL);

  /* Compose outer sequence: (cond_counter_gt, seq_inner) */
  seq_outer_children[0] = &t->n_cond_counter;
  seq_outer_children[1] = &t->n_seq_inner;
  bt_init(&t->n_seq_outer, BT_SEQUENCE, BT_NULL, seq_outer_children, 2U, BT_NULL);

  /* Compose root selector: (seq_outer, cond_true) */
  root_children[0] = &t->n_seq_outer;
  root_children[1] = &t->n_cond_true;
  bt_init(&t->n_root, BT_SELECTOR, BT_NULL, root_children, 2U, BT_NULL);

  /* Hook enter/exit on inner sequence to observe lifecycle */
  t->n_seq_inner.blackboard = &g_ctx;
  t->n_seq_inner.on_enter = hook_on_enter;
  t->n_seq_inner.on_exit = hook_on_exit;

  /* Share blackboard */
  t->n_cond_true.blackboard = &g_ctx;
  t->n_cond_false.blackboard = &g_ctx;
  t->n_cond_counter.blackboard = &g_ctx;
  t->n_action_progress.blackboard = &g_ctx;
  t->n_action_fail_succ.blackboard = &g_ctx;
  t->n_seq_outer.blackboard = &g_ctx;
  t->n_selector.blackboard = &g_ctx;
  t->n_root.blackboard = &g_ctx;
}

/* ===== Individual tests ===== */

static rt_err_t test_basic_sequence(void) {
  rt_err_t rc = -RT_ERROR;
  bt_test_tree_t tree;
  uint32_t threshold = 0U;
  uint32_t need_ticks = 3U;
  uint32_t i;

  bt_test_reset_ctx();
  bt_build_tree(&tree, threshold, need_ticks);

  /* counter=0, threshold=0 -> cond_counter_gt returns FAILURE.
   * Root is SELECTOR(seq_outer, cond_true) so root should resolve to SUCCESS via cond_true.
   */
  {
    const bt_status_t s0 = bt_tick(&tree.n_root);
    if (s0 != BT_SUCCESS) {
      rt_kprintf("[E] basic_sequence: expected SUCCESS via fallback, got %u\n", (unsigned)s0);
      return rc;
    }
  }

  /* Increase counter to pass the first condition and exercise SEQUENCE running */
  g_ctx.counter = 1U;

  for (i = 0U; i < BT_TEST_TICKS_SHORT; i++) {
    const bt_status_t s = bt_tick(&tree.n_root);

    if (i < need_ticks) {
      if (s != BT_RUNNING) {
        rt_kprintf("[E] basic_sequence: expected RUNNING at i=%u, got %u\n", (unsigned)i, (unsigned)s);
        return rc;
      }
    } else {
      /* After progress completes, selector child makes outer sequence succeed
       * (cond_false fails, action_fail_then_success succeeds).
       */
      if ((s != BT_SUCCESS) && (s != BT_RUNNING)) {
        rt_kprintf("[E] basic_sequence: expected RUNNING/SUCCESS after progress, got %u\n", (unsigned)s);
        return rc;
      }
      if (s == BT_SUCCESS) {
        break;
      }
    }
  }

  rc = RT_EOK;
  return rc;
}

static rt_err_t test_selector_semantics(void) {
  rt_err_t rc = -RT_ERROR;
  bt_node_t cond_fail;
  bt_node_t cond_succ;
  bt_node_t selector;
  bt_node_t* children[2];

  bt_test_reset_ctx();

  bt_init(&cond_fail, BT_CONDITION, leaf_cond_false, BT_NULL, 0U, BT_NULL);
  bt_init(&cond_succ, BT_CONDITION, leaf_cond_true, BT_NULL, 0U, BT_NULL);
  children[0] = &cond_fail;
  children[1] = &cond_succ;
  bt_init(&selector, BT_SELECTOR, BT_NULL, children, 2U, BT_NULL);

  cond_fail.blackboard = &g_ctx;
  cond_succ.blackboard = &g_ctx;
  selector.blackboard = &g_ctx;

  {
    const bt_status_t s = bt_tick(&selector);
    if (s != BT_SUCCESS) {
      rt_kprintf("[E] selector_semantics: expected SUCCESS (second child), got %u\n", (unsigned)s);
      return rc;
    }
  }

  rc = RT_EOK;
  return rc;
}

static rt_err_t test_inverter_semantics(void) {
  rt_err_t rc = -RT_ERROR;
  bt_node_t leaf_ok;
  bt_node_t* ch1[1];
  bt_node_t inverter;

  bt_test_reset_ctx();

  bt_init(&leaf_ok, BT_CONDITION, leaf_cond_true, BT_NULL, 0U, BT_NULL);
  ch1[0] = &leaf_ok;
  bt_init(&inverter, BT_INVERTER, BT_NULL, ch1, 1U, BT_NULL);

  leaf_ok.blackboard = &g_ctx;
  inverter.blackboard = &g_ctx;

  {
    const bt_status_t s = bt_tick(&inverter);
    if (s != BT_FAILURE) {
      rt_kprintf("[E] inverter_semantics: expected FAILURE (invert SUCCESS), got %u\n", (unsigned)s);
      return rc;
    }
  }

  rc = RT_EOK;
  return rc;
}

static rt_err_t test_error_cases(void) {
  rt_err_t rc = -RT_ERROR;

  /* NULL root */
  {
    const bt_status_t s = bt_tick(BT_NULL);
    if (s != BT_ERROR) {
      rt_kprintf("[E] error_cases: expected BT_ERROR for NULL root\n");
      return rc;
    }
  }

  /* INVERTER with wrong children_count */
  {
    bt_node_t inv_bad;
    /* pass BT_NULL directly for children pointer */
    bt_init(&inv_bad, BT_INVERTER, BT_NULL, BT_NULL, 0U, BT_NULL);
    inv_bad.blackboard = &g_ctx;

    {
      const bt_status_t s2 = bt_tick(&inv_bad);
      if (s2 != BT_ERROR) {
        rt_kprintf("[E] error_cases: expected BT_ERROR for inverter without child, got %u\n", (unsigned)s2);
        return rc;
      }
    }
  }

  rc = RT_EOK;
  return rc;
}

static rt_err_t test_stress(void) {
  rt_err_t rc = -RT_ERROR;
  bt_test_tree_t tree;
  uint32_t threshold = 0U;
  uint32_t need_ticks = 2U;
  uint32_t i;
  uint32_t handled = 0U;

  bt_test_reset_ctx();
  bt_build_tree(&tree, threshold, need_ticks);

  for (i = 0U; i < BT_TEST_TICKS_LONG; i++) {
    /* Toggle counter to sometimes run the outer sequence, sometimes fallback */
    g_ctx.counter = (i & 1U);

    if (bt_tick(&tree.n_root) != BT_ERROR) {
      handled++;
    }

    if ((i % 8U) == 0U) {
      rt_thread_delay(1);
    }
  }

  if (handled < (BT_TEST_TICKS_LONG * 9U) / 10U) {
    rt_kprintf("[E] stress: handled too few ticks (%u)\n", (unsigned)handled);
    return rc;
  }

  rc = RT_EOK;
  return rc;
}

static rt_err_t test_performance(void) {
  rt_err_t rc = -RT_ERROR;
  bt_node_t act_prog;
  bt_node_t cond_ok;
  bt_node_t seq;
  bt_node_t* seq_children[2];
  uint32_t need_ticks = 1U;
  uint32_t i;
  const uint32_t cycles = 1000U;
  uint32_t successes = 0U;

  bt_test_reset_ctx();

  bt_init(&act_prog, BT_ACTION, leaf_action_progress, BT_NULL, 0U, &need_ticks);
  bt_init(&cond_ok, BT_CONDITION, leaf_cond_true, BT_NULL, 0U, BT_NULL);
  seq_children[0] = &cond_ok;
  seq_children[1] = &act_prog;
  bt_init(&seq, BT_SEQUENCE, BT_NULL, seq_children, 2U, BT_NULL);

  act_prog.blackboard = &g_ctx;
  cond_ok.blackboard = &g_ctx;
  seq.blackboard = &g_ctx;

  const rt_tick_t t0 = rt_tick_get();
  for (i = 0U; i < cycles; i++) {
    if (bt_tick(&seq) == BT_SUCCESS) {
      successes++;
      g_ctx.progress = 0U; /* reset progress for next iteration */
    }
  }
  const rt_tick_t t1 = rt_tick_get();

  rt_kprintf("[PERF] cycles=%u, successes=%u, ticks=%u\n", (unsigned)cycles, (unsigned)successes, (unsigned)(t1 - t0));

  if (successes == 0U) {
    rt_kprintf("[E] performance: no successes recorded\n");
    return rc;
  }

  rc = RT_EOK;
  return rc;
}

/* ===== Test runner & shell commands ===== */

typedef struct {
  const char* name;
  rt_err_t (*fn)(void);
  const char* desc;
} bt_case_t;

static const bt_case_t g_cases[] = {{"Basic Sequence", test_basic_sequence, "Basic SEQUENCE + SELECTOR flow"},
                                    {"Selector", test_selector_semantics, "SELECTOR semantics"},
                                    {"Inverter", test_inverter_semantics, "INVERTER semantics"},
                                    {"Error Cases", test_error_cases, "Invalid usage handling"},
                                    {"Stress", test_stress, "Repeated ticks under variation"},
                                    {"Performance", test_performance, "Throughput of simple SEQUENCE"}};

static void bt_run_one(const bt_case_t* c) {
  if (c == BT_NULL) {
    rt_kprintf("[E] Invalid case\n");
    return;
  }

  rt_tick_t t0 = rt_tick_get();
  const rt_err_t r = c->fn();
  rt_tick_t t1 = rt_tick_get();

  if (r == RT_EOK) {
    rt_kprintf("[PASS] %s (%s) in %u ticks\n", c->name, c->desc, (unsigned)(t1 - t0));
  } else {
    rt_kprintf("[FAIL] %s (%s)\n", c->name, c->desc);
  }
}

static void cmd_bt_test_all(int argc, char** argv) {
  (void)argc;
  (void)argv;

  const uint32_t n = (uint32_t)(sizeof(g_cases) / sizeof(g_cases[0]));
  uint32_t i;

  rt_kprintf("\n=== Behavior Tree Test Suite (%u cases) ===\n", (unsigned)n);

  for (i = 0U; i < n; i++) {
    bt_run_one(&g_cases[i]);
    rt_thread_mdelay(10);
  }

  rt_kprintf("=== End of BT Test Suite ===\n");
}

static void cmd_bt_status(int argc, char** argv) {
  (void)argc;
  (void)argv;

  rt_kprintf("BT Test Context:\n");
  rt_kprintf("  counter=%u, progress=%u, flag=%u\n", (unsigned)g_ctx.counter, (unsigned)g_ctx.progress,
             (unsigned)g_ctx.flag);
  rt_kprintf("  enter_calls=%u, exit_calls=%u\n", (unsigned)g_ctx.last_enter_calls, (unsigned)g_ctx.last_exit_calls);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  /* Banner */
  rt_kprintf("\n=== c-behavior-tree POSIX Test Runner ===\n");

  if (argc == 1) {
    /* No args: run all tests */
    rt_kprintf("Running all tests...\n");
    cmd_bt_test_all(0, BT_NULL);
    rt_kprintf("All tests finished.\n");
    return 0;
  }

  if (strcmp(argv[1], "list") == 0) {
    const uint32_t n = (uint32_t)(sizeof(g_cases) / sizeof(g_cases[0]));
    for (uint32_t i = 0U; i < n; i++) {
      rt_kprintf("%2u: %s -- %s\n", (unsigned)i, g_cases[i].name, g_cases[i].desc);
    }
    return 0;
  }

  if (strcmp(argv[1], "status") == 0) {
    rt_kprintf("Printing BT test context:\n");
    cmd_bt_status(0, BT_NULL);
    return 0;
  }

  /* If numeric index provided, run that case */
  char* endptr = NULL;
  long idx = strtol(argv[1], &endptr, 10);
  if ((endptr != NULL) && (*endptr == '\0') && (idx >= 0)) {
    const int max = (int)(sizeof(g_cases) / sizeof(g_cases[0]));
    if (idx < max) {
      rt_kprintf("Running test %ld: %s -- %s\n", idx, g_cases[(uint32_t)idx].name, g_cases[(uint32_t)idx].desc);
      bt_run_one(&g_cases[(uint32_t)idx]);
      return 0;
    } else {
      rt_kprintf("Invalid index. Range: 0..%d\n", max - 1);
      return 2;
    }
  }

  /* Print usage, avoid repeated argv[0] in a single call */
  {
    const char* prog = argv[0];
    rt_kprintf("Usage:\n");
    rt_kprintf("  %s            Run all tests\n", prog);
    rt_kprintf("  %s list       List tests\n", prog);
    rt_kprintf("  %s <index>    Run single test\n", prog);
    rt_kprintf("  %s status     Print test context\n", prog);
  }
  return 1;
}

/*
gcc -std=c11 -O2 -Wall test_c-behavior-tree.c c-behavior-tree.c -o bt_test
*/