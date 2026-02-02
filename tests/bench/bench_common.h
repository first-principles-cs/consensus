/**
 * bench_common.h - Common benchmark utilities
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Get current time in nanoseconds */
static inline uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Get current time in microseconds */
static inline uint64_t bench_now_us(void) {
    return bench_now_ns() / 1000;
}

/* Get current time in milliseconds */
static inline uint64_t bench_now_ms(void) {
    return bench_now_ns() / 1000000;
}

/* Benchmark result */
typedef struct {
    uint64_t total_ops;
    uint64_t total_time_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t* latencies;  /* Array of individual latencies */
    size_t latency_count;
} bench_result_t;

/* Initialize benchmark result */
static inline void bench_init(bench_result_t* result, size_t max_samples) {
    result->total_ops = 0;
    result->total_time_ns = 0;
    result->min_latency_ns = UINT64_MAX;
    result->max_latency_ns = 0;
    result->latencies = malloc(max_samples * sizeof(uint64_t));
    result->latency_count = 0;
}

/* Record a latency sample */
static inline void bench_record(bench_result_t* result, uint64_t latency_ns) {
    result->total_ops++;
    result->total_time_ns += latency_ns;
    if (latency_ns < result->min_latency_ns) {
        result->min_latency_ns = latency_ns;
    }
    if (latency_ns > result->max_latency_ns) {
        result->max_latency_ns = latency_ns;
    }
    if (result->latencies) {
        result->latencies[result->latency_count++] = latency_ns;
    }
}

/* Compare for qsort */
static int compare_uint64(const void* a, const void* b) {
    uint64_t va = *(const uint64_t*)a;
    uint64_t vb = *(const uint64_t*)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* Get percentile latency */
static inline uint64_t bench_percentile(bench_result_t* result, double p) {
    if (result->latency_count == 0) return 0;

    /* Sort latencies */
    qsort(result->latencies, result->latency_count,
          sizeof(uint64_t), compare_uint64);

    size_t idx = (size_t)(p * result->latency_count / 100.0);
    if (idx >= result->latency_count) idx = result->latency_count - 1;
    return result->latencies[idx];
}

/* Print benchmark results */
static inline void bench_print(bench_result_t* result, const char* name) {
    double avg_ns = (double)result->total_time_ns / result->total_ops;
    double ops_per_sec = 1000000000.0 / avg_ns;

    printf("\n%s Results:\n", name);
    printf("  Total operations: %llu\n", (unsigned long long)result->total_ops);
    printf("  Total time:       %.2f ms\n", result->total_time_ns / 1000000.0);
    printf("  Throughput:       %.2f ops/sec\n", ops_per_sec);
    printf("  Latency:\n");
    printf("    Min:    %.2f us\n", result->min_latency_ns / 1000.0);
    printf("    Avg:    %.2f us\n", avg_ns / 1000.0);
    printf("    Max:    %.2f us\n", result->max_latency_ns / 1000.0);
    printf("    P50:    %.2f us\n", bench_percentile(result, 50) / 1000.0);
    printf("    P95:    %.2f us\n", bench_percentile(result, 95) / 1000.0);
    printf("    P99:    %.2f us\n", bench_percentile(result, 99) / 1000.0);
}

/* Free benchmark result */
static inline void bench_free(bench_result_t* result) {
    free(result->latencies);
    result->latencies = NULL;
}

#endif /* BENCH_COMMON_H */
