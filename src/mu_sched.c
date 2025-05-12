/**
 * MIT License
 *
 * Copyright (c) 2025 R. D. Poor & Assoc <rdpoor @ gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file mu_sched.c
 * @brief Simple cooperative scheduler for embedded systems using mu_thunk
 * thunks.
 */

// *****************************************************************************
// Includes

#include "mu_sched.h"
#include "mu_pool.h"
#include "mu_pqueue.h"
#include "mu_pvec.h"
#include "mu_spsc.h"
#include "mu_store.h"
#include "mu_thunk.h"
#include "mu_time.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// *****************************************************************************
// Private types and definitions

/**
 * @brief Represents the scheduler instance.
 *
 * Contains pointers to the user-provided and already initialized queue and pool
 * instances. The user is responsible for declaring and initializing the actual
 * queue and pool structures and their backing memory, and passing pointers to
 * them during scheduler initialization.
 *
 * This struct is internal to the mu_sched implementation and not directly
 * exposed to the user API.
 */
typedef struct mu_sched_t {
    mu_spsc_t *interrupt_q; /**< Interrupt queue of mu_thunk_t* pointers */
    mu_pqueue_t *asap_q;    /**< ASAP queue of mu_thunk_t* pointers */
    mu_pvec_t *event_q;     /**< Event queue of mu_event_t* pointers */
    mu_pool_t *event_pool;  /**< Pool for mu_event_t wrappers */
    mu_thunk_t *idle_thunk; /**< Idle thunk to run when queues empty */
    mu_time_abs_t (*get_time)(void); /**< Function to fetch current time */
    mu_thunk_t *current_thunk;       /**< The thunk currently being executed */
} mu_sched_t;

// *****************************************************************************
// Private data

static mu_sched_t s_sched;
static bool s_sched_initialized = false;

// *****************************************************************************
// Private function prototypes

static bool is_scheduler_initialized(void);

/**
 * @brief Comparison function for scheduling events.
 *
 * The `compare_events` function is designed to place the "soonest" event at the
 * end of the event queue.  This means that mu_pvec_pop(event_queue) will fetch
 * the soonest event efficiently.  Also since most events are scheduled in the
 * near future, the minimizes memory moves when opening up a new slot in the
 * event queue.
 *
 * Provides ascending-order comparison of mu_event_t pointers based on their
 * timestamp, so that the earliest event (smallest timestamp) sorts first.
 * Used by mu_pvec_sorted_insert on the event queue.
 */
static int compare_events(const void *a, const void *b);

// *****************************************************************************
// Public function implementations

bool mu_sched_init(mu_spsc_t *interrupt_q, mu_pqueue_t *asap_q,
                   mu_pvec_t *event_q, mu_pool_t *event_pool) {
    if (!interrupt_q || !asap_q || !event_q || !event_pool) {
        s_sched_initialized = false;
        return false;
    }

    s_sched.interrupt_q = interrupt_q;
    s_sched.asap_q = asap_q;
    s_sched.event_q = event_q;
    s_sched.event_pool = event_pool;
    s_sched.idle_thunk = NULL;
    s_sched.current_thunk = NULL;
    s_sched.get_time = mu_time_now; // Default time source
    s_sched_initialized = true;
    return true;
}

bool mu_sched_now(mu_thunk_t *thunk) {
    if (!is_scheduler_initialized() || !thunk) {
        return false;
    }
    return mu_pqueue_put(s_sched.asap_q, thunk) == MU_STORE_ERR_NONE;
}

bool mu_sched_at(mu_thunk_t *thunk, mu_time_abs_t timestamp) {
    if (!is_scheduler_initialized() || !thunk) {
        return false;
    }
    mu_event_t *evt = mu_pool_alloc(s_sched.event_pool);
    if (!evt) {
        return false;
    }
    evt->thunk = thunk;
    evt->timestamp = timestamp;
    if (mu_pvec_sorted_insert(s_sched.event_q, evt, compare_events,
                              MU_STORE_INSERT_FIRST) != MU_STORE_ERR_NONE) {
        mu_pool_free(s_sched.event_pool, evt);
        return false;
    }
    return true;
}

bool mu_sched_in(mu_thunk_t *thunk, mu_time_rel_t delay) {
    if (!is_scheduler_initialized() || !thunk) {
        return false;
    }
    mu_time_abs_t now = s_sched.get_time();
    return mu_sched_at(thunk, mu_time_offset(now, delay));
}

