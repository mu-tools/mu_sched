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
 * @file mu_sched.h
 * @brief Simple cooperative scheduler for embedded systems using mu_thunk
 * thunks.
 *
 * This module provides a fixed-memory scheduler that manages thunks represented
 * by mu_thunk_t objects across three priority queues: interrupt, asap,
 * and event. It also supports an idle thunk hook.
 *
 * The scheduler requires initialized instances of mu_spsc, mu_pqueue, mu_pvec,
 * and mu_pool modules, with user-provided memory for their backing stores.
 * The scheduler manages *pointers* to mu_thunk_t objects; the user is
 * responsible for the allocation and lifetime of the mu_thunk_t instances
 * themselves (e.g., using static/global variables).
 */

#ifndef MU_SCHED_H
#define MU_SCHED_H

// *****************************************************************************
// C++ Compatibility

#ifdef __cplusplus
extern "C" {
#endif

// *****************************************************************************
// Includes

#include <stdbool.h>
#include <stddef.h>

// Include headers for the required mu_store modules and time API
// Note: User must ensure these headers define the required types (mu_spsc_t,
// mu_pqueue_t, mu_pvec_t, mu_pool_t, mu_time_abs_t, mu_time_rel_t) and
// functions. These modules' queue types should be configured to store void*
// pointers.
#include "mu_pool.h"   // For mu_pool_t (needed for mu_event_t)
#include "mu_pqueue.h" // For mu_pqueue_t (stores mu_thunk_t* pointers)
#include "mu_pvec.h"   // For mu_pvec_t (stores mu_event_t* pointers)
#include "mu_spsc.h"   // For mu_spsc_t (stores mu_thunk_t*pointers)
#include "mu_thunk.h"  // For mu_thunk_t definition
#include "mu_time.h"   // For mu_time_abs_t, mu_time_rel_t, mu_time_xxx()

// *****************************************************************************
// Public types and definitions

/**
 * @brief A mu_event is a mu_thunk scheduled to run at a specific time.
 *
 * Used internally by the event queue. These objects are allocated from a pool.
 * TODO: Consider making mu_event_t a standalone module.
 */
typedef struct {
    mu_thunk_t *thunk; ///< Pointer to the thunk to be executed.
    mu_time_abs_t
        timestamp; ///< The absolute time at which the thunk should run.
} mu_event_t;

// *****************************************************************************
// Public function prototypes

/**
 * @brief Initializes the scheduler instance.
 *
 * Links the scheduler to the user-provided and already initialized queue and
 * pool instances. The user is responsible for ensuring the provided queues/vecs
 * are configured to store void* pointers. The event pool must be
 * configured with item size appropriate for `mu_event_t`.
 *
 * @param interrupt_q Pointer to the initialized mu_spsc_t instance for the
 * interrupt queue (stores mu_thunk_t*). Must not be NULL.
 * @param asap_q Pointer to the initialized mu_pqueue_t instance for the
 * asap_q (stores mu_thunk_t*). Must not be NULL.
 * @param event_q Pointer to the initialized mu_pvec_t instance for the event
 * queue (stores mu_event_t*). Must not be NULL.
 * @param event_pool Pointer to the initialized mu_pool_t instance for
 * mu_event_t objects. Must not be NULL. Item size should be
 * sizeof(mu_event_t).
 * @return true on success or falseL on failure
 * (e.g., invalid parameters).
 */
bool mu_sched_init(mu_spsc_t *interrupt_q, mu_pqueue_t *asap_q,
                   mu_pvec_t *event_q, mu_pool_t *event_pool);

/**
 * @brief Schedules a thunk to run as soon as possible.
 *
 * Adds the pointer to the thunk to the asap_q. The caller is
 * responsible for ensuring the memory pointed to by `thunk` remains valid until
 * the thunk has finished execution and is no longer in any scheduler queue. Can
 * be called from thunk context.
 *
 * @param thunk Pointer to the thunk to schedule. Must not be NULL.
 * @return true on success, false if the asap_q is full or invalid
 * scheduler.
 */
bool mu_sched_now(mu_thunk_t *thunk);

/**
 * @brief Schedules a thunk to run at a specific absolute time.
 *
 * Allocates a `mu_event_t` wrapper from the event pool,
 * populates it with the thunk pointer and timestamp, and inserts the allocated
 * wrapper's pointer into the event queue, ordered by timestamp. The caller is
 * responsible for ensuring the memory pointed to by `thunk` remains valid until
 * the thunk has finished execution and is no longer in any scheduler queue. Can
 * be called from thunk context.
 *
 * @param thunk Pointer to the thunk to schedule. Must not be NULL.
 * @param timestamp The absolute time at which the thunk should run.
 * @return true on success, false if the event queue is full, event
 * pool is full, or invalid scheduler.
 */
bool mu_sched_at(mu_thunk_t *thunk, mu_time_abs_t timestamp);

/**
 * @brief Schedules a thunk to run after a given delay.
 *
 * Calculates the target time and calls mu_sched_at. The caller is responsible
 * for ensuring the memory pointed to by `thunk` remains valid until the thunk
 * has finished execution and is no longer in any scheduler queue. Can be called
 * from thunk context.
 *
 * @param thunk Pointer to the thunk to schedule. Must not be NULL.
 * @param delay The relative time at which the thunk should run.
 * @return true on success, false if the event queue is full, event
 * pool is full, or invalid scheduler.
 */
bool mu_sched_in(mu_thunk_t *thunk, mu_time_rel_t delay);

/**
 * @brief Schedules a thunk to run from an interrupt context.
 *
 * Adds the pointer to the thunk to the interrupt queue. Any thunk in the
 * interrupt queue is processed first by the main scheduler loop. **Important:**
 * The thunk must reside in memory that is guaranteed to be valid and accessible
 * after the ISR returns (e.g., static or global variables). Do NOT schedule
 * pointers to ISR stack variables.
 *
 * @param thunk Pointer to the thunk to schedule. Must not be NULL.
 * @return true on success, false if the interrupt queue is full.
 */
bool mu_sched_from_isr(mu_thunk_t *thunk);

/**
 * @brief Sets the idle thunk.
 *
 * This thunk is executed when there are no other thunks to run in
 * the asap or interrupt queues. Passing NULL removes the idle thunk hook.
 *
 * @param idle_thunk Pointer to the thunk to use as the idle thunk, or NULL. The
 * scheduler stores this pointer. The user is responsible for ensuring the
 * memory pointed to by this pointer remains valid for the lifetime it is set as
 * the idle thunk.
 */
void mu_sched_set_idle_thunk(mu_thunk_t *idle_thunk);

/**
 * @brief Override the time source.
 *
 * By default the scheduler calls mu_time_now().  A test can inject
 * its own “now” function to simulate time jumps.
 */
void mu_sched_set_time_fn(mu_time_abs_t (*fn)(void));

/**
 * @brief Executes one scheduling pass.
 *
 * Checks queues in priority order (interrupt, event, asap) and runs
 * the next available thunk. Runs the idle thunk if no other thunks are ready.
 * This function should be called repeatedly in the application's main loop.
 */
void mu_sched_step(void);

/**
 * @brief Checks if there are any thunks ready to run in the interrupt or
 * asap_qs.
 *
 * This excludes thunks still pending in the event queue. Useful for deciding
 * whether the system can enter a low-power sleep mode.
 *
 * @return true if there are thunks in the interrupt or asap_q, false
 * otherwise.
 */
bool mu_sched_has_runnable_thunk(void);

/**
 * @brief Gets the thunk (thunk) currently being executed by the scheduler.
 *
 * Returns a pointer to the thunk object that is currently inside its `fn`
 * execution.
 *
 * @return A pointer to the const mu_thunk_t currently being executed, or NULL
 * if the scheduler is not currently inside a thunk's execution context (e.g.,
 * between thunks, running queue management, or running the idle thunk). The
 * returned pointer points to the user-owned thunk object.
 */
const mu_thunk_t *mu_sched_current_thunk(void);

// *****************************************************************************
// End of file

#ifdef __cplusplus
}
#endif

#endif /* MU_SCHED_H */
