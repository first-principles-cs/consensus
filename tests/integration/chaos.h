/**
 * chaos.h - Chaos testing framework
 *
 * Provides tools for chaos testing: random failures, delays, and restarts.
 */

#ifndef CHAOS_H
#define CHAOS_H

#include <stdint.h>
#include <stdbool.h>

#define CHAOS_MAX_NODES 10

/* Chaos event types */
typedef enum {
    CHAOS_NONE = 0,
    CHAOS_CRASH,        /* Node crashes */
    CHAOS_RESTART,      /* Node restarts */
    CHAOS_SLOW,         /* Node becomes slow */
    CHAOS_PARTITION,    /* Network partition */
    CHAOS_HEAL,         /* Heal partition */
} chaos_event_t;

/* Chaos configuration */
typedef struct {
    double crash_rate;      /* Probability of crash per tick */
    double restart_rate;    /* Probability of restart per tick */
    double slow_rate;       /* Probability of becoming slow */
    double partition_rate;  /* Probability of partition */
    double heal_rate;       /* Probability of healing */

    uint64_t min_crash_duration;   /* Min ticks before restart */
    uint64_t max_crash_duration;   /* Max ticks before restart */
} chaos_config_t;

/* Chaos state */
typedef struct {
    chaos_config_t config;

    /* Node states */
    bool crashed[CHAOS_MAX_NODES];
    uint64_t crash_until[CHAOS_MAX_NODES];
    bool slow[CHAOS_MAX_NODES];

    /* Statistics */
    uint64_t total_crashes;
    uint64_t total_restarts;
    uint64_t total_partitions;
    uint64_t current_tick;
} chaos_state_t;

/**
 * Initialize chaos state with default config
 */
void chaos_init(chaos_state_t* state);

/**
 * Set chaos configuration
 */
void chaos_configure(chaos_state_t* state, const chaos_config_t* config);

/**
 * Tick chaos - may trigger random events
 * Returns the event that occurred (CHAOS_NONE if nothing)
 */
chaos_event_t chaos_tick(chaos_state_t* state, int32_t num_nodes,
                          int32_t* affected_node);

/**
 * Check if a node is crashed
 */
bool chaos_is_crashed(chaos_state_t* state, int32_t node_id);

/**
 * Check if a node is slow
 */
bool chaos_is_slow(chaos_state_t* state, int32_t node_id);

/**
 * Manually crash a node
 */
void chaos_crash_node(chaos_state_t* state, int32_t node_id, uint64_t duration);

/**
 * Manually restart a node
 */
void chaos_restart_node(chaos_state_t* state, int32_t node_id);

/**
 * Print chaos statistics
 */
void chaos_print_stats(chaos_state_t* state);

#endif /* CHAOS_H */
