/**
 * test_phase6.c - Phase 6 tests for advanced Raft features
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
#include "../src/snapshot.h"
#include "../src/read.h"
#include "../src/transfer.h"
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

/* External reset functions */
extern void raft_read_reset(void);
extern void raft_transfer_reset(void);
extern void raft_snapshot_reset_callback(void);

/* Message capture for testing */
static int msg_count = 0;
static raft_msg_type_t last_msg_type = 0;
static int32_t last_msg_target = -1;

static void test_send(raft_node_t* n, int32_t peer, const void* msg,
                      size_t len, void* ud) {
    (void)n; (void)len; (void)ud;
    msg_count++;
    last_msg_type = *(const raft_msg_type_t*)msg;
    last_msg_target = peer;
}

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

/* Test 1: PreVote basic functionality */
TEST(test_prevote_basic) {
    msg_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .send_fn = test_send,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_reset_election_timer(node);

    /* Start pre-vote */
    raft_status_t status = raft_start_pre_vote(node);
    assert(status == RAFT_OK);

    /* Should be in pre-candidate state */
    assert(node->role == RAFT_PRE_CANDIDATE);

    /* Should have sent PreVote messages */
    assert(msg_count == 2);  /* To nodes 1 and 2 */
    assert(last_msg_type == RAFT_MSG_PRE_VOTE);

    /* Term should NOT have increased yet */
    assert(node->persistent.current_term == 0);

    raft_destroy(node);
}

/* Test 2: PreVote prevents disruption from partitioned node */
TEST(test_prevote_prevents_disruption) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .send_fn = test_send,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);
    node->persistent.current_term = 5;

    /* Set up election timer to simulate active leader */
    raft_reset_election_timer(node);
    node->election_timer_ms = 0;  /* Just reset, haven't timed out */

    /* Simulate receiving PreVote from partitioned node with higher term */
    raft_pre_vote_t request = {
        .type = RAFT_MSG_PRE_VOTE,
        .term = 10,  /* Higher term */
        .candidate_id = 1,
        .last_log_index = 0,
        .last_log_term = 0,
    };

    raft_pre_vote_response_t response;
    raft_handle_pre_vote(node, &request, &response);

    /* Should NOT step down - PreVote doesn't affect term */
    assert(node->role == RAFT_LEADER);
    assert(node->persistent.current_term == 5);

    /* Should reject because we have a leader (self) and haven't timed out */
    assert(response.vote_granted == false);

    raft_destroy(node);
}

/* Test 3: PreVote log check */
TEST(test_prevote_log_check) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);

    /* Add some log entries */
    raft_log_append(node->log, 2, "cmd1", 4, NULL);
    raft_log_append(node->log, 2, "cmd2", 4, NULL);

    /* Force election timeout */
    node->election_timer_ms = node->election_timeout_ms + 1;
    node->current_leader = -1;

    /* PreVote with outdated log should be rejected */
    raft_pre_vote_t request = {
        .type = RAFT_MSG_PRE_VOTE,
        .term = 3,
        .candidate_id = 1,
        .last_log_index = 1,  /* Behind us */
        .last_log_term = 1,   /* Lower term */
    };

    raft_pre_vote_response_t response;
    raft_handle_pre_vote(node, &request, &response);

    assert(response.vote_granted == false);

    /* PreVote with up-to-date log should be granted */
    request.last_log_index = 2;
    request.last_log_term = 2;
    raft_handle_pre_vote(node, &request, &response);

    assert(response.vote_granted == true);

    raft_destroy(node);
}

/* Test 4: ReadIndex basic functionality */
static int read_callback_count = 0;
static raft_status_t last_read_status = RAFT_OK;

static void test_read_callback(raft_node_t* n, void* ctx, raft_status_t status) {
    (void)n; (void)ctx;
    read_callback_count++;
    last_read_status = status;
}

TEST(test_read_index_basic) {
    raft_read_reset();
    read_callback_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,  /* Single node - immediate response */
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    /* Single node becomes leader automatically */
    assert(node->role == RAFT_LEADER);

    /* ReadIndex should complete immediately for single node */
    raft_status_t status = raft_read_index(node, test_read_callback, NULL);
    assert(status == RAFT_OK);
    assert(read_callback_count == 1);
    assert(last_read_status == RAFT_OK);

    raft_destroy(node);
    raft_read_reset();
}

/* Test 5: ReadIndex not leader */
TEST(test_read_index_not_leader) {
    raft_read_reset();
    read_callback_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    /* Node is follower */
    assert(node->role == RAFT_FOLLOWER);

    /* ReadIndex should fail */
    raft_status_t status = raft_read_index(node, test_read_callback, NULL);
    assert(status == RAFT_NOT_LEADER);
    assert(read_callback_count == 0);

    raft_destroy(node);
    raft_read_reset();
}

