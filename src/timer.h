/**
 * timer.h - Timer management for Raft consensus
 *
 * Handles election and heartbeat timeouts.
 */

#ifndef RAFT_TIMER_H
#define RAFT_TIMER_H

#include "types.h"

/**
 * Generate a random election timeout within configured range
 */
uint64_t raft_random_election_timeout(void);

/**
 * Seed the random number generator (for deterministic testing)
 */
void raft_timer_seed(uint32_t seed);

/**
 * Process election timer tick
 * Called periodically with elapsed time since last tick
 * May trigger election timeout and state transition
 */
raft_status_t raft_tick_election(raft_node_t* node, uint64_t elapsed_ms);

/**
 * Process heartbeat timer tick (for leaders)
 * Called periodically with elapsed time since last tick
 * May trigger heartbeat sends
 */
raft_status_t raft_tick_heartbeat(raft_node_t* node, uint64_t elapsed_ms);

/**
 * Process all timers
 * Convenience function that calls both tick_election and tick_heartbeat
 */
raft_status_t raft_tick(raft_node_t* node, uint64_t elapsed_ms);

/**
 * Reset election timer with new random timeout
 */
void raft_reset_election_timer(raft_node_t* node);

#endif /* RAFT_TIMER_H */
