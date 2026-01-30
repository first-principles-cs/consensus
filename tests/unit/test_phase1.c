/**
 * test_phase1.c - Phase 1 unit tests
 *
 * Tests for log management and basic Raft node operations.
 */

#include "../../src/raft.h"
#include "../../src/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

/* ========== Log Tests ========== */

TEST(test_log_create_destroy) {
    raft_log_t* log = raft_log_create();
    assert(log != NULL);
    assert(raft_log_count(log) == 0);
    assert(raft_log_last_index(log) == 0);
    assert(raft_log_last_term(log) == 0);
    raft_log_destroy(log);
}

TEST(test_log_append_get) {
    raft_log_t* log = raft_log_create();
    uint64_t index;

    /* Append first entry */
    raft_status_t status = raft_log_append(log, 1, "cmd1", 4, &index);
    assert(status == RAFT_OK);
    assert(index == 1);
    assert(raft_log_count(log) == 1);
    assert(raft_log_last_index(log) == 1);
    assert(raft_log_last_term(log) == 1);

    /* Verify entry */
    const raft_entry_t* entry = raft_log_get(log, 1);
    assert(entry != NULL);
    assert(entry->term == 1);
    assert(entry->index == 1);
    assert(entry->command_len == 4);
    assert(memcmp(entry->command, "cmd1", 4) == 0);

    /* Append more entries */
    raft_log_append(log, 1, "cmd2", 4, &index);
    assert(index == 2);
    raft_log_append(log, 2, "cmd3", 4, &index);
    assert(index == 3);
    assert(raft_log_count(log) == 3);
    assert(raft_log_last_index(log) == 3);
    assert(raft_log_last_term(log) == 2);

    /* Get by index */
    entry = raft_log_get(log, 2);
    assert(entry != NULL);
    assert(entry->term == 1);
    assert(memcmp(entry->command, "cmd2", 4) == 0);

    /* Out of range returns NULL */
    assert(raft_log_get(log, 0) == NULL);
    assert(raft_log_get(log, 4) == NULL);

    raft_log_destroy(log);
}

TEST(test_log_truncate_after) {
    raft_log_t* log = raft_log_create();
    uint64_t index;

    /* Add 5 entries */
    for (int i = 1; i <= 5; i++) {
        raft_log_append(log, 1, "cmd", 3, &index);
    }
    assert(raft_log_count(log) == 5);

    /* Truncate after index 3 */
    raft_status_t status = raft_log_truncate_after(log, 3);
    assert(status == RAFT_OK);
    assert(raft_log_count(log) == 3);
    assert(raft_log_last_index(log) == 3);

    /* Entries 4 and 5 should be gone */
    assert(raft_log_get(log, 3) != NULL);
    assert(raft_log_get(log, 4) == NULL);

    raft_log_destroy(log);
}

TEST(test_log_truncate_before) {
    raft_log_t* log = raft_log_create();
    uint64_t index;

    /* Add 5 entries with different terms */
    raft_log_append(log, 1, "cmd1", 4, &index);
    raft_log_append(log, 1, "cmd2", 4, &index);
    raft_log_append(log, 2, "cmd3", 4, &index);
    raft_log_append(log, 2, "cmd4", 4, &index);
    raft_log_append(log, 3, "cmd5", 4, &index);
    assert(raft_log_count(log) == 5);

    /* Truncate before index 3 (keep 3, 4, 5) */
    raft_status_t status = raft_log_truncate_before(log, 3);
    assert(status == RAFT_OK);
    assert(raft_log_count(log) == 3);
    assert(raft_log_last_index(log) == 5);

    /* Entries 1 and 2 should be gone */
    assert(raft_log_get(log, 1) == NULL);
    assert(raft_log_get(log, 2) == NULL);
    assert(raft_log_get(log, 3) != NULL);

    raft_log_destroy(log);
}

TEST(test_log_term_at) {
    raft_log_t* log = raft_log_create();
    uint64_t index;

    raft_log_append(log, 1, "cmd1", 4, &index);
    raft_log_append(log, 2, "cmd2", 4, &index);
    raft_log_append(log, 2, "cmd3", 4, &index);

    assert(raft_log_term_at(log, 1) == 1);
    assert(raft_log_term_at(log, 2) == 2);
    assert(raft_log_term_at(log, 3) == 2);
    assert(raft_log_term_at(log, 4) == 0);  /* Out of range */

    raft_log_destroy(log);
}

