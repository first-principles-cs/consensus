/**
 * test_phase5.c - Phase 5 tests for membership changes and optimization
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/raft.h"
#include "../src/election.h"
#include "../src/timer.h"
#include "../src/storage.h"
#include "../src/snapshot.h"
#include "../src/membership.h"
#include "../src/batch.h"
#include "../src/rpc.h"
#include "../src/param.h"
#include "../src/log.h"

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

static int test_counter = 0;

/* External function for resetting membership state between tests */
extern void raft_membership_reset(void);

static char* make_test_dir(void) {
    char* dir = malloc(64);
    snprintf(dir, 64, "/tmp/raft_test_%d_%d", getpid(), test_counter++);
    mkdir(dir, 0755);
    return dir;
}

static void remove_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

/* Test 1: Snapshot create and load */
TEST(test_snapshot_create_load) {
    char* dir = make_test_dir();

    /* Create a snapshot */
    const char* state = "test state data";
    size_t state_len = strlen(state);

    raft_status_t status = raft_snapshot_create(dir, 10, 2, state, state_len);
    assert(status == RAFT_OK);

    /* Verify snapshot exists */
    assert(raft_snapshot_exists(dir) == true);

    /* Load snapshot metadata */
    raft_snapshot_meta_t meta;
    status = raft_snapshot_load_meta(dir, &meta);
    assert(status == RAFT_OK);
    assert(meta.last_index == 10);
    assert(meta.last_term == 2);

    /* Load full snapshot */
    void* loaded_state = NULL;
    size_t loaded_len = 0;
    status = raft_snapshot_load(dir, &meta, &loaded_state, &loaded_len);
    assert(status == RAFT_OK);
    assert(loaded_len == state_len);
    assert(memcmp(loaded_state, state, state_len) == 0);

    free(loaded_state);
    remove_dir(dir);
    free(dir);
}

/* Test 2: Snapshot compaction (log truncation after snapshot) */
TEST(test_snapshot_compaction) {
    char* dir = make_test_dir();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = dir,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Add some log entries */
    for (int i = 0; i < 5; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        uint64_t index;
        raft_propose(node, cmd, strlen(cmd), &index);
    }

    assert(raft_log_count(node->log) == 5);

    /* Create snapshot at index 3 */
    raft_snapshot_meta_t meta = { .last_index = 3, .last_term = 1 };
    raft_status_t status = raft_snapshot_install(node, &meta, "state", 5);
    assert(status == RAFT_OK);

    /* Log should be truncated - base_index should be 3 */
    assert(node->log->base_index == 3);

    raft_destroy(node);
    remove_dir(dir);
    free(dir);
}

/* Test 3: Add node to cluster */
TEST(test_add_node) {
    raft_membership_reset();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Initial cluster size */
    assert(raft_get_cluster_size(node) == 3);

    /* Add node 3 */
    raft_status_t status = raft_add_node(node, 3);
    assert(status == RAFT_OK);

    /* Config should be transitioning */
    assert(raft_get_config_type(node) == RAFT_CONFIG_TRANSITIONING);

    /* New node should be voting member (during transition) */
    assert(raft_is_voting_member(node, 3) == true);

    raft_destroy(node);
    raft_membership_reset();
}

/* Test 4: Remove node from cluster */
TEST(test_remove_node) {
    raft_membership_reset();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Node 2 should be a member */
    assert(raft_is_voting_member(node, 2) == true);

    /* Remove node 2 */
    raft_status_t status = raft_remove_node(node, 2);
    assert(status == RAFT_OK);

    /* Config should be transitioning */
    assert(raft_get_config_type(node) == RAFT_CONFIG_TRANSITIONING);

    raft_destroy(node);
    raft_membership_reset();
}

/* Test 5: Config change commit */
TEST(test_config_change_commit) {
    raft_membership_reset();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Add node 3 */
    raft_add_node(node, 3);

    /* Get the config entry */
    const raft_entry_t* entry = raft_log_get(node->log, 1);
    assert(entry != NULL);
    assert(entry->type == RAFT_ENTRY_CONFIG);

    /* Apply the config change */
    raft_apply_config_change(node, entry);

    /* Config should be stable now */
    assert(raft_get_config_type(node) == RAFT_CONFIG_STABLE);

    /* Cluster size should be 4 */
    assert(raft_get_cluster_size(node) == 4);

    raft_destroy(node);
    raft_membership_reset();
}

/* Test 6: Batch propose */
TEST(test_batch_propose) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Batch propose 5 commands */
    const char* commands[] = { "cmd1", "cmd2", "cmd3", "cmd4", "cmd5" };
    size_t lens[] = { 4, 4, 4, 4, 4 };

    uint64_t first_index;
    raft_status_t status = raft_propose_batch(node, commands, lens, 5, &first_index);
    assert(status == RAFT_OK);
    assert(first_index == 1);

    /* Log should have 5 entries */
    assert(raft_log_count(node->log) == 5);

    /* Verify entries */
    for (int i = 1; i <= 5; i++) {
        const raft_entry_t* entry = raft_log_get(node->log, (uint64_t)i);
        assert(entry != NULL);
        assert(entry->index == (uint64_t)i);
    }

    raft_destroy(node);
}

