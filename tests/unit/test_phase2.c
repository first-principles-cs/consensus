/**
 * test_phase2.c - Phase 2 tests for Raft leader election
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/raft.h"
#include "../src/election.h"
#include "../src/timer.h"
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
    char data[256];
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
    if (msg_count < MAX_MESSAGES && msg_len <= 256) {
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

/* Test 1: Follower timeout becomes candidate */
TEST(test_follower_timeout_becomes_candidate) {
    raft_timer_seed(42);
    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);
    assert(raft_get_role(node) == RAFT_FOLLOWER);

    /* Tick past election timeout */
    uint64_t timeout = node->election_timeout_ms;
    raft_tick(node, timeout + 1);

    assert(raft_get_role(node) == RAFT_CANDIDATE);
    assert(raft_get_term(node) == 1);
    assert(node->persistent.voted_for == 0);

    raft_destroy(node);
}

/* Test 2: Candidate wins election with majority */
TEST(test_candidate_wins_election) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* Start election */
    raft_start_election(node);
    assert(raft_get_role(node) == RAFT_CANDIDATE);
    assert(msg_count == 2);  /* Sent RequestVote to 2 peers */

    /* Receive vote from node 1 */
    raft_request_vote_response_t response = {
        .type = RAFT_MSG_REQUEST_VOTE_RESPONSE,
        .term = 1,
        .vote_granted = true,
    };
    raft_handle_request_vote_response(node, 1, &response);

    /* Should now be leader (2 votes out of 3) */
    assert(raft_get_role(node) == RAFT_LEADER);

    raft_destroy(node);
}

/* Test 3: Step down on higher term */
TEST(test_step_down_on_higher_term) {
    raft_timer_seed(42);
    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* Become leader */
    raft_start_election(node);
    raft_request_vote_response_t vote = {
        .type = RAFT_MSG_REQUEST_VOTE_RESPONSE,
        .term = 1,
        .vote_granted = true,
    };
    raft_handle_request_vote_response(node, 1, &vote);
    assert(raft_get_role(node) == RAFT_LEADER);

    /* Receive message with higher term */
    raft_request_vote_t request = {
        .type = RAFT_MSG_REQUEST_VOTE,
        .term = 5,
        .candidate_id = 2,
        .last_log_index = 0,
        .last_log_term = 0,
    };
    raft_request_vote_response_t response;
    raft_handle_request_vote(node, &request, &response);

    /* Should step down to follower */
    assert(raft_get_role(node) == RAFT_FOLLOWER);
    assert(raft_get_term(node) == 5);

    raft_destroy(node);
}

/* Test 4: Vote only once per term */
TEST(test_vote_only_once_per_term) {
    raft_timer_seed(42);
    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* First vote request */
    raft_request_vote_t request1 = {
        .type = RAFT_MSG_REQUEST_VOTE,
        .term = 1,
        .candidate_id = 1,
        .last_log_index = 0,
        .last_log_term = 0,
    };
    raft_request_vote_response_t response1;
    raft_handle_request_vote(node, &request1, &response1);
    assert(response1.vote_granted == true);
    assert(node->persistent.voted_for == 1);

    /* Second vote request from different candidate, same term */
    raft_request_vote_t request2 = {
        .type = RAFT_MSG_REQUEST_VOTE,
        .term = 1,
        .candidate_id = 2,
        .last_log_index = 0,
        .last_log_term = 0,
    };
    raft_request_vote_response_t response2;
    raft_handle_request_vote(node, &request2, &response2);
    assert(response2.vote_granted == false);

    raft_destroy(node);
}

/* Test 5: Reject stale log candidate */
TEST(test_reject_stale_log_candidate) {
    raft_timer_seed(42);
    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* Add some log entries */
    raft_log_append(node->log, 1, "cmd1", 4, NULL);
    raft_log_append(node->log, 2, "cmd2", 4, NULL);

    /* Request from candidate with stale log */
    raft_request_vote_t request = {
        .type = RAFT_MSG_REQUEST_VOTE,
        .term = 3,
        .candidate_id = 1,
        .last_log_index = 1,
        .last_log_term = 1,  /* Our last term is 2 */
    };
    raft_request_vote_response_t response;
    raft_handle_request_vote(node, &request, &response);

    /* Should reject - our log is more up-to-date */
    assert(response.vote_granted == false);

    raft_destroy(node);
}

/* Test 6: Split vote leads to new election */
TEST(test_split_vote_new_election) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 5);
    assert(node != NULL);

    /* Start election */
    raft_start_election(node);
    assert(raft_get_role(node) == RAFT_CANDIDATE);
    uint64_t term1 = raft_get_term(node);

    /* Receive only 1 vote (not majority of 5) */
    raft_request_vote_response_t vote = {
        .type = RAFT_MSG_REQUEST_VOTE_RESPONSE,
        .term = term1,
        .vote_granted = true,
    };
    raft_handle_request_vote_response(node, 1, &vote);
    assert(raft_get_role(node) == RAFT_CANDIDATE);  /* Still candidate */

    /* Election timeout - start new election */
    raft_tick(node, node->election_timeout_ms + 1);
    assert(raft_get_term(node) == term1 + 1);  /* New term */

    raft_destroy(node);
}

