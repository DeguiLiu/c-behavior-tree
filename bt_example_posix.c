/* bt_example_posix.c
 * POSIX demo for the simplified Behavior Tree (no parallel/repeater/timer).
 * Demonstrates: SEQUENCE / SELECTOR / INVERTER, with blackboard and user_data.
 *
 * Scenario:
 *   Root = SELECTOR(
 *             SEQUENCE(
 *               check_battery,                 // must be > 30%
 *               SEQUENCE(
 *                 collect_pipeline,            // needs multiple ticks to complete
 *                 SELECTOR(                    // handle obstacle or pass-through
 *                   handle_obstacle,
 *                   pass_through
 *                 ),
 *                 upload_once                  // attempt once; if fails, sequence fails
 *               )
 *             ),
 *             recharge                          // fallback if work fails
 *          )
 *
 * Expected behavior:
 *  - When battery is sufficient, the device works: collect progresses (RUNNING),
 *    obstacle is handled or passed, upload attempts once (fails in this demo),
 *    causing the outer SELECTOR to switch to recharge.
 *  - After recharge, battery is full and the loop repeats.
 */

#include "c-behavior-tree.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

/* Helper: get current time in milliseconds (wrap to uint32_t) */
static uint32_t bt_get_time_ms(void)
{
    struct timeval tv;
    (void)gettimeofday(&tv, BT_NULL);
    return (uint32_t)(tv.tv_sec * 1000UL + (uint32_t)(tv.tv_usec / 1000UL));
}

/* Forward declaration: time-anchor checker used by leaf callbacks */
static bool bt_time_anchor_ready(bt_node_t * node);

/* ===== Blackboard / context ===== */
typedef struct
{
    uint32_t battery;           /* [0..100] percent */
    uint32_t collect_progress;  /* 0..N, when reaches threshold => success */
    uint32_t obstacle_flag;     /* 0 = none, 1 = present */
    uint32_t upload_attempt;    /* count attempts; we demo failure at first try */
} app_ctx_t;

/* ===== Leaf callbacks ===== */

/* Condition: battery must be > threshold stored in user_data (uint32_t*) */
static bt_status_t cb_check_battery(bt_node_t * node)
{
    bt_status_t res = BT_ERROR;

    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        if (!bt_time_anchor_ready(node))
        {
            return BT_RUNNING;
        }
         const app_ctx_t * ctx = (const app_ctx_t *)node->blackboard;
         const uint32_t * threshold = (const uint32_t *)node->user_data;
         uint32_t th = 30U;

        if (threshold != BT_NULL)
        {
            th = *threshold;
        }

        if (ctx->battery > th)
        {
            res = BT_SUCCESS;
        }
        else
        {
            res = BT_FAILURE;
        }
    }
    else
    {
        res = BT_ERROR;
    }

    return res;
}

/* Action: collect data (takes N ticks to complete; N stored in user_data as uint32_t*) */
static bt_status_t cb_collect(bt_node_t * node)
{
    bt_status_t res = BT_ERROR;

    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        if (!bt_time_anchor_ready(node))
        {
            return BT_RUNNING;
        }
         app_ctx_t * ctx = (app_ctx_t *)node->blackboard;
         const uint32_t * need_ptr = (const uint32_t *)node->user_data;
         uint32_t need = 3U;

        if (need_ptr != BT_NULL)
        {
            need = *need_ptr;
        }

        if (ctx->collect_progress < need)
        {
            ctx->collect_progress++;
            (void)printf("[collect] progress=%u/%u, battery=%u%%\n",
                         (unsigned)ctx->collect_progress, (unsigned)need, (unsigned)ctx->battery);

            if (ctx->battery > 0U)
            {
                ctx->battery--;
            }
            res = BT_RUNNING;
        }
        else
        {
            (void)printf("[collect] done.\n");
            res = BT_SUCCESS;
        }
    }
    else
    {
        res = BT_ERROR;
    }

    return res;
}

