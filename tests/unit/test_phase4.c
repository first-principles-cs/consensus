/**
 * test_phase4.c - Phase 4 tests for Raft persistence and recovery
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
#include "../src/recovery.h"
#include "../src/crc32.h"
#include "../src/rpc.h"
#include "../src/param.h"

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

/* Test 1: Storage lifecycle - open and close */
TEST(test_storage_lifecycle) {
    char* dir = make_test_dir();

    raft_storage_t* storage = raft_storage_open(dir, true);
    assert(storage != NULL);
    assert(raft_storage_get_dir(storage) != NULL);
    assert(strcmp(raft_storage_get_dir(storage), dir) == 0);

    raft_storage_close(storage);
    remove_dir(dir);
    free(dir);
}

/* Test 2: Save and load state */
TEST(test_save_and_load_state) {
    char* dir = make_test_dir();

    raft_storage_t* storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    raft_status_t status = raft_storage_save_state(storage, 42, 3);
    assert(status == RAFT_OK);

    uint64_t term;
    int32_t voted_for;
    status = raft_storage_load_state(storage, &term, &voted_for);
    assert(status == RAFT_OK);
    assert(term == 42);
    assert(voted_for == 3);

    raft_storage_close(storage);
    remove_dir(dir);
    free(dir);
}

/* Test 3: Save and load log entries */
TEST(test_save_and_load_log) {
    char* dir = make_test_dir();

    raft_storage_t* storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    raft_entry_t entry1 = { .term = 1, .index = 1, .command = "cmd1", .command_len = 4 };
    raft_entry_t entry2 = { .term = 1, .index = 2, .command = "cmd2", .command_len = 4 };
    raft_entry_t entry3 = { .term = 2, .index = 3, .command = "cmd3", .command_len = 4 };

    assert(raft_storage_append_entry(storage, &entry1) == RAFT_OK);
    assert(raft_storage_append_entry(storage, &entry2) == RAFT_OK);
    assert(raft_storage_append_entry(storage, &entry3) == RAFT_OK);

    uint64_t base_index, base_term, count;
    assert(raft_storage_get_log_info(storage, &base_index, &base_term, &count) == RAFT_OK);
    assert(count == 3);

    raft_storage_close(storage);
    remove_dir(dir);
    free(dir);
}

/* Test 4: Recovery with empty state */
TEST(test_recovery_empty) {
    char* dir = make_test_dir();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .apply_fn = NULL,
        .send_fn = NULL,
        .user_data = NULL,
        .data_dir = dir,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);
    assert(node->storage != NULL);

    assert(node->persistent.current_term == 0);
    assert(node->persistent.voted_for == -1);
    assert(raft_log_count(node->log) == 0);

    raft_destroy(node);
    remove_dir(dir);
    free(dir);
}

/* Test 5: Recovery with saved state */
TEST(test_recovery_with_state) {
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
        raft_timer_seed(42);
        raft_reset_election_timer(node);
        raft_start_election(node);

        assert(node->persistent.current_term == 1);
        assert(node->persistent.voted_for == 0);

        raft_destroy(node);
    }

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

/* Test 6: CRC32 corruption detection */
TEST(test_corruption_detection) {
    char* dir = make_test_dir();

    raft_storage_t* storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    raft_storage_save_state(storage, 100, 5);
    raft_storage_close(storage);

    char path[256];
    snprintf(path, sizeof(path), "%s/raft_state.dat", dir);
    FILE* f = fopen(path, "r+b");
    assert(f != NULL);
    fseek(f, 12, SEEK_SET);
    uint64_t bad_term = 999;
    fwrite(&bad_term, sizeof(bad_term), 1, f);
    fclose(f);

    storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    uint64_t term;
    int32_t voted_for;
    raft_status_t status = raft_storage_load_state(storage, &term, &voted_for);
    assert(status == RAFT_CORRUPTION);

    raft_storage_close(storage);
    remove_dir(dir);
    free(dir);
}

