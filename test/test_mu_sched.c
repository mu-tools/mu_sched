// mu_sched/test/test_mu_sched.c

#include "mu_pool.h"
#include "mu_pqueue.h"
#include "mu_pvec.h"
#include "mu_queue.h"
#include "mu_sched.h"
#include "mu_spsc.h"
#include "mu_thunk.h"
#include "mu_time.h"
#include "unity.h"
#include <stddef.h>

// backing-store sizes
#define MAX_TEST_THUNKS 4

//-----------------------------------------------------------------------------
// Virtual‐time support
//-----------------------------------------------------------------------------

static mu_time_abs_t virtual_time;

static mu_time_abs_t get_virtual_time(void) { return virtual_time; }

static void set_virtual_time(mu_time_abs_t t) { virtual_time = t; }

static mu_time_abs_t mk_time(int s, long ns) {
    return (mu_time_abs_t){.seconds = s, .nanoseconds = ns};
}

//-----------------------------------------------------------------------------
// counting_thunk_t: a thunk whose context carries a call_count.
//-----------------------------------------------------------------------------

typedef struct {
    mu_thunk_t thunk;
    int call_count;
} counting_thunk_t;

static void counting_thunk_fn(mu_thunk_t *thunk, void *args) {
    (void)args;
    counting_thunk_t *counting_thunk = (counting_thunk_t *)thunk;
    counting_thunk->call_count++;
}

static void counting_thunk_init(counting_thunk_t *counting_thunk) {
    counting_thunk->call_count = 0;
    mu_thunk_init(&counting_thunk->thunk, counting_thunk_fn);
}

// A thunk whose job is simply to check that
// mu_sched_current_thunk() == the thunk pointer we were given.
static void current_thunk_fn(mu_thunk_t *thunk, void *args) {
    (void)args;
    // This will fail the test if the scheduler didn’t set current_thunk
    // correctly.
    TEST_ASSERT_EQUAL_PTR(thunk, mu_sched_current_thunk());
}

//-----------------------------------------------------------------------------
// Support functions
//-----------------------------------------------------------------------------

/*
 * Build & initialize the singleton scheduler for test:
 *  - SPSC queue
 *  - ASAP pqueue
 *  - timed pvec
 *  - pool for mu_event_t
 * Then override its time function so set_virtual_time() controls the its time.
 */
static void init_scheduler_for_test(void) {
    static mu_event_t pool_store[MAX_TEST_THUNKS];
    static void *event_store[MAX_TEST_THUNKS];
    static void *asap_store[MAX_TEST_THUNKS];
    static mu_spsc_item_t isr_store[MAX_TEST_THUNKS];

    static mu_spsc_t isr_q;
    static mu_pqueue_t asap_q;
    static mu_pvec_t event_q;
    static mu_pool_t pool;

    TEST_ASSERT_EQUAL(MU_SPSC_ERR_NONE,
                      mu_spsc_init(&isr_q, isr_store, MAX_TEST_THUNKS));
    TEST_ASSERT_NOT_NULL(mu_pqueue_init(&asap_q, asap_store, MAX_TEST_THUNKS));
    TEST_ASSERT_NOT_NULL(mu_pvec_init(&event_q, event_store, MAX_TEST_THUNKS));
    TEST_ASSERT_NOT_NULL(
        mu_pool_init(&pool, pool_store, MAX_TEST_THUNKS, sizeof(mu_event_t)));

    TEST_ASSERT_TRUE(mu_sched_init(&isr_q, &asap_q, &event_q, &pool));

    /* Plug in our virtual clock -- use set_virtual_time to set time. */
    mu_sched_set_time_fn(get_virtual_time);

    /* Start virtual time at zero */
    set_virtual_time(mk_time(0, 0));
}

void setUp(void) {}
void tearDown(void) {}

// *****************************************************************************
// The tests...