/* Action: handle obstacle if present; succeed only if obstacle_flag==1, then clear it */
static bt_status_t cb_handle_obstacle(bt_node_t * node)
{
    bt_status_t res = BT_ERROR;

    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        if (!bt_time_anchor_ready(node))
        {
            return BT_RUNNING;
        }
         app_ctx_t * ctx = (app_ctx_t *)node->blackboard;

        if (ctx->obstacle_flag != 0U)
        {
            (void)printf("[avoid] obstacle detected -> avoiding...\n");
            ctx->obstacle_flag = 0U;
            res = BT_SUCCESS;
        }
        else
        {
            res = BT_FAILURE; /* Nothing to avoid -> let SELECTOR try the other branch */
        }
    }
    else
    {
        res = BT_ERROR;
    }

    return res;
}

/* Action: pass-through (used as the 2nd branch of obstacle SELECTOR) */
static bt_status_t cb_pass_through(bt_node_t * node)
{
    bt_status_t res = BT_ERROR;

    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        if (!bt_time_anchor_ready(node))
        {
            return BT_RUNNING;
        }
         (void)node;
         (void)printf("[avoid] nothing to do, pass-through.\n");
         res = BT_SUCCESS;
     }
     else
     {
         res = BT_ERROR;
     }

     return res;
}

/* Action: try upload once (always fail on first attempt each work cycle, then succeed next time if reached) */
static bt_status_t cb_upload_once(bt_node_t * node)
{
    bt_status_t res = BT_ERROR;

    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        if (!bt_time_anchor_ready(node))
        {
            return BT_RUNNING;
        }
         app_ctx_t * ctx = (app_ctx_t *)node->blackboard;
         ctx->upload_attempt++;

        if (ctx->upload_attempt < 2U)
        {
            (void)printf("[upload] attempt #%u -> FAILURE\n", (unsigned)ctx->upload_attempt);
            res = BT_FAILURE;
        }
        else
        {
            (void)printf("[upload] attempt #%u -> SUCCESS\n", (unsigned)ctx->upload_attempt);
            res = BT_SUCCESS;
        }
    }
    else
    {
        res = BT_ERROR;
    }

    return res;
}

/* Action: recharge (always succeeds and fill battery) */
static bt_status_t cb_recharge(bt_node_t * node)
{
    bt_status_t res = BT_ERROR;

    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        if (!bt_time_anchor_ready(node))
        {
            return BT_RUNNING;
        }
         app_ctx_t * ctx = (app_ctx_t *)node->blackboard;
         (void)printf("[recharge] charging...\n");
         ctx->battery = 100U;
         ctx->collect_progress = 0U;
         /* NOTE: keep upload_attempt as-is so upload attempts persist across cycles */
         res = BT_SUCCESS;
     }
     else
     {
         res = BT_ERROR;
     }

     return res;
}

/* ===== Optional lifecycle hooks (for demo logging) ===== */
static const char * bt_node_type_to_str(uint32_t type)
{
    switch (type)
    {
        case BT_SEQUENCE:  return "SEQUENCE";
        case BT_SELECTOR:  return "SELECTOR";
        case BT_ACTION:    return "ACTION";
        case BT_CONDITION: return "CONDITION";
        case BT_INVERTER:  return "INVERTER";
        default:           return "UNKNOWN";
    }
}

/* Check time anchor: if time_anchor_ms==0 => ready; otherwise wait until now >= anchor.
 * When anchor reached, clear it (set to 0) so logic runs only once.
 */
static bool bt_time_anchor_ready(bt_node_t * node)
{
    if ((node == BT_NULL) || (node->time_anchor_ms == 0U))
    {
        return true;
    }

    uint32_t now = bt_get_time_ms();
    if (now >= node->time_anchor_ms)
    {
        node->time_anchor_ms = 0U; /* consume anchor */
        return true;
    }

    (void)printf("[time] node type=%s waiting until %u ms (now %u)\n",
                 bt_node_type_to_str((uint32_t)node->type), (unsigned)node->time_anchor_ms, (unsigned)now);
    return false;
}