/* ========== Raft Node Tests ========== */

TEST(test_node_create_destroy) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .apply_fn = NULL,
        .send_fn = NULL,
        .user_data = NULL
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);
    assert(raft_get_role(node) == RAFT_FOLLOWER);
    assert(raft_get_term(node) == 0);
    assert(raft_get_leader(node) == -1);
    assert(!raft_is_leader(node));

    raft_destroy(node);
}

TEST(test_node_start_stop) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .apply_fn = NULL,
        .send_fn = NULL,
        .user_data = NULL
    };

    raft_node_t* node = raft_create(&config);

    /* Start node - single node becomes leader */
    raft_status_t status = raft_start(node);
    assert(status == RAFT_OK);
    assert(raft_is_leader(node));
    assert(raft_get_role(node) == RAFT_LEADER);
    assert(raft_get_leader(node) == 0);

    /* Stop node */
    status = raft_stop(node);
    assert(status == RAFT_OK);

    raft_destroy(node);
}

/* Track applied entries for testing */
static int applied_count = 0;
static char applied_commands[10][16];

static void test_apply_fn(raft_node_t* node, const raft_entry_t* entry,
                          void* user_data) {
    (void)node;
    (void)user_data;
    if (applied_count < 10 && entry->command_len < 16) {
        memcpy(applied_commands[applied_count], entry->command, entry->command_len);
        applied_commands[applied_count][entry->command_len] = '\0';
        applied_count++;
    }
}

TEST(test_single_node_propose) {
    applied_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .apply_fn = test_apply_fn,
        .send_fn = NULL,
        .user_data = NULL
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);

    /* Propose commands */
    uint64_t index;
    raft_status_t status = raft_propose(node, "set x 1", 7, &index);
    assert(status == RAFT_OK);
    assert(index == 1);

    status = raft_propose(node, "set y 2", 7, &index);
    assert(status == RAFT_OK);
    assert(index == 2);

    /* For single node, entries are committed immediately */
    /* Update commit index manually for single node */
    raft_log_t* log = raft_get_log(node);
    assert(raft_log_count(log) == 2);

    /* Apply committed entries */
    raft_apply_committed(node);
    assert(applied_count == 2);
    assert(strcmp(applied_commands[0], "set x 1") == 0);
    assert(strcmp(applied_commands[1], "set y 2") == 0);

    raft_destroy(node);
}

TEST(test_propose_not_leader) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,  /* Multi-node cluster */
        .apply_fn = NULL,
        .send_fn = NULL,
        .user_data = NULL
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);

    /* Node starts as follower in multi-node cluster */
    assert(!raft_is_leader(node));

    /* Propose should fail */
    uint64_t index;
    raft_status_t status = raft_propose(node, "cmd", 3, &index);
    assert(status == RAFT_NOT_LEADER);

    raft_destroy(node);
}

TEST(test_invalid_config) {
    /* NULL config */
    assert(raft_create(NULL) == NULL);

    /* Invalid node_id */
    raft_config_t config = { .node_id = -1, .num_nodes = 1 };
    assert(raft_create(&config) == NULL);

    /* Invalid num_nodes */
    config.node_id = 0;
    config.num_nodes = 0;
    assert(raft_create(&config) == NULL);
}

/* ========== Main ========== */

int main(void) {
    printf("Phase 1 Tests: Log and Basic Raft Node\n");
    printf("=======================================\n\n");

    printf("Log Tests:\n");
    RUN_TEST(test_log_create_destroy);
    RUN_TEST(test_log_append_get);
    RUN_TEST(test_log_truncate_after);
    RUN_TEST(test_log_truncate_before);
    RUN_TEST(test_log_term_at);

    printf("\nRaft Node Tests:\n");
    RUN_TEST(test_node_create_destroy);
    RUN_TEST(test_node_start_stop);
    RUN_TEST(test_single_node_propose);
    RUN_TEST(test_propose_not_leader);
    RUN_TEST(test_invalid_config);

    printf("\n=======================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