bool mu_sched_from_isr(mu_thunk_t *thunk) {
    if (!s_sched_initialized || !thunk) {
        return false;
    }
    return mu_spsc_put(s_sched.interrupt_q, thunk) == MU_SPSC_ERR_NONE;
}

void mu_sched_set_idle_thunk(mu_thunk_t *idle) {
    if (!is_scheduler_initialized()) {
        return;
    }
    s_sched.idle_thunk = idle;
}

void mu_sched_set_time_fn(mu_time_abs_t (*fn)(void)) {
    if (!is_scheduler_initialized()) {
        return;
    }
    s_sched.get_time = fn ? fn : mu_time_now;
}

/**
 * @brief Performs one scheduling pass: processes interrupts, due timed events,
 *        then executes the next thunk (or idle thunk). Prevents recursion by
 *        returning immediately if already in a step.
 */
void mu_sched_step(void) {
    if (!is_scheduler_initialized()) {
        return;
    }

    /* Prevent recursive scheduling if inside a thunk */
    if (s_sched.current_thunk != NULL) {
        return;
    }

    /* Clear any previous thunk marker */
    s_sched.current_thunk = NULL;

    // 1) ISR has top priority: if there's an ISR thunk, run it now and return
    mu_spsc_item_t isr_item;
    if (mu_spsc_get(s_sched.interrupt_q, &isr_item) == MU_SPSC_ERR_NONE) {
        s_sched.current_thunk = (mu_thunk_t *)isr_item;
        mu_thunk_call(s_sched.current_thunk, NULL);
        s_sched.current_thunk = NULL;
        return;
    }

    /* 2) Move due timed events into ASAP queue */
    mu_event_t *evt;
    mu_time_abs_t now = s_sched.get_time();
    while (!mu_pqueue_is_full(s_sched.asap_q) &&
           mu_pvec_peek(s_sched.event_q, (void **)&evt) == MU_STORE_ERR_NONE &&
           !mu_time_is_after(evt->timestamp, now)) {

        if (mu_pvec_pop(s_sched.event_q, (void **)&evt) != MU_STORE_ERR_NONE) {
            /* Should not happen: peek succeeded but pop failed */
            break;
        }

        if (mu_pqueue_put(s_sched.asap_q, evt->thunk) != MU_STORE_ERR_NONE) {
            /* ASAP queue full: free wrapper and stop */
            mu_pool_free(s_sched.event_pool, evt);
            break;
        }

        /* Free the event wrapper now that its thunk is enqueued */
        mu_pool_free(s_sched.event_pool, evt);
    }

    /* 3) Execute next available thunk, or idle if none */
    mu_thunk_t *thunk_ptr;
    if (mu_pqueue_get(s_sched.asap_q, (void **)&thunk_ptr) ==
        MU_STORE_ERR_NONE) {
        s_sched.current_thunk = thunk_ptr;
        mu_thunk_call(thunk_ptr, NULL);
        s_sched.current_thunk = NULL;
    } else if (s_sched.idle_thunk) {
        s_sched.current_thunk = s_sched.idle_thunk;
        mu_thunk_call(s_sched.idle_thunk, NULL);
        s_sched.current_thunk = NULL;
    }
}

bool mu_sched_has_runnable_thunk(void) {
    if (!is_scheduler_initialized()) {
        return false;
    }
    // We don't check the spsc_q because (A) there's no interrupt safe
    // mu_spsc_is_empty() function and (B) an interrupt could happen
    // any time, so we assume any events from spsc_q have already been
    // move to the asap_q.
    return !mu_pqueue_is_empty(s_sched.asap_q);
}

const mu_thunk_t *mu_sched_current_thunk(void) {
    if (!is_scheduler_initialized()) {
        return NULL;
    }
    return s_sched.current_thunk;
}

static bool is_scheduler_initialized(void) { return s_sched_initialized; }

static int compare_events(const void *a, const void *b) {
    const mu_event_t *ea = *(const mu_event_t *const *)a;
    const mu_event_t *eb = *(const mu_event_t *const *)b;
    // Ascending timestamp: earliest events get put at the end of the event_q
    if (mu_time_is_before(ea->timestamp, eb->timestamp)) {
        return 1;
    } else if (mu_time_is_after(ea->timestamp, eb->timestamp)) {
        return -1;
    }
    return 0;
}