static void on_enter_log(bt_node_t * node)
{
    if (node != BT_NULL)
    {
        (void)printf(">> enter node type=%s\n", bt_node_type_to_str((uint32_t)node->type));
    }
}

static void on_exit_log(bt_node_t * node)
{
    if (node != BT_NULL)
    {
        (void)printf("<< exit  node type=%s with status=%u\n", bt_node_type_to_str((uint32_t)node->type), (unsigned)node->status);
    }
}

/* on_enter for the outer work sequence: reset collect progress at start of work */
static void on_enter_work(bt_node_t * node)
{
    if ((node != BT_NULL) && (node->blackboard != BT_NULL))
    {
        app_ctx_t * ctx = (app_ctx_t *)node->blackboard;
        ctx->collect_progress = 0U;
        (void)printf(">> work sequence enter: reset collect_progress\n");
    }
}

/* ===== Build the tree and run ===== */
int main(void)
{
    /* Blackboard initialization */
    app_ctx_t ctx;
    uint32_t battery_threshold = 30U;
    uint32_t collect_ticks_need = 3U;

    ctx.battery = 35U;          /* start slightly above threshold so we see work first */
    ctx.collect_progress = 0U;
    ctx.obstacle_flag = 1U;     /* there is an obstacle initially */
    ctx.upload_attempt = 0U;

    /* Declare nodes */
    bt_node_t nd_check_batt;
    bt_node_t nd_collect;
    bt_node_t nd_handle_obst;
    bt_node_t nd_pass_through;
    bt_node_t nd_upload_once;
    bt_node_t nd_recharge;

    bt_node_t nd_obstacle_selector;
    bt_node_t nd_work_sequence_inner;
    bt_node_t nd_work_sequence_outer;
    bt_node_t nd_root_selector;

    /* Wire children arrays (arrays of pointers to nodes) */
    bt_node_t * obstacle_children[] = { &nd_handle_obst, &nd_pass_through };
    bt_node_t * work_inner_children[] = { &nd_collect, &nd_obstacle_selector, &nd_upload_once };
    bt_node_t * work_outer_children[] = { &nd_check_batt, &nd_work_sequence_inner };
    bt_node_t * root_sel_children[]   = { &nd_work_sequence_outer, &nd_recharge };

    /* Initialize leaves */
    bt_init(&nd_check_batt,    BT_CONDITION, cb_check_battery, BT_NULL, 0U, &battery_threshold);
    bt_init(&nd_collect,       BT_ACTION,    cb_collect,       BT_NULL, 0U, &collect_ticks_need);
    bt_init(&nd_handle_obst,   BT_ACTION,    cb_handle_obstacle,BT_NULL,0U, BT_NULL);
    bt_init(&nd_pass_through,  BT_ACTION,    cb_pass_through,  BT_NULL, 0U, BT_NULL);
    bt_init(&nd_upload_once,   BT_ACTION,    cb_upload_once,   BT_NULL, 0U, BT_NULL);
    bt_init(&nd_recharge,      BT_ACTION,    cb_recharge,      BT_NULL, 0U, BT_NULL);

    /* Initialize composites/decorators */
    bt_init(&nd_obstacle_selector,   BT_SELECTOR, BT_NULL, obstacle_children,   BT_COUNT_OF(obstacle_children), BT_NULL);
    bt_init(&nd_work_sequence_inner, BT_SEQUENCE, BT_NULL, work_inner_children, BT_COUNT_OF(work_inner_children),BT_NULL);
    bt_init(&nd_work_sequence_outer, BT_SEQUENCE, BT_NULL, work_outer_children, BT_COUNT_OF(work_outer_children),BT_NULL);
    bt_init(&nd_root_selector,       BT_SELECTOR, BT_NULL, root_sel_children,   BT_COUNT_OF(root_sel_children),  BT_NULL);

    /* Optional: add enter/exit hooks for visibility */
    nd_work_sequence_inner.on_enter = on_enter_log;
    nd_work_sequence_inner.on_exit  = on_exit_log;
    nd_work_sequence_outer.on_enter = on_enter_work;

    /* Share the blackboard across relevant nodes */
    nd_check_batt.blackboard    = &ctx;
    nd_collect.blackboard       = &ctx;
    nd_handle_obst.blackboard   = &ctx;
    nd_pass_through.blackboard  = &ctx;
    nd_upload_once.blackboard   = &ctx;
    nd_recharge.blackboard      = &ctx;
    /* give the outer work sequence access so its on_enter hook can reset progress */
    nd_work_sequence_outer.blackboard = &ctx;

    /* Drive the tree for several iterations */
    for (uint32_t i = 0U; i < 20U; i++)
    {
        const bt_status_t s = bt_tick(&nd_root_selector);
        (void)printf("[main] tick=%u => root status=%u, battery=%u%%\n",
                     (unsigned)i, (unsigned)s, (unsigned)ctx.battery);

        (void)usleep(500000); /* 500 ms */

        /* Drain battery faster after a few ticks to trigger recharge path */
        if (i == 8U)
        {
            ctx.battery = 10U; /* Force low battery */
        }
    }

    return 0;
}

/*
$ gcc -Wall -Wextra -O2 bt_example_posix.c c-behavior-tree.c -o bt_demo 
$ ./bt_demo
>> work sequence enter: reset collect_progress
>> enter node type=SEQUENCE
[collect] progress=1/3, battery=35%
[main] tick=0 => root status=2, battery=34%
[collect] progress=2/3, battery=34%
[main] tick=1 => root status=2, battery=33%
[collect] progress=3/3, battery=33%
[main] tick=2 => root status=2, battery=32%
[collect] done.
[avoid] obstacle detected -> avoiding...
[upload] attempt #1 -> FAILURE
<< exit  node type=SEQUENCE with status=1
[recharge] charging...
[main] tick=3 => root status=0, battery=100%
>> work sequence enter: reset collect_progress
>> enter node type=SEQUENCE
[collect] progress=1/3, battery=100%
[main] tick=4 => root status=2, battery=99%
[collect] progress=2/3, battery=99%
[main] tick=5 => root status=2, battery=98%
[collect] progress=3/3, battery=98%
[main] tick=6 => root status=2, battery=97%
[collect] done.
[avoid] nothing to do, pass-through.
[upload] attempt #2 -> SUCCESS
<< exit  node type=SEQUENCE with status=0
[main] tick=7 => root status=0, battery=97%
>> work sequence enter: reset collect_progress
>> enter node type=SEQUENCE
[collect] progress=1/3, battery=97%
[main] tick=8 => root status=2, battery=96%
[collect] progress=2/3, battery=10%
[main] tick=9 => root status=2, battery=9%
[collect] progress=3/3, battery=9%
[main] tick=10 => root status=2, battery=8%
[collect] done.
[avoid] nothing to do, pass-through.
[upload] attempt #3 -> SUCCESS
<< exit  node type=SEQUENCE with status=0
[main] tick=11 => root status=0, battery=8%
>> work sequence enter: reset collect_progress
[recharge] charging...
[main] tick=12 => root status=0, battery=100%
>> work sequence enter: reset collect_progress
>> enter node type=SEQUENCE
[collect] progress=1/3, battery=100%
[main] tick=13 => root status=2, battery=99%
[collect] progress=2/3, battery=99%
[main] tick=14 => root status=2, battery=98%
[collect] progress=3/3, battery=98%
[main] tick=15 => root status=2, battery=97%
[collect] done.
[avoid] nothing to do, pass-through.
[upload] attempt #4 -> SUCCESS
<< exit  node type=SEQUENCE with status=0
[main] tick=16 => root status=0, battery=97%
>> work sequence enter: reset collect_progress
>> enter node type=SEQUENCE
[collect] progress=1/3, battery=97%
[main] tick=17 => root status=2, battery=96%
[collect] progress=2/3, battery=96%
[main] tick=18 => root status=2, battery=95%
[collect] progress=3/3, battery=95%
[main] tick=19 => root status=2, battery=94%
*/