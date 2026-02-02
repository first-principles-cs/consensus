/**
 * bench_latency.c - Latency benchmark for Raft
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_common.h"
#include "../../src/raft.h"
#include "../../src/election.h"
#include "../../src/timer.h"
#include "../../src/log.h"

#define WARMUP_OPS 100
#define BENCH_OPS 1000

/* Benchmark election start latency */
static void bench_election_start(void) {
    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        raft_config_t config = {
            .node_id = 0,
            .num_nodes = 3,
            .data_dir = NULL,
        };

        raft_node_t* node = raft_create(&config);
        raft_start(node);
        raft_reset_election_timer(node);

        uint64_t start = bench_now_ns();
        raft_start_election(node);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);

        raft_destroy(node);
    }

    bench_print(&result, "Election Start");
    bench_free(&result);
}

/* Benchmark heartbeat send latency */
static void bench_heartbeat_send(void) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);
    raft_become_leader(node);

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        raft_send_heartbeats(node);
    }

    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        uint64_t start = bench_now_ns();
        raft_send_heartbeats(node);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
    }

    bench_print(&result, "Heartbeat Send (no network)");
    bench_free(&result);

    raft_destroy(node);
}

/* Benchmark vote request handling latency */
static void bench_vote_request(void) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);
    raft_reset_election_timer(node);

    raft_request_vote_t request = {
        .type = RAFT_MSG_REQUEST_VOTE,
        .term = 1,
        .candidate_id = 1,
        .last_log_index = 0,
        .last_log_term = 0,
    };

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        raft_request_vote_response_t response;
        raft_handle_request_vote(node, &request, &response);
        node->persistent.voted_for = -1;  /* Reset for next iteration */
    }

    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        raft_request_vote_response_t response;

        uint64_t start = bench_now_ns();
        raft_handle_request_vote(node, &request, &response);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
        node->persistent.voted_for = -1;  /* Reset */
    }

    bench_print(&result, "Vote Request Handling");
    bench_free(&result);

    raft_destroy(node);
}

/* Benchmark append entries handling latency */
static void bench_append_entries(void) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);
    raft_reset_election_timer(node);

    raft_append_entries_t request = {
        .type = RAFT_MSG_APPEND_ENTRIES,
        .term = 1,
        .leader_id = 1,
        .prev_log_index = 0,
        .prev_log_term = 0,
        .leader_commit = 0,
        .entries_count = 0,  /* Heartbeat */
    };

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        raft_append_entries_response_t response;
        raft_handle_append_entries(node, &request, &response);
    }

    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        raft_append_entries_response_t response;

        uint64_t start = bench_now_ns();
        raft_handle_append_entries(node, &request, &response);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
    }

    bench_print(&result, "Append Entries (Heartbeat)");
    bench_free(&result);

    raft_destroy(node);
}

/* Benchmark timer tick latency */
static void bench_timer_tick(void) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 3,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);
    raft_reset_election_timer(node);

    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        uint64_t start = bench_now_ns();
        raft_tick(node, 1);  /* 1ms tick */
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);

        /* Reset timer to prevent election */
        if (i % 100 == 0) {
            raft_reset_election_timer(node);
        }
    }

    bench_print(&result, "Timer Tick (1ms)");
    bench_free(&result);

    raft_destroy(node);
}

int main(void) {
    printf("Raft Latency Benchmarks\n");
    printf("=======================\n");

    raft_timer_seed(42);
    srand(42);

    bench_election_start();
    bench_heartbeat_send();
    bench_vote_request();
    bench_append_entries();
    bench_timer_tick();

    return 0;
}