void test_mu_sched_now_runs_immediately(void) {
    counting_thunk_t A;

    init_scheduler_for_test();
    counting_thunk_init(&A);

    TEST_ASSERT_TRUE(mu_sched_now(&A.thunk));
    TEST_ASSERT_EQUAL(0, A.call_count);
    TEST_ASSERT_TRUE(mu_sched_has_runnable_thunk());
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_FALSE(mu_sched_has_runnable_thunk());
}

void test_mu_sched_from_isr_runs_first(void) {
    counting_thunk_t A, B, C;

    init_scheduler_for_test();
    counting_thunk_init(&A);
    counting_thunk_init(&B);
    counting_thunk_init(&C);

    TEST_ASSERT_TRUE(mu_sched_now(&A.thunk));
    TEST_ASSERT_TRUE(mu_sched_from_isr(&B.thunk));
    TEST_ASSERT_TRUE(mu_sched_from_isr(&C.thunk));

    // thunks in the spsc_q are run first
    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);
    TEST_ASSERT_EQUAL(1, B.call_count);
    TEST_ASSERT_EQUAL(0, C.call_count);

    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);
    TEST_ASSERT_EQUAL(1, B.call_count);
    TEST_ASSERT_EQUAL(1, C.call_count);

    // spqc_q is now empty: asap_q events can now run
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(1, B.call_count);
    TEST_ASSERT_EQUAL(1, C.call_count);
}

void test_mu_sched_at_with_now_timestamp(void) {
    counting_thunk_t A;

    init_scheduler_for_test();
    counting_thunk_init(&A);

    TEST_ASSERT_TRUE(mu_sched_at(&A.thunk, get_virtual_time()));
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
}

void test_mu_sched_at_respects_delay(void) {
    counting_thunk_t A;

    init_scheduler_for_test();
    counting_thunk_init(&A);

    set_virtual_time(mk_time(0, 0));
    TEST_ASSERT_TRUE(mu_sched_at(&A.thunk, mk_time(0, 5)));

    set_virtual_time(mk_time(0, 4));
    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);

    set_virtual_time(mk_time(0, 5));
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);

    set_virtual_time(mk_time(0, 6));
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
}

void test_mu_sched_in_respects_delay(void) {
    counting_thunk_t A;

    init_scheduler_for_test();
    counting_thunk_init(&A);

    set_virtual_time(mk_time(100, 0));
    TEST_ASSERT_TRUE(mu_sched_in(&A.thunk, 5)); // relative time...

    set_virtual_time(mk_time(100, 4));
    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);

    set_virtual_time(mk_time(100, 5));
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);

    set_virtual_time(mk_time(100, 6));
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
}

void test_mu_sched_idle_thunk_fires_when_nothing_else(void) {
    counting_thunk_t A;

    init_scheduler_for_test();
    counting_thunk_init(&A);

    mu_sched_set_idle_thunk(&A.thunk);
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    mu_sched_step();
    TEST_ASSERT_EQUAL(2, A.call_count);

    mu_sched_set_idle_thunk(NULL);
    mu_sched_step();
    TEST_ASSERT_EQUAL(2, A.call_count);
}

/**
 * Two events at t=5 and t=10.  If we advance time to t=20 then call
 * mu_sched_step() twice, we should see the t=5 event fire first (earliest),
 * then the t=10 event.
 */
void test_mu_sched_step_earliest_first(void) {
    init_scheduler_for_test();

    counting_thunk_t A, B;
    counting_thunk_init(&A);
    counting_thunk_init(&B);

    /* schedule B at t=10, then A at t=5 */
    TEST_ASSERT_TRUE(mu_sched_at(&B.thunk, mk_time(10, 0)));
    TEST_ASSERT_TRUE(mu_sched_at(&A.thunk, mk_time(5, 0)));

    /* nothing will run until time >= 10 */
    set_virtual_time(mk_time(0, 0));
    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);
    TEST_ASSERT_EQUAL(0, B.call_count);

    /* advance time past the event timestamps */
    set_virtual_time(mk_time(20, 0));

    /* first step should run A (t=5) */
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(0, B.call_count);

    /* second step should run B (t=10) */
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(1, B.call_count);
}

/**
 * Identical to test_mu_sched_step_earliest_first, but inserted in the 
 * opposite order to verify sorting function
 */
void test_mu_sched_step_latest_last(void) {
    init_scheduler_for_test();

    counting_thunk_t A, B;
    counting_thunk_init(&A);
    counting_thunk_init(&B);

    /* schedule A at t=5, then B at t=10 */
    TEST_ASSERT_TRUE(mu_sched_at(&A.thunk, mk_time(5, 0)));
    TEST_ASSERT_TRUE(mu_sched_at(&B.thunk, mk_time(10, 0)));

    /* nothing will run until time >= 10 */
    set_virtual_time(mk_time(0, 0));
    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);
    TEST_ASSERT_EQUAL(0, B.call_count);

    /* advance time past the event timestamps */
    set_virtual_time(mk_time(20, 0));

    /* first step should run A (t=5) */
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(0, B.call_count);

    /* second step should run B (t=10) */
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(1, B.call_count);
}

/**
 * Two events at the same timestamp t=7.  We schedule A then B.
 * Because we insert ties with INSERT_FIRST, B sits before A in the pvec,
 * but popping from the back still executes A first, then B.
 */
void test_mu_sched_step_tied_fifo(void) {
    init_scheduler_for_test();

    counting_thunk_t A, B;
    counting_thunk_init(&A);
    counting_thunk_init(&B);

    mu_time_abs_t t = mk_time(7, 7);
    TEST_ASSERT_TRUE(mu_sched_at(&A.thunk, t)); // incumbent
    TEST_ASSERT_TRUE(mu_sched_at(&B.thunk, t)); // newcomer

    /* nothing will run until time >= 7.7 */
    set_virtual_time(mk_time(0, 0));
    mu_sched_step();
    TEST_ASSERT_EQUAL(0, A.call_count);
    TEST_ASSERT_EQUAL(0, B.call_count);

    /* advance time past the event timestamp */
    set_virtual_time(mk_time(8, 0));

    /* first step runs A */
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(0, B.call_count);

    /* second step runs B */
    mu_sched_step();
    TEST_ASSERT_EQUAL(1, A.call_count);
    TEST_ASSERT_EQUAL(1, B.call_count);
}

void test_mu_sched_current_thunk_reports_self(void) {
    mu_thunk_t t;

    // initialize scheduler and override time if you do that in setUp...
    init_scheduler_for_test();

    // init our special thunk
    TEST_ASSERT_NOT_NULL(mu_thunk_init(&t, current_thunk_fn));

    // before anything running, current_thunk should be NULL
    TEST_ASSERT_NULL(mu_sched_current_thunk());

    // schedule and run our thunk
    TEST_ASSERT_TRUE(mu_sched_now(&t));
    mu_sched_step();   // inside here, current_thunk_fn will fire that assertion

    // after step is done, current_thunk should be NULL again
    TEST_ASSERT_NULL(mu_sched_current_thunk());
}

// *****************************************************************************
// Test driver

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_mu_sched_now_runs_immediately);
    RUN_TEST(test_mu_sched_from_isr_runs_first);
    RUN_TEST(test_mu_sched_at_with_now_timestamp);
    RUN_TEST(test_mu_sched_at_respects_delay);
    RUN_TEST(test_mu_sched_in_respects_delay);
    RUN_TEST(test_mu_sched_idle_thunk_fires_when_nothing_else);
    RUN_TEST(test_mu_sched_step_earliest_first);
    RUN_TEST(test_mu_sched_step_latest_last);
    RUN_TEST(test_mu_sched_step_tied_fifo);
    RUN_TEST(test_mu_sched_current_thunk_reports_self);

    return UNITY_END();
}