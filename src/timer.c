/**
 * timer.c - Timer management implementation
 */

#include "timer.h"
#include "raft.h"
#include "election.h"
#include "param.h"
#include <stdlib.h>
#include <time.h>

static uint32_t timer_seed_value = 0;
static bool seed_initialized = false;

void raft_timer_seed(uint32_t seed) {
    timer_seed_value = seed;
    seed_initialized = true;
    srand(seed);
}

uint64_t raft_random_election_timeout(void) {
    if (!seed_initialized) {
        srand((unsigned int)time(NULL));
        seed_initialized = true;
    }
    uint64_t range = RAFT_ELECTION_TIMEOUT_MAX_MS - RAFT_ELECTION_TIMEOUT_MIN_MS;
    return RAFT_ELECTION_TIMEOUT_MIN_MS + (rand() % (range + 1));
}

void raft_reset_election_timer(raft_node_t* node) {
    if (!node) return;
    node->election_timer_ms = 0;
    node->election_timeout_ms = raft_random_election_timeout();
}

raft_status_t raft_tick_election(raft_node_t* node, uint64_t elapsed_ms) {
    if (!node) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;
    if (node->role == RAFT_LEADER) return RAFT_OK;

    node->election_timer_ms += elapsed_ms;

    if (node->election_timer_ms >= node->election_timeout_ms) {
        return raft_start_election(node);
    }

    return RAFT_OK;
}

raft_status_t raft_tick_heartbeat(raft_node_t* node, uint64_t elapsed_ms) {
    if (!node) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;
    if (node->role != RAFT_LEADER) return RAFT_OK;

    node->heartbeat_timer_ms += elapsed_ms;

    if (node->heartbeat_timer_ms >= RAFT_HEARTBEAT_INTERVAL_MS) {
        node->heartbeat_timer_ms = 0;
        return raft_send_heartbeats(node);
    }

    return RAFT_OK;
}

raft_status_t raft_tick(raft_node_t* node, uint64_t elapsed_ms) {
    if (!node) return RAFT_INVALID_ARG;

    raft_status_t status = raft_tick_election(node, elapsed_ms);
    if (status != RAFT_OK) return status;

    return raft_tick_heartbeat(node, elapsed_ms);
}
