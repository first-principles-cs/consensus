/**
 * test_chaos.c - Chaos testing for Raft cluster
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "chaos.h"
#include "network_sim.h"
#include "../../src/raft.h"
#include "../../src/election.h"
#include "../../src/timer.h"

#define NUM_NODES 5

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    name(); \
    tests_run++; \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

/* Test cluster state */
static raft_node_t* nodes[NUM_NODES];
static network_sim_t network;
static chaos_state_t chaos;

/* Message delivery callback */
static void deliver_message(int32_t from, int32_t to,
                            const void* data, size_t len, void* ctx) {
    (void)ctx;
    if (nodes[to] && nodes[to]->running && !chaos_is_crashed(&chaos, to)) {
        raft_receive_message(nodes[to], from, data, len);
    }
}

/* Send callback for Raft nodes */
static void cluster_send(raft_node_t* node, int32_t peer,
                         const void* msg, size_t len, void* ud) {
    (void)ud;
    if (!chaos_is_crashed(&chaos, node->node_id)) {
        net_send(&network, node->node_id, peer, msg, len);
    }
}

/* Create test cluster */
static void create_cluster(void) {
    net_init(&network, NUM_NODES);
    chaos_init(&chaos);

    for (int i = 0; i < NUM_NODES; i++) {
        raft_config_t config = {
            .node_id = i,
            .num_nodes = NUM_NODES,
            .send_fn = cluster_send,
            .data_dir = NULL,
        };
        nodes[i] = raft_create(&config);
        raft_start(nodes[i]);
        raft_reset_election_timer(nodes[i]);
    }
}

/* Destroy test cluster */
static void destroy_cluster(void) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i]) {
            raft_destroy(nodes[i]);
            nodes[i] = NULL;
        }
    }
    net_clear_pending(&network);
}

/* Tick all nodes and network */
static void tick_cluster(uint64_t ms) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i] && nodes[i]->running && !chaos_is_crashed(&chaos, i)) {
            /* Slow nodes tick at half speed */
            uint64_t effective_ms = chaos_is_slow(&chaos, i) ? ms / 2 : ms;
            raft_tick(nodes[i], effective_ms);
        }
    }
    net_tick(&network, ms, deliver_message, NULL);
}

/* Find current leader */
static int find_leader(void) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i] && nodes[i]->role == RAFT_LEADER &&
            !chaos_is_crashed(&chaos, i)) {
            return i;
        }
    }
    return -1;
}

/* Wait for a leader to be elected */
static int wait_for_leader(int max_ticks) {
    for (int t = 0; t < max_ticks; t++) {
        tick_cluster(10);
        int leader = find_leader();
        if (leader >= 0) {
            return leader;
        }
    }
    return -1;
}

/* Test 1: Cluster survives random crashes */
TEST(test_random_crashes) {
    create_cluster();

    chaos_config_t config = {
        .crash_rate = 0.01,
        .restart_rate = 0.0,  /* Manual restart */
        .slow_rate = 0.0,
        .partition_rate = 0.0,
        .heal_rate = 0.0,
        .min_crash_duration = 50,
        .max_crash_duration = 100,
    };
    chaos_configure(&chaos, &config);

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Run with chaos for a while */
    int leader_changes = 0;
    int last_leader = leader;

    for (int t = 0; t < 500; t++) {
        int32_t affected;
        chaos_event_t event = chaos_tick(&chaos, NUM_NODES, &affected);

        if (event == CHAOS_CRASH && affected >= 0) {
            net_isolate(&network, affected, NUM_NODES);
        } else if (event == CHAOS_RESTART && affected >= 0) {
            net_reconnect(&network, affected, NUM_NODES);
        }

        tick_cluster(10);

        int current_leader = find_leader();
        if (current_leader >= 0 && current_leader != last_leader) {
            leader_changes++;
            last_leader = current_leader;
        }
    }

    /* Should have had some leader changes due to crashes */
    /* But cluster should still be functional */
    int final_leader = wait_for_leader(200);
    assert(final_leader >= 0);

    destroy_cluster();
}

/* Test 2: Cluster survives message drops */
TEST(test_message_drops) {
    create_cluster();
    net_set_drop_rate(&network, 0.1);  /* 10% drop rate */

    int leader = wait_for_leader(200);  /* May take longer with drops */
    assert(leader >= 0);

    /* Run for a while with drops */
    for (int t = 0; t < 300; t++) {
        tick_cluster(10);
    }

    /* Should still have a leader */
    int final_leader = find_leader();
    assert(final_leader >= 0);

    destroy_cluster();
}

/* Test 3: Cluster survives high latency */
TEST(test_high_latency) {
    create_cluster();
    net_set_delay(&network, 50, 100);  /* 50-100ms delay */

    int leader = wait_for_leader(300);  /* May take longer with delays */
    assert(leader >= 0);

    /* Run for a while */
    for (int t = 0; t < 300; t++) {
        tick_cluster(10);
    }

    /* Should still have a leader */
    int final_leader = find_leader();
    assert(final_leader >= 0);

    destroy_cluster();
}

/* Test 4: Cluster survives slow nodes */
TEST(test_slow_nodes) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Make some nodes slow */
    chaos.slow[1] = true;
    chaos.slow[3] = true;

    /* Run for a while */
    for (int t = 0; t < 300; t++) {
        tick_cluster(10);
    }

    /* Should still have a leader */
    int final_leader = find_leader();
    assert(final_leader >= 0);

    destroy_cluster();
}

/* Test 5: Combined chaos */
TEST(test_combined_chaos) {
    create_cluster();

    chaos_config_t config = {
        .crash_rate = 0.005,
        .restart_rate = 0.0,
        .slow_rate = 0.002,
        .partition_rate = 0.001,
        .heal_rate = 0.01,
        .min_crash_duration = 30,
        .max_crash_duration = 80,
    };
    chaos_configure(&chaos, &config);
    net_set_drop_rate(&network, 0.05);
    net_set_delay(&network, 5, 20);

    int leader = wait_for_leader(200);
    assert(leader >= 0);

    /* Run with combined chaos */
    for (int t = 0; t < 500; t++) {
        int32_t affected;
        chaos_event_t event = chaos_tick(&chaos, NUM_NODES, &affected);

        if (event == CHAOS_CRASH && affected >= 0) {
            net_isolate(&network, affected, NUM_NODES);
        } else if (event == CHAOS_RESTART && affected >= 0) {
            net_reconnect(&network, affected, NUM_NODES);
        } else if (event == CHAOS_PARTITION && affected >= 0) {
            net_isolate(&network, affected, NUM_NODES);
        } else if (event == CHAOS_HEAL) {
            net_heal(&network, NUM_NODES);
        }

        tick_cluster(10);
    }

    /* Heal everything and wait for stability */
    net_heal(&network, NUM_NODES);
    for (int i = 0; i < NUM_NODES; i++) {
        chaos_restart_node(&chaos, i);
    }

    int final_leader = wait_for_leader(300);
    assert(final_leader >= 0);

    destroy_cluster();
}

int main(void) {
    printf("Integration Tests: Chaos Testing\n");
    printf("=================================\n\n");

    raft_timer_seed(42);
    srand(42);

    RUN_TEST(test_random_crashes);
    RUN_TEST(test_message_drops);
    RUN_TEST(test_high_latency);
    RUN_TEST(test_slow_nodes);
    RUN_TEST(test_combined_chaos);

    printf("\n=================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
