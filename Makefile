# consensus (Raft) Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -Isrc
LDFLAGS = -lpthread

# Phase 1 sources
PHASE1_SRCS = src/log.c src/raft.c
PHASE1_OBJS = $(PHASE1_SRCS:.c=.o)

# Phase 2 sources (adds election, timer)
PHASE2_SRCS = $(PHASE1_SRCS) src/election.c src/timer.c
PHASE2_OBJS = $(PHASE2_SRCS:.c=.o)

# Phase 3 sources (adds replication, commit)
PHASE3_SRCS = $(PHASE2_SRCS) src/replication.c src/commit.c
PHASE3_OBJS = $(PHASE3_SRCS:.c=.o)

# Phase 4 sources (adds crc32, storage, snapshot, recovery)
PHASE4_SRCS = $(PHASE3_SRCS) src/crc32.c src/storage.c src/snapshot.c src/recovery.c
PHASE4_OBJS = $(PHASE4_SRCS:.c=.o)

# Phase 5 sources (adds membership, batch)
PHASE5_SRCS = $(PHASE4_SRCS) src/membership.c src/batch.c
PHASE5_OBJS = $(PHASE5_SRCS:.c=.o)

# Phase 6 sources (adds read, transfer)
PHASE6_SRCS = $(PHASE5_SRCS) src/read.c src/transfer.c
PHASE6_OBJS = $(PHASE6_SRCS:.c=.o)

.PHONY: all clean test

all: test_phase1

# Compile objects
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Phase 1 test
test_phase1: $(PHASE1_OBJS) tests/unit/test_phase1.c
	$(CC) $(CFLAGS) -o $@ tests/unit/test_phase1.c $(PHASE1_OBJS) $(LDFLAGS)

# Phase 2 test
test_phase2: $(PHASE2_OBJS) tests/unit/test_phase2.c
	$(CC) $(CFLAGS) -o $@ tests/unit/test_phase2.c $(PHASE2_OBJS) $(LDFLAGS)

# Phase 3 test
test_phase3: $(PHASE3_OBJS) tests/unit/test_phase3.c
	$(CC) $(CFLAGS) -o $@ tests/unit/test_phase3.c $(PHASE3_OBJS) $(LDFLAGS)

# Phase 4 test
test_phase4: $(PHASE4_OBJS) tests/unit/test_phase4.c
	$(CC) $(CFLAGS) -o $@ tests/unit/test_phase4.c $(PHASE4_OBJS) $(LDFLAGS)

# Phase 5 test
test_phase5: $(PHASE5_OBJS) tests/unit/test_phase5.c
	$(CC) $(CFLAGS) -o $@ tests/unit/test_phase5.c $(PHASE5_OBJS) $(LDFLAGS)

# Phase 6 test
test_phase6: $(PHASE6_OBJS) tests/unit/test_phase6.c
	$(CC) $(CFLAGS) -o $@ tests/unit/test_phase6.c $(PHASE6_OBJS) $(LDFLAGS)

# Integration tests
test_partition: $(PHASE6_OBJS) tests/integration/network_sim.c tests/integration/test_partition.c
	$(CC) $(CFLAGS) -o $@ tests/integration/network_sim.c tests/integration/test_partition.c $(PHASE6_OBJS) $(LDFLAGS)

test_chaos: $(PHASE6_OBJS) tests/integration/network_sim.c tests/integration/chaos.c tests/integration/test_chaos.c
	$(CC) $(CFLAGS) -o $@ tests/integration/network_sim.c tests/integration/chaos.c tests/integration/test_chaos.c $(PHASE6_OBJS) $(LDFLAGS)

# Benchmarks
bench_throughput: $(PHASE6_OBJS) tests/bench/bench_throughput.c
	$(CC) $(CFLAGS) -O2 -o $@ tests/bench/bench_throughput.c $(PHASE6_OBJS) $(LDFLAGS)

bench_latency: $(PHASE6_OBJS) tests/bench/bench_latency.c
	$(CC) $(CFLAGS) -O2 -o $@ tests/bench/bench_latency.c $(PHASE6_OBJS) $(LDFLAGS)

test: test_phase1
	./test_phase1

clean:
	rm -f src/*.o test_phase* raft-bench
	rm -rf /tmp/raft_test_* *.dSYM
