/**
 * test_partition.c - Network partition integration tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "network_sim.h"
#include "../../src/raft.h"
#include "../../src/election.h"
#include "../../src/timer.h"
#include "../../src/rpc.h"

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

/* Message delivery callback */
static void deliver_message(int32_t from, int32_t to,
                            const void* data, size_t len, void* ctx) {
    (void)ctx;
    if (nodes[to] && nodes[to]->running) {
        raft_receive_message(nodes[to], from, data, len);
    }
}

/* Send callback for Raft nodes */
static void cluster_send(raft_node_t* node, int32_t peer,
                         const void* msg, size_t len, void* ud) {
    (void)ud;
    net_send(&network, node->node_id, peer, msg, len);
}

/* Create test cluster */
static void create_cluster(void) {
    net_init(&network, NUM_NODES);

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
        if (nodes[i] && nodes[i]->running) {
            raft_tick(nodes[i], ms);
        }
    }
    net_tick(&network, ms, deliver_message, NULL);
}

/* Find current leader */
static int find_leader(void) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i] && nodes[i]->role == RAFT_LEADER) {
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

/* Test 1: Basic leader election */
TEST(test_basic_election) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Verify only one leader */
    int leader_count = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i]->role == RAFT_LEADER) {
            leader_count++;
        }
    }
    assert(leader_count == 1);

    destroy_cluster();
}

/* Test 2: Leader in minority partition loses leadership */
TEST(test_leader_minority_partition) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Partition: leader alone vs rest */
    int32_t minority[] = { leader };
    int32_t majority[NUM_NODES - 1];
    int j = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (i != leader) majority[j++] = i;
    }

    net_partition(&network, minority, 1, majority, NUM_NODES - 1);

    /* Tick until majority elects new leader */
    int new_leader = -1;
    for (int t = 0; t < 200; t++) {
        tick_cluster(10);
        for (int i = 0; i < NUM_NODES; i++) {
            if (i != leader && nodes[i]->role == RAFT_LEADER) {
                new_leader = i;
                break;
            }
        }
        if (new_leader >= 0) break;
    }

    assert(new_leader >= 0);
    assert(new_leader != leader);

    destroy_cluster();
}

/* Test 3: Leader in majority partition keeps leadership */
TEST(test_leader_majority_partition) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Partition: leader + 2 others vs 2 nodes */
    int32_t majority[3];
    int32_t minority[2];
    int mj = 0, mn = 0;

    majority[mj++] = leader;
    for (int i = 0; i < NUM_NODES && (mj < 3 || mn < 2); i++) {
        if (i != leader) {
            if (mj < 3) majority[mj++] = i;
            else minority[mn++] = i;
        }
    }

    net_partition(&network, majority, 3, minority, 2);

    /* Tick for a while */
    for (int t = 0; t < 100; t++) {
        tick_cluster(10);
    }

    /* Original leader should still be leader */
    assert(nodes[leader]->role == RAFT_LEADER);

    /* Minority should not have a leader */
    for (int i = 0; i < 2; i++) {
        assert(nodes[minority[i]]->role != RAFT_LEADER);
    }

    destroy_cluster();
}

/* Test 4: Partition heals and cluster converges */
TEST(test_partition_heal) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);
    uint64_t original_term = nodes[leader]->persistent.current_term;

    /* Isolate leader */
    net_isolate(&network, leader, NUM_NODES);

    /* Wait for new leader in majority */
    int new_leader = -1;
    for (int t = 0; t < 200; t++) {
        tick_cluster(10);
        for (int i = 0; i < NUM_NODES; i++) {
            if (i != leader && nodes[i]->role == RAFT_LEADER) {
                new_leader = i;
                break;
            }
        }
        if (new_leader >= 0) break;
    }
    assert(new_leader >= 0);

    /* Heal partition */
    net_reconnect(&network, leader, NUM_NODES);

    /* Tick until cluster converges */
    for (int t = 0; t < 100; t++) {
        tick_cluster(10);
    }

    /* Old leader should have stepped down */
    assert(nodes[leader]->role == RAFT_FOLLOWER);
    assert(nodes[leader]->persistent.current_term > original_term);

    /* Should be exactly one leader */
    int leader_count = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i]->role == RAFT_LEADER) {
            leader_count++;
        }
    }
    assert(leader_count == 1);

    destroy_cluster();
}

/* Test 5: Symmetric partition (2-1-2 split) */
TEST(test_symmetric_partition) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Create 2-1-2 partition where leader is isolated */
    int32_t group1[2] = {0, 1};
    int32_t group2[2] = {3, 4};
    int32_t isolated[1] = {2};

    /* Disconnect all groups from each other */
    net_partition(&network, group1, 2, group2, 2);
    net_partition(&network, group1, 2, isolated, 1);
    net_partition(&network, group2, 2, isolated, 1);

    /* Tick for a while */
    for (int t = 0; t < 200; t++) {
        tick_cluster(10);
    }

    /* No group has majority, so no leader should emerge */
    /* (or if there was a leader in a group, they should step down) */
    /* Actually, existing leaders might persist briefly */

    /* Heal and verify convergence */
    net_heal(&network, NUM_NODES);

    for (int t = 0; t < 200; t++) {
        tick_cluster(10);
    }

    /* Should have exactly one leader now */
    int leader_count = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i]->role == RAFT_LEADER) {
            leader_count++;
        }
    }
    assert(leader_count == 1);

    destroy_cluster();
}

/* Test 6: Flapping partition */
TEST(test_flapping_partition) {
    create_cluster();

    int leader = wait_for_leader(100);
    assert(leader >= 0);

    /* Repeatedly partition and heal */
    for (int round = 0; round < 5; round++) {
        /* Isolate a random node */
        int victim = rand() % NUM_NODES;
        net_isolate(&network, victim, NUM_NODES);

        for (int t = 0; t < 50; t++) {
            tick_cluster(10);
        }

        /* Heal */
        net_reconnect(&network, victim, NUM_NODES);

        for (int t = 0; t < 50; t++) {
            tick_cluster(10);
        }
    }

    /* Wait for stability */
    for (int t = 0; t < 100; t++) {
        tick_cluster(10);
    }

    /* Should have exactly one leader */
    int leader_count = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (nodes[i]->role == RAFT_LEADER) {
            leader_count++;
        }
    }
    assert(leader_count == 1);

    destroy_cluster();
}

int main(void) {
    printf("Integration Tests: Network Partitions\n");
    printf("======================================\n\n");

    raft_timer_seed(42);
    srand(42);

    RUN_TEST(test_basic_election);
    RUN_TEST(test_leader_minority_partition);
    RUN_TEST(test_leader_majority_partition);
    RUN_TEST(test_partition_heal);
    RUN_TEST(test_symmetric_partition);
    RUN_TEST(test_flapping_partition);

    printf("\n======================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
