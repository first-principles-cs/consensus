/**
 * bench_throughput.c - Throughput benchmark for Raft
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_common.h"
#include "../../src/raft.h"
#include "../../src/log.h"
#include "../../src/batch.h"

#define WARMUP_OPS 1000
#define BENCH_OPS 10000

/* Benchmark single-node propose throughput */
static void bench_single_node_propose(void) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "warmup%d", i);
        uint64_t idx;
        raft_propose(node, cmd, strlen(cmd), &idx);
    }

    /* Benchmark */
    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "bench%d", i);

        uint64_t start = bench_now_ns();
        uint64_t idx;
        raft_propose(node, cmd, strlen(cmd), &idx);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
    }

    bench_print(&result, "Single-Node Propose");
    bench_free(&result);

    raft_destroy(node);
}

/* Benchmark log append throughput */
static void bench_log_append(void) {
    raft_log_t* log = raft_log_create();

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "warmup%d", i);
        uint64_t idx;
        raft_log_append(log, 1, cmd, strlen(cmd), &idx);
    }

    /* Benchmark */
    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "bench%d", i);

        uint64_t start = bench_now_ns();
        uint64_t idx;
        raft_log_append(log, 1, cmd, strlen(cmd), &idx);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
    }

    bench_print(&result, "Log Append");
    bench_free(&result);

    raft_log_destroy(log);
}

/* Benchmark log get throughput */
static void bench_log_get(void) {
    raft_log_t* log = raft_log_create();

    /* Populate log */
    for (int i = 0; i < BENCH_OPS; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "entry%d", i);
        uint64_t idx;
        raft_log_append(log, 1, cmd, strlen(cmd), &idx);
    }

    /* Benchmark random access */
    bench_result_t result;
    bench_init(&result, BENCH_OPS);

    for (int i = 0; i < BENCH_OPS; i++) {
        uint64_t idx = (rand() % BENCH_OPS) + 1;

        uint64_t start = bench_now_ns();
        const raft_entry_t* entry = raft_log_get(log, idx);
        (void)entry;
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
    }

    bench_print(&result, "Log Get (Random)");
    bench_free(&result);

    raft_log_destroy(log);
}

/* Benchmark batch propose */
static void bench_batch_propose(void) {
    raft_config_t config = {
        .node_id = 0,
        .num_nodes = 1,
        .data_dir = NULL,
    };

    raft_node_t* node = raft_create(&config);
    raft_start(node);

    /* Prepare batch */
    #define BATCH_SIZE 100
    const char* commands[BATCH_SIZE];
    size_t lens[BATCH_SIZE];
    char cmd_data[BATCH_SIZE][32];

    for (int i = 0; i < BATCH_SIZE; i++) {
        snprintf(cmd_data[i], sizeof(cmd_data[i]), "batch%d", i);
        commands[i] = cmd_data[i];
        lens[i] = strlen(cmd_data[i]);
    }

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS / BATCH_SIZE; i++) {
        uint64_t first_idx;
        raft_propose_batch(node, commands, lens, BATCH_SIZE, &first_idx);
    }

    /* Benchmark */
    bench_result_t result;
    bench_init(&result, BENCH_OPS / BATCH_SIZE);

    for (int i = 0; i < BENCH_OPS / BATCH_SIZE; i++) {
        uint64_t start = bench_now_ns();
        uint64_t first_idx;
        raft_propose_batch(node, commands, lens, BATCH_SIZE, &first_idx);
        uint64_t end = bench_now_ns();

        bench_record(&result, end - start);
    }

    /* Adjust for batch size */
    result.total_ops *= BATCH_SIZE;

    bench_print(&result, "Batch Propose (100 entries/batch)");
    bench_free(&result);

    raft_destroy(node);
}

int main(void) {
    printf("Raft Throughput Benchmarks\n");
    printf("==========================\n");

    srand(42);

    bench_single_node_propose();
    bench_log_append();
    bench_log_get();
    bench_batch_propose();

    return 0;
}