/* Test 7: Truncated file handling */
TEST(test_truncated_file) {
    char* dir = make_test_dir();

    raft_storage_t* storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    raft_storage_save_state(storage, 50, 2);
    raft_storage_close(storage);

    char path[256];
    snprintf(path, sizeof(path), "%s/raft_state.dat", dir);
    truncate(path, 10);

    storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    uint64_t term;
    int32_t voted_for;
    raft_status_t status = raft_storage_load_state(storage, &term, &voted_for);
    assert(status == RAFT_IO_ERROR);

    raft_storage_close(storage);
    remove_dir(dir);
    free(dir);
}

/* Test 8: Multiple restarts */
TEST(test_multiple_restarts) {
    char* dir = make_test_dir();

    for (int i = 1; i <= 5; i++) {
        raft_config_t config = {
            .node_id = 0,
            .num_nodes = 3,
            .data_dir = dir,
        };

        raft_node_t* node = raft_create(&config);
        assert(node != NULL);

        assert(node->persistent.current_term == (uint64_t)(i - 1));

        raft_start(node);
        raft_timer_seed(42);
        raft_reset_election_timer(node);
        raft_start_election(node);

        assert(node->persistent.current_term == (uint64_t)i);

        raft_destroy(node);
    }

    remove_dir(dir);
    free(dir);
}

/* Test 9: Log truncation */
TEST(test_log_truncation) {
    char* dir = make_test_dir();

    raft_storage_t* storage = raft_storage_open(dir, true);
    assert(storage != NULL);

    raft_entry_t entry1 = { .term = 1, .index = 1, .command = "cmd1", .command_len = 4 };
    raft_entry_t entry2 = { .term = 1, .index = 2, .command = "cmd2", .command_len = 4 };
    raft_entry_t entry3 = { .term = 2, .index = 3, .command = "cmd3", .command_len = 4 };

    raft_storage_append_entry(storage, &entry1);
    raft_storage_append_entry(storage, &entry2);
    raft_storage_append_entry(storage, &entry3);

    uint64_t base_index, base_term, count;
    raft_storage_get_log_info(storage, &base_index, &base_term, &count);
    assert(count == 3);

    raft_storage_truncate_log(storage, 1);

    raft_storage_get_log_info(storage, &base_index, &base_term, &count);
    assert(count == 1);

    raft_storage_close(storage);
    remove_dir(dir);
    free(dir);
}

/* Test 10: Phase 3 regression - ensure previous functionality works */
TEST(test_phase3_regression) {
    raft_timer_seed(42);

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .apply_fn = NULL,
        .send_fn = NULL,
        .user_data = NULL,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_reset_election_timer(node);
    raft_start_election(node);

    assert(node->role == RAFT_CANDIDATE);
    assert(node->persistent.current_term == 1);

    raft_request_vote_response_t vote = {
        .type = RAFT_MSG_REQUEST_VOTE_RESPONSE,
        .term = 1,
        .vote_granted = true,
    };
    raft_handle_request_vote_response(node, 1, &vote);

    assert(node->role == RAFT_LEADER);

    uint64_t index;
    raft_status_t status = raft_propose(node, "test", 4, &index);
    assert(status == RAFT_OK);
    assert(index == 1);

    raft_destroy(node);
}

int main(void) {
    printf("Phase 4: Persistence and Recovery Tests\n");
    printf("========================================\n\n");

    RUN_TEST(test_storage_lifecycle);
    RUN_TEST(test_save_and_load_state);
    RUN_TEST(test_save_and_load_log);
    RUN_TEST(test_recovery_empty);
    RUN_TEST(test_recovery_with_state);
    RUN_TEST(test_corruption_detection);
    RUN_TEST(test_truncated_file);
    RUN_TEST(test_multiple_restarts);
    RUN_TEST(test_log_truncation);
    RUN_TEST(test_phase3_regression);

    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