/* Test 7: Batch apply */
static int batch_apply_count = 0;

static void batch_test_apply(raft_node_t* n, const raft_entry_t* e, void* ud) {
    (void)n; (void)e; (void)ud;
    batch_apply_count++;
}

TEST(test_batch_apply) {
    batch_apply_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .apply_fn = batch_test_apply,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Add some entries */
    const char* commands[] = { "cmd1", "cmd2", "cmd3", "cmd4", "cmd5" };
    size_t lens[] = { 4, 4, 4, 4, 4 };
    uint64_t first_index;
    raft_propose_batch(node, commands, lens, 5, &first_index);

    /* Set commit index */
    node->volatile_state.commit_index = 5;

    /* Check pending count */
    assert(raft_pending_apply_count(node) == 5);

    /* Apply in batch of 3 */
    size_t applied = raft_apply_batch(node, 3);
    assert(applied == 3);
    assert(batch_apply_count == 3);
    assert(node->volatile_state.last_applied == 3);

    /* Apply remaining */
    applied = raft_apply_batch(node, 0);
    assert(applied == 2);
    assert(batch_apply_count == 5);
    assert(node->volatile_state.last_applied == 5);

    raft_destroy(node);
}

/* Test 8: Install snapshot on lagging node */
TEST(test_install_snapshot) {
    char* dir = make_test_dir();

    raft_config_t config = {
        .node_id = 1,
        .num_nodes = 3,
        .data_dir = dir,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);

    /* Add some entries */
    raft_log_append(node->log, 1, "cmd1", 4, NULL);
    raft_log_append(node->log, 1, "cmd2", 4, NULL);
    assert(raft_log_count(node->log) == 2);

    /* Install snapshot from leader at index 10 */
    raft_snapshot_meta_t meta = { .last_index = 10, .last_term = 3 };
    const char* state = "leader state";
    raft_status_t status = raft_snapshot_install(node, &meta, state, strlen(state));
    assert(status == RAFT_OK);

    /* Log should be cleared, base set to snapshot */
    assert(raft_log_count(node->log) == 0);
    assert(node->log->base_index == 10);
    assert(node->log->base_term == 3);

    /* Commit and applied should be updated */
    assert(node->volatile_state.commit_index == 10);
    assert(node->volatile_state.last_applied == 10);

    /* Snapshot should be saved */
    assert(raft_snapshot_exists(dir) == true);

    raft_destroy(node);
    remove_dir(dir);
    free(dir);
}

/* Test 9: Membership persistence */
TEST(test_membership_persistence) {
    raft_membership_reset();
    char* dir = make_test_dir();

    {
        raft_config_t config = {
            .node_id = 0,
            .num_nodes = 3,
            .data_dir = dir,
        };

        raft_node_t* node = raft_create(&config);
        assert(node != NULL);

        raft_start(node);
        raft_become_leader(node);

        /* Add node 3 - this creates a config entry in the log */
        raft_status_t status = raft_add_node(node, 3);
        assert(status == RAFT_OK);

        /* Verify entry was persisted */
        uint64_t base_index, base_term, count;
        raft_storage_get_log_info(node->storage, &base_index, &base_term, &count);
        assert(count == 1);

        raft_destroy(node);
    }

    /* Verify log entry persisted */
    {
        raft_storage_t* storage = raft_storage_open(dir, true);
        assert(storage != NULL);

        uint64_t base_index, base_term, count;
        raft_storage_get_log_info(storage, &base_index, &base_term, &count);
        assert(count == 1);

        raft_storage_close(storage);
    }

    remove_dir(dir);
    free(dir);
    raft_membership_reset();
}

/* Test 10: Phase 4 regression - ensure previous functionality works */
TEST(test_phase4_regression) {
    char* dir = make_test_dir();
    raft_timer_seed(42);

    /* Test persistence still works */
    {
        raft_config_t config = {
            .node_id = 0,
            .num_nodes = 3,
            .data_dir = dir,
        };

        raft_node_t* node = raft_create(&config);
        assert(node != NULL);

        raft_start(node);
        raft_reset_election_timer(node);
        raft_start_election(node);

        assert(node->persistent.current_term == 1);
        assert(node->persistent.voted_for == 0);

        raft_destroy(node);
    }

    /* Verify state was persisted */
    {
        raft_config_t config = {
            .node_id = 0,
            .num_nodes = 3,
            .data_dir = dir,
        };

        raft_node_t* node = raft_create(&config);
        assert(node != NULL);

        assert(node->persistent.current_term == 1);
        assert(node->persistent.voted_for == 0);

        raft_destroy(node);
    }

    remove_dir(dir);
    free(dir);
}

int main(void) {
    printf("Phase 5: Membership Changes and Optimization Tests\n");
    printf("===================================================\n\n");

    RUN_TEST(test_snapshot_create_load);
    RUN_TEST(test_snapshot_compaction);
    RUN_TEST(test_add_node);
    RUN_TEST(test_remove_node);
    RUN_TEST(test_config_change_commit);
    RUN_TEST(test_batch_propose);
    RUN_TEST(test_batch_apply);
    RUN_TEST(test_install_snapshot);
    RUN_TEST(test_membership_persistence);
    RUN_TEST(test_phase4_regression);

    printf("\n===================================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