/* Test 7: Leader sends heartbeats */
TEST(test_leader_sends_heartbeats) {
    raft_timer_seed(42);
    clear_messages();

    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* Become leader */
    raft_start_election(node);
    raft_request_vote_response_t vote = {
        .type = RAFT_MSG_REQUEST_VOTE_RESPONSE,
        .term = 1,
        .vote_granted = true,
    };
    raft_handle_request_vote_response(node, 1, &vote);
    assert(raft_get_role(node) == RAFT_LEADER);

    clear_messages();

    /* Send heartbeats */
    raft_send_heartbeats(node);
    assert(msg_count == 2);  /* Sent to 2 peers */

    /* Verify heartbeat content */
    raft_append_entries_t* hb = (raft_append_entries_t*)messages[0].data;
    assert(hb->type == RAFT_MSG_APPEND_ENTRIES);
    assert(hb->entries_count == 0);
    assert(hb->leader_id == 0);

    raft_destroy(node);
}

/* Test 8: Follower resets timer on heartbeat */
TEST(test_follower_resets_timer_on_heartbeat) {
    raft_timer_seed(42);
    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* Advance timer partway */
    raft_tick(node, 100);
    assert(node->election_timer_ms == 100);

    /* Receive heartbeat */
    raft_append_entries_t heartbeat = {
        .type = RAFT_MSG_APPEND_ENTRIES,
        .term = 1,
        .leader_id = 1,
        .prev_log_index = 0,
        .prev_log_term = 0,
        .leader_commit = 0,
        .entries_count = 0,
    };
    raft_append_entries_response_t response;
    raft_handle_append_entries(node, &heartbeat, &response);

    /* Timer should be reset */
    assert(node->election_timer_ms == 0);
    assert(response.success == true);
    assert(node->current_leader == 1);

    raft_destroy(node);
}

/* Test 9: Candidate steps down on AppendEntries */
TEST(test_candidate_steps_down_on_append_entries) {
    raft_timer_seed(42);
    raft_node_t* node = create_test_node(0, 3);
    assert(node != NULL);

    /* Start election */
    raft_start_election(node);
    assert(raft_get_role(node) == RAFT_CANDIDATE);

    /* Receive AppendEntries from leader with same term */
    raft_append_entries_t ae = {
        .type = RAFT_MSG_APPEND_ENTRIES,
        .term = 1,
        .leader_id = 2,
        .prev_log_index = 0,
        .prev_log_term = 0,
        .leader_commit = 0,
        .entries_count = 0,
    };
    raft_append_entries_response_t response;
    raft_handle_append_entries(node, &ae, &response);

    /* Should step down to follower */
    assert(raft_get_role(node) == RAFT_FOLLOWER);
    assert(node->current_leader == 2);

    raft_destroy(node);
}

/* Test 10: Three node election complete flow */
TEST(test_three_node_election) {
    raft_timer_seed(42);
    clear_messages();

    /* Create 3 nodes */
    raft_node_t* nodes[3];
    for (int i = 0; i < 3; i++) {
        nodes[i] = create_test_node(i, 3);
        assert(nodes[i] != NULL);
    }

    /* Node 0 times out first and starts election */
    raft_start_election(nodes[0]);
    assert(raft_get_role(nodes[0]) == RAFT_CANDIDATE);
    assert(raft_get_term(nodes[0]) == 1);

    /* Deliver RequestVote to nodes 1 and 2 */
    for (int i = 0; i < msg_count; i++) {
        int32_t to = messages[i].to_node;
        raft_request_vote_t* rv = (raft_request_vote_t*)messages[i].data;
        raft_request_vote_response_t resp;
        raft_handle_request_vote(nodes[to], rv, &resp);

        /* Both should grant vote */
        assert(resp.vote_granted == true);

        /* Deliver response back to node 0 */
        raft_handle_request_vote_response(nodes[0], to, &resp);
    }

    /* Node 0 should now be leader */
    assert(raft_get_role(nodes[0]) == RAFT_LEADER);

    /* Clean up */
    for (int i = 0; i < 3; i++) {
        raft_destroy(nodes[i]);
    }
}

int main(void) {
    printf("Phase 2: Leader Election Tests\n");
    printf("==============================\n\n");

    RUN_TEST(test_follower_timeout_becomes_candidate);
    RUN_TEST(test_candidate_wins_election);
    RUN_TEST(test_step_down_on_higher_term);
    RUN_TEST(test_vote_only_once_per_term);
    RUN_TEST(test_reject_stale_log_candidate);
    RUN_TEST(test_split_vote_new_election);
    RUN_TEST(test_leader_sends_heartbeats);
    RUN_TEST(test_follower_resets_timer_on_heartbeat);
    RUN_TEST(test_candidate_steps_down_on_append_entries);
    RUN_TEST(test_three_node_election);

    printf("\n==============================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
