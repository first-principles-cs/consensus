/**
 * chaos.c - Chaos testing framework implementation
 */

#include "chaos.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void chaos_init(chaos_state_t* state) {
    memset(state, 0, sizeof(chaos_state_t));

    /* Default config: low chaos */
    state->config.crash_rate = 0.001;
    state->config.restart_rate = 0.01;
    state->config.slow_rate = 0.001;
    state->config.partition_rate = 0.0005;
    state->config.heal_rate = 0.01;
    state->config.min_crash_duration = 10;
    state->config.max_crash_duration = 100;
}

void chaos_configure(chaos_state_t* state, const chaos_config_t* config) {
    state->config = *config;
}

static bool random_event(double rate) {
    if (rate <= 0.0) return false;
    if (rate >= 1.0) return true;
    return ((double)rand() / RAND_MAX) < rate;
}

static uint64_t random_duration(chaos_state_t* state) {
    uint64_t min = state->config.min_crash_duration;
    uint64_t max = state->config.max_crash_duration;
    if (min >= max) return min;
    return min + (rand() % (max - min + 1));
}

chaos_event_t chaos_tick(chaos_state_t* state, int32_t num_nodes,
                          int32_t* affected_node) {
    state->current_tick++;
    *affected_node = -1;

    /* Check for automatic restarts */
    for (int i = 0; i < num_nodes; i++) {
        if (state->crashed[i] && state->current_tick >= state->crash_until[i]) {
            state->crashed[i] = false;
            state->total_restarts++;
            *affected_node = i;
            return CHAOS_RESTART;
        }
    }

    /* Random crash */
    if (random_event(state->config.crash_rate)) {
        int node = rand() % num_nodes;
        if (!state->crashed[node]) {
            chaos_crash_node(state, node, random_duration(state));
            *affected_node = node;
            return CHAOS_CRASH;
        }
    }

    /* Random slow */
    if (random_event(state->config.slow_rate)) {
        int node = rand() % num_nodes;
        state->slow[node] = !state->slow[node];
        *affected_node = node;
        return CHAOS_SLOW;
    }

    /* Random partition */
    if (random_event(state->config.partition_rate)) {
        *affected_node = rand() % num_nodes;
        state->total_partitions++;
        return CHAOS_PARTITION;
    }

    /* Random heal */
    if (random_event(state->config.heal_rate)) {
        return CHAOS_HEAL;
    }

    return CHAOS_NONE;
}

bool chaos_is_crashed(chaos_state_t* state, int32_t node_id) {
    if (node_id < 0 || node_id >= CHAOS_MAX_NODES) return false;
    return state->crashed[node_id];
}

bool chaos_is_slow(chaos_state_t* state, int32_t node_id) {
    if (node_id < 0 || node_id >= CHAOS_MAX_NODES) return false;
    return state->slow[node_id];
}

void chaos_crash_node(chaos_state_t* state, int32_t node_id, uint64_t duration) {
    if (node_id < 0 || node_id >= CHAOS_MAX_NODES) return;
    state->crashed[node_id] = true;
    state->crash_until[node_id] = state->current_tick + duration;
    state->total_crashes++;
}

void chaos_restart_node(chaos_state_t* state, int32_t node_id) {
    if (node_id < 0 || node_id >= CHAOS_MAX_NODES) return;
    state->crashed[node_id] = false;
    state->total_restarts++;
}

void chaos_print_stats(chaos_state_t* state) {
    printf("Chaos Statistics:\n");
    printf("  Total crashes:    %llu\n", (unsigned long long)state->total_crashes);
    printf("  Total restarts:   %llu\n", (unsigned long long)state->total_restarts);
    printf("  Total partitions: %llu\n", (unsigned long long)state->total_partitions);
    printf("  Current tick:     %llu\n", (unsigned long long)state->current_tick);
}
