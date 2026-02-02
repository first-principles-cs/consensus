/**
 * network_sim.c - Network simulator implementation
 */

#include "network_sim.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void net_init(network_sim_t* net, int32_t num_nodes) {
    memset(net, 0, sizeof(network_sim_t));
    net_reset(net, num_nodes);
    net->min_delay = 1;
    net->max_delay = 10;
    net->drop_rate = 0.0;
}

void net_reset(network_sim_t* net, int32_t num_nodes) {
    /* Fully connected */
    for (int i = 0; i < num_nodes; i++) {
        for (int j = 0; j < num_nodes; j++) {
            net->connected[i][j] = (i != j);
        }
    }
    net_clear_pending(net);
}

void net_partition(network_sim_t* net,
                   const int32_t* partition1, size_t p1_size,
                   const int32_t* partition2, size_t p2_size) {
    for (size_t i = 0; i < p1_size; i++) {
        for (size_t j = 0; j < p2_size; j++) {
            int32_t n1 = partition1[i];
            int32_t n2 = partition2[j];
            net->connected[n1][n2] = false;
            net->connected[n2][n1] = false;
        }
    }
}

void net_heal(network_sim_t* net, int32_t num_nodes) {
    net_reset(net, num_nodes);
}

void net_isolate(network_sim_t* net, int32_t node_id, int32_t num_nodes) {
    for (int i = 0; i < num_nodes; i++) {
        net->connected[node_id][i] = false;
        net->connected[i][node_id] = false;
    }
}

void net_reconnect(network_sim_t* net, int32_t node_id, int32_t num_nodes) {
    for (int i = 0; i < num_nodes; i++) {
        if (i != node_id) {
            net->connected[node_id][i] = true;
            net->connected[i][node_id] = true;
        }
    }
}

void net_set_delay(network_sim_t* net, uint64_t min_ms, uint64_t max_ms) {
    net->min_delay = min_ms;
    net->max_delay = max_ms;
}

void net_set_drop_rate(network_sim_t* net, double rate) {
    net->drop_rate = rate;
}

static uint64_t random_delay(network_sim_t* net) {
    if (net->min_delay == net->max_delay) {
        return net->min_delay;
    }
    return net->min_delay + (rand() % (net->max_delay - net->min_delay + 1));
}

static bool should_drop(network_sim_t* net) {
    if (net->drop_rate <= 0.0) return false;
    if (net->drop_rate >= 1.0) return true;
    return ((double)rand() / RAND_MAX) < net->drop_rate;
}

bool net_send(network_sim_t* net, int32_t from, int32_t to,
              const void* data, size_t len) {
    net->messages_sent++;

    /* Check connectivity */
    if (!net->connected[from][to]) {
        net->messages_dropped++;
        return false;
    }

    /* Check drop rate */
    if (should_drop(net)) {
        net->messages_dropped++;
        return false;
    }

    /* Check capacity */
    if (net->pending_count >= NET_MAX_PENDING) {
        net->messages_dropped++;
        return false;
    }

    /* Queue message */
    net_message_t* msg = &net->pending[net->pending_count++];
    msg->from = from;
    msg->to = to;
    msg->data = malloc(len);
    if (!msg->data) {
        net->pending_count--;
        net->messages_dropped++;
        return false;
    }
    memcpy(msg->data, data, len);
    msg->len = len;
    msg->deliver_at = net->current_time + random_delay(net);

    return true;
}

size_t net_tick(network_sim_t* net, uint64_t elapsed_ms,
                net_deliver_fn callback, void* ctx) {
    net->current_time += elapsed_ms;

    size_t delivered = 0;
    size_t i = 0;

    while (i < net->pending_count) {
        net_message_t* msg = &net->pending[i];

        if (msg->deliver_at <= net->current_time) {
            /* Check if still connected */
            if (net->connected[msg->from][msg->to]) {
                if (callback) {
                    callback(msg->from, msg->to, msg->data, msg->len, ctx);
                }
                net->messages_delivered++;
                delivered++;
            } else {
                net->messages_dropped++;
            }

            /* Free and remove */
            free(msg->data);
            net->pending[i] = net->pending[--net->pending_count];
        } else {
            i++;
        }
    }

    return delivered;
}

size_t net_pending_count(network_sim_t* net) {
    return net->pending_count;
}

void net_clear_pending(network_sim_t* net) {
    for (size_t i = 0; i < net->pending_count; i++) {
        free(net->pending[i].data);
    }
    net->pending_count = 0;
}

void net_print_stats(network_sim_t* net) {
    printf("Network Statistics:\n");
    printf("  Messages sent:      %llu\n", (unsigned long long)net->messages_sent);
    printf("  Messages delivered: %llu\n", (unsigned long long)net->messages_delivered);
    printf("  Messages dropped:   %llu\n", (unsigned long long)net->messages_dropped);
    printf("  Pending messages:   %zu\n", net->pending_count);
}