/* Test 6: ReadIndex leadership change */
TEST(test_read_index_leadership_change) {
    raft_read_reset();
    read_callback_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Queue a read request */
    raft_status_t status = raft_read_index(node, test_read_callback, NULL);
    assert(status == RAFT_OK);
    assert(raft_read_pending_count(node) == 1);

    /* Simulate leadership loss - cancel all reads */
    raft_read_cancel_all(node);

    assert(read_callback_count == 1);
    assert(last_read_status == RAFT_NOT_LEADER);
    assert(raft_read_pending_count(node) == 0);

    raft_destroy(node);
    raft_read_reset();
}

/* Test 7: Auto compaction trigger */
TEST(test_auto_compaction_trigger) {
    raft_snapshot_reset_callback();
    char* dir = make_test_dir();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .data_dir = dir,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);

    /* Check entries since snapshot */
    assert(raft_entries_since_snapshot(node) == 0);

    /* Add some entries */
    for (int i = 0; i < 10; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        raft_log_append(node->log, 1, cmd, strlen(cmd), NULL);
    }

    assert(raft_entries_since_snapshot(node) == 10);

    /* Without callback, compaction should not happen */
    raft_status_t status = raft_maybe_compact(node);
    assert(status == RAFT_OK);
    assert(raft_entries_since_snapshot(node) == 10);

    raft_destroy(node);
    remove_dir(dir);
    free(dir);
    raft_snapshot_reset_callback();
}

/* Test 8: Auto compaction with callback */
static raft_status_t test_snapshot_cb(raft_node_t* n, void** data,
                                       size_t* len, void* ud) {
    (void)n; (void)ud;
    *data = strdup("test state");
    *len = strlen("test state");
    return RAFT_OK;
}

TEST(test_auto_compaction_callback) {
    raft_snapshot_reset_callback();
    char* dir = make_test_dir();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .data_dir = dir,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);

    /* Set snapshot callback */
    raft_set_snapshot_callback(node, test_snapshot_cb, NULL);

    /* Add entries */
    for (int i = 0; i < 100; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        raft_log_append(node->log, 1, cmd, strlen(cmd), NULL);
    }

    /* Set last_applied to allow compaction */
    node->volatile_state.last_applied = 50;

    /* Verify entries count */
    assert(raft_entries_since_snapshot(node) == 100);

    raft_destroy(node);
    remove_dir(dir);
    free(dir);
    raft_snapshot_reset_callback();
}

/* Test 9: Leadership transfer basic */
TEST(test_transfer_basic) {
    raft_transfer_reset();
    msg_count = 0;

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .send_fn = test_send,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Set match_index for node 1 to be caught up */
    node->leader_state.match_index[1] = raft_log_last_index(node->log);

    /* Transfer to node 1 */
    raft_status_t status = raft_transfer_leadership(node, 1);
    assert(status == RAFT_OK);

    /* Should be in progress */
    assert(raft_transfer_in_progress(node) == true);
    assert(raft_transfer_target(node) == 1);

    /* Should have sent TimeoutNow (target is caught up) */
    assert(last_msg_type == RAFT_MSG_TIMEOUT_NOW);
    assert(last_msg_target == 1);

    raft_destroy(node);
    raft_transfer_reset();
}

/* Test 10: Leadership transfer abort */
TEST(test_transfer_abort) {
    raft_transfer_reset();

    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .send_fn = test_send,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    assert(node != NULL);

    raft_start(node);
    raft_become_leader(node);

    /* Start transfer to node 2 (not caught up) */
    node->leader_state.match_index[2] = 0;
    raft_log_append(node->log, 1, "cmd", 3, NULL);

    raft_status_t status = raft_transfer_leadership(node, 2);
    assert(status == RAFT_OK);
    assert(raft_transfer_in_progress(node) == true);

    /* Abort transfer */
    raft_transfer_abort(node);

    assert(raft_transfer_in_progress(node) == false);
    assert(raft_transfer_target(node) == -1);

    raft_destroy(node);
    raft_transfer_reset();
}

int main(void) {
    printf("Phase 6: Advanced Raft Features Tests\n");
    printf("======================================\n\n");

    RUN_TEST(test_prevote_basic);
    RUN_TEST(test_prevote_prevents_disruption);
    RUN_TEST(test_prevote_log_check);
    RUN_TEST(test_read_index_basic);
    RUN_TEST(test_read_index_not_leader);
    RUN_TEST(test_read_index_leadership_change);
    RUN_TEST(test_auto_compaction_trigger);
    RUN_TEST(test_auto_compaction_callback);
    RUN_TEST(test_transfer_basic);
    RUN_TEST(test_transfer_abort);

    printf("\n======================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}