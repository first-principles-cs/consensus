/**
 * test_phase3.c - Phase 3 tests for Raft log replication
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/raft.h"
#include "../src/election.h"
#include "../src/timer.h"
#include "../src/replication.h"
#include "../src/commit.h"
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

/* Message buffer for capturing sent messages */
#define MAX_MESSAGES 100
typedef struct {
    int32_t to_node;
    char data[4096];
    size_t len;
} captured_msg_t;

static captured_msg_t messages[MAX_MESSAGES];
static int msg_count = 0;

static void clear_messages(void) {
    msg_count = 0;
}

static void capture_send(raft_node_t* node, int32_t peer_id, const void* msg,
                         size_t msg_len, void* user_data) {
    (void)node;
    (void)user_data;
    if (msg_count < MAX_MESSAGES && msg_len <= 4096) {
        messages[msg_count].to_node = peer_id;
        memcpy(messages[msg_count].data, msg, msg_len);
        messages[msg_count].len = msg_len;
        msg_count++;
    }
}

static raft_node_t* create_test_node(int32_t id, int32_t num_nodes) {
    raft_config_t config = {
        .node_id = id,
        .num_nodes = num_nodes,
        .apply_fn = NULL,
        .send_fn = capture_send,
        .user_data = NULL,
    };
    raft_node_t* node = raft_create(&config);
    if (node) {
        raft_start(node);
        raft_reset_election_timer(node);
    }
    return node;
}

static void make_leader(raft_node_t* node) {
    raft_start_election(node);
    raft_request_vote_response_t vote = {
        .type = RAFT_MSG_REQUEST_VOTE_RESPONSE,
        .term = node->persistent.current_term,
        .vote_granted = true,
    };
    for (int i = 0; i < node->num_nodes / 2; i++) {
        int peer = (node->node_id + 1 + i) % node->num_nodes;
        raft_handle_request_vote_response(node, peer, &vote);
    }
}

/* Test 1: Leader replicates to follower */
TEST(test_leader_replicates_to_follower) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    make_leader(node);
    assert(raft_get_role(node) == RAFT_LEADER);
    clear_messages();

    /* Propose a command */
    uint64_t index;
    raft_status_t status = raft_propose(node, "cmd1", 4, &index);
    assert(status == RAFT_OK);
    assert(index == 1);

    /* Should have sent AppendEntries to 2 peers */
    assert(msg_count == 2);

    /* Verify message contains the entry */
    raft_append_entries_t* ae = (raft_append_entries_t*)messages[0].data;
    assert(ae->type == RAFT_MSG_APPEND_ENTRIES);
    assert(ae->entries_count == 1);

    raft_destroy(node);
}

/* Test 2: Follower appends entries */
TEST(test_follower_appends_entries) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* leader = create_test_node(0, 3);
    raft_node_t* follower = create_test_node(1, 3);
    assert(leader != NULL && follower != NULL);

    make_leader(leader);
    clear_messages();

    /* Leader proposes command */
    uint64_t index;
    raft_propose(leader, "cmd1", 4, &index);

    /* Deliver AppendEntries to follower */
    raft_append_entries_response_t response;
    raft_handle_append_entries_with_log(follower, messages[0].data,
                                         messages[0].len, &response);

    assert(response.success == true);
    assert(response.match_index == 1);
    assert(raft_log_last_index(follower->log) == 1);

    raft_destroy(leader);
    raft_destroy(follower);
}

/* Test 3: Log consistency check passes */
TEST(test_log_consistency_check_pass) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* leader = create_test_node(0, 3);
    raft_node_t* follower = create_test_node(1, 3);

    make_leader(leader);

    /* Add entry to both logs */
    raft_log_append(leader->log, 1, "cmd1", 4, NULL);
    raft_log_append(follower->log, 1, "cmd1", 4, NULL);

    clear_messages();

    /* Leader proposes second command */
    raft_propose(leader, "cmd2", 4, NULL);

    /* Deliver to follower - should pass consistency check */
    raft_append_entries_response_t response;
    raft_handle_append_entries_with_log(follower, messages[0].data,
                                         messages[0].len, &response);

    assert(response.success == true);
    assert(raft_log_last_index(follower->log) == 2);

    raft_destroy(leader);
    raft_destroy(follower);
}

/* Test 4: Log consistency check fails */
TEST(test_log_consistency_check_fail) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* leader = create_test_node(0, 3);
    raft_node_t* follower = create_test_node(1, 3);

    make_leader(leader);

    /* Add entry to leader only */
    raft_log_append(leader->log, 1, "cmd1", 4, NULL);

    /* Update next_index to reflect the new entry */
    leader->leader_state.next_index[1] = 2;

    clear_messages();

    /* Leader proposes second command */
    raft_propose(leader, "cmd2", 4, NULL);

    /* Now next_index[1] = 2, so we send entry at index 2 with prev_log_index = 1
     * Follower doesn't have entry at index 1, so consistency check should fail */
    raft_append_entries_response_t response;
    raft_handle_append_entries_with_log(follower, messages[0].data,
                                         messages[0].len, &response);

    assert(response.success == false);

    raft_destroy(leader);
    raft_destroy(follower);
}

/* Test 5: Leader retries on mismatch */
TEST(test_leader_retries_on_mismatch) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    make_leader(node);

    /* Add some entries via propose (which updates next_index properly) */
    raft_propose(node, "cmd1", 4, NULL);
    raft_propose(node, "cmd2", 4, NULL);

    /* Update next_index to simulate that we've tried to replicate */
    node->leader_state.next_index[1] = 3;

    /* Simulate failed response */
    raft_append_entries_response_t response = {
        .type = RAFT_MSG_APPEND_ENTRIES_RESPONSE,
        .term = 1,
        .success = false,
        .match_index = 0,
    };
    raft_handle_append_entries_response(node, 1, &response);

    /* next_index should be decremented */
    assert(node->leader_state.next_index[1] == 2);

    raft_destroy(node);
}

