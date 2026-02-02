/**
 * network_sim.h - Network simulator for integration testing
 *
 * Simulates network conditions including partitions, delays, and message loss.
 */

#ifndef NETWORK_SIM_H
#define NETWORK_SIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define NET_MAX_NODES 10
#define NET_MAX_PENDING 1000

/* Message in the network */
typedef struct {
    int32_t from;
    int32_t to;
    void* data;
    size_t len;
    uint64_t deliver_at;  /* Simulation time to deliver */
} net_message_t;

/* Network simulator state */
typedef struct {
    /* Connectivity matrix: connected[i][j] = true if i can reach j */
    bool connected[NET_MAX_NODES][NET_MAX_NODES];

    /* Pending messages */
    net_message_t pending[NET_MAX_PENDING];
    size_t pending_count;

    /* Simulation time */
    uint64_t current_time;

    /* Configuration */
    uint64_t min_delay;
    uint64_t max_delay;
    double drop_rate;  /* 0.0 to 1.0 */

    /* Statistics */
    uint64_t messages_sent;
    uint64_t messages_delivered;
    uint64_t messages_dropped;
} network_sim_t;

/**
 * Initialize network simulator
 */
void net_init(network_sim_t* net, int32_t num_nodes);

/**
 * Reset network to fully connected state
 */
void net_reset(network_sim_t* net, int32_t num_nodes);

/**
 * Create a network partition
 * Nodes in partition1 cannot communicate with nodes in partition2
 */
void net_partition(network_sim_t* net,
                   const int32_t* partition1, size_t p1_size,
                   const int32_t* partition2, size_t p2_size);

/**
 * Heal all partitions (restore full connectivity)
 */
void net_heal(network_sim_t* net, int32_t num_nodes);

/**
 * Disconnect a specific node from all others
 */
void net_isolate(network_sim_t* net, int32_t node_id, int32_t num_nodes);

/**
 * Reconnect a previously isolated node
 */
void net_reconnect(network_sim_t* net, int32_t node_id, int32_t num_nodes);

/**
 * Set network delay range
 */
void net_set_delay(network_sim_t* net, uint64_t min_ms, uint64_t max_ms);

/**
 * Set message drop rate (0.0 = no drops, 1.0 = all dropped)
 */
void net_set_drop_rate(network_sim_t* net, double rate);

/**
 * Send a message through the network
 * Message may be delayed, dropped, or blocked by partition
 */
bool net_send(network_sim_t* net, int32_t from, int32_t to,
              const void* data, size_t len);

/**
 * Advance simulation time and deliver pending messages
 * Returns number of messages delivered
 * Callback is invoked for each delivered message
 */
typedef void (*net_deliver_fn)(int32_t from, int32_t to,
                                const void* data, size_t len, void* ctx);

size_t net_tick(network_sim_t* net, uint64_t elapsed_ms,
                net_deliver_fn callback, void* ctx);

/**
 * Get number of pending messages
 */
size_t net_pending_count(network_sim_t* net);

/**
 * Clear all pending messages
 */
void net_clear_pending(network_sim_t* net);

/**
 * Print network statistics
 */
void net_print_stats(network_sim_t* net);

#endif /* NETWORK_SIM_H */