/* Test 6: Commit index advances */
TEST(test_commit_index_advances) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    make_leader(node);

    /* Propose command */
    uint64_t index;
    raft_propose(node, "cmd1", 4, &index);
    assert(node->volatile_state.commit_index == 0);

    /* Simulate successful response from peer 1 */
    raft_append_entries_response_t response = {
        .type = RAFT_MSG_APPEND_ENTRIES_RESPONSE,
        .term = 1,
        .success = true,
        .match_index = 1,
    };
    raft_handle_append_entries_response(node, 1, &response);

    /* Commit index should advance (majority: self + peer 1) */
    assert(node->volatile_state.commit_index == 1);

    raft_destroy(node);
}

/* Test 7: Only commit current term entries */
TEST(test_only_commit_current_term) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    make_leader(node);

    /* Add entry from previous term */
    raft_log_append(node->log, 0, "old_cmd", 7, NULL);

    /* Simulate successful response */
    raft_append_entries_response_t response = {
        .type = RAFT_MSG_APPEND_ENTRIES_RESPONSE,
        .term = 1,
        .success = true,
        .match_index = 1,
    };
    raft_handle_append_entries_response(node, 1, &response);

    /* Should NOT commit entry from term 0 */
    assert(node->volatile_state.commit_index == 0);

    /* Add entry from current term */
    raft_propose(node, "new_cmd", 7, NULL);
    response.match_index = 2;
    raft_handle_append_entries_response(node, 1, &response);

    /* Now both entries should be committed */
    assert(node->volatile_state.commit_index == 2);

    raft_destroy(node);
}

/* Test 8: Follower updates commit index */
TEST(test_follower_updates_commit_index) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* leader = create_test_node(0, 3);
    raft_node_t* follower = create_test_node(1, 3);

    make_leader(leader);

    /* Propose and commit on leader */
    raft_propose(leader, "cmd1", 4, NULL);
    raft_append_entries_response_t resp = {
        .type = RAFT_MSG_APPEND_ENTRIES_RESPONSE,
        .term = 1,
        .success = true,
        .match_index = 1,
    };
    raft_handle_append_entries_response(leader, 2, &resp);
    assert(leader->volatile_state.commit_index == 1);

    clear_messages();

    /* Send heartbeat with updated commit index */
    raft_replicate_log(leader);

    /* Deliver to follower */
    raft_append_entries_response_t response;
    raft_handle_append_entries_with_log(follower, messages[0].data,
                                         messages[0].len, &response);

    /* Follower should update commit index */
    assert(follower->volatile_state.commit_index == 1);

    raft_destroy(leader);
    raft_destroy(follower);
}

/* Test 9: Three node replication */
TEST(test_three_node_replication) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* nodes[3];
    for (int i = 0; i < 3; i++) {
        nodes[i] = create_test_node(i, 3);
    }

    /* Node 0 becomes leader */
    make_leader(nodes[0]);
    clear_messages();

    /* Leader proposes command */
    raft_propose(nodes[0], "cmd1", 4, NULL);
    assert(msg_count == 2);

    /* Deliver to followers */
    for (int i = 0; i < msg_count; i++) {
        int32_t to = messages[i].to_node;
        raft_append_entries_response_t response;
        raft_handle_append_entries_with_log(nodes[to], messages[i].data,
                                             messages[i].len, &response);
        assert(response.success == true);

        /* Deliver response back to leader */
        raft_handle_append_entries_response(nodes[0], to, &response);
    }

    /* All nodes should have the entry */
    for (int i = 0; i < 3; i++) {
        assert(raft_log_last_index(nodes[i]->log) == 1);
    }

    /* Leader should have committed */
    assert(nodes[0]->volatile_state.commit_index == 1);

    for (int i = 0; i < 3; i++) {
        raft_destroy(nodes[i]);
    }
}

/* Test 10: Propose and commit */
TEST(test_propose_and_commit) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    make_leader(node);

    /* Propose multiple commands */
    for (int i = 0; i < 5; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        raft_propose(node, cmd, strlen(cmd), NULL);
    }

    assert(raft_log_last_index(node->log) == 5);

    /* Simulate responses for all entries */
    raft_append_entries_response_t response = {
        .type = RAFT_MSG_APPEND_ENTRIES_RESPONSE,
        .term = 1,
        .success = true,
        .match_index = 5,
    };
    raft_handle_append_entries_response(node, 1, &response);

    /* All entries should be committed */
    assert(node->volatile_state.commit_index == 5);

    raft_destroy(node);
}

int main(void) {
    printf("Phase 3: Log Replication Tests\n");
    printf("==============================\n\n");

    RUN_TEST(test_leader_replicates_to_follower);
    RUN_TEST(test_follower_appends_entries);
    RUN_TEST(test_log_consistency_check_pass);
    RUN_TEST(test_log_consistency_check_fail);
    RUN_TEST(test_leader_retries_on_mismatch);
    RUN_TEST(test_commit_index_advances);
    RUN_TEST(test_only_commit_current_term);
    RUN_TEST(test_follower_updates_commit_index);
    RUN_TEST(test_three_node_replication);
    RUN_TEST(test_propose_and_commit);

    printf("\n==============================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
