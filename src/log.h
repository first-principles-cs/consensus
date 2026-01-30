/**
 * log.h - Raft log management
 *
 * Manages the replicated log of commands.
 */

#ifndef RAFT_LOG_H
#define RAFT_LOG_H

#include "types.h"

/**
 * Raft log structure
 */
struct raft_log {
    raft_entry_t* entries;  /* Array of log entries */
    size_t capacity;        /* Allocated capacity */
    size_t count;           /* Number of entries */
    uint64_t base_index;    /* Index of first entry (for compaction) */
    uint64_t base_term;     /* Term of entry before base_index */
};

/**
 * Create a new log
 */
raft_log_t* raft_log_create(void);

/**
 * Destroy a log and free all entries
 */
void raft_log_destroy(raft_log_t* log);

/**
 * Append an entry to the log
 * Returns the index of the appended entry
 */
raft_status_t raft_log_append(raft_log_t* log, uint64_t term,
                              const char* command, size_t command_len,
                              uint64_t* out_index);

/**
 * Get an entry by index (1-based)
 * Returns NULL if index is out of range
 */
const raft_entry_t* raft_log_get(raft_log_t* log, uint64_t index);

/**
 * Truncate log after given index (exclusive)
 * Removes all entries with index > after_index
 */
raft_status_t raft_log_truncate_after(raft_log_t* log, uint64_t after_index);

/**
 * Truncate log before given index (for compaction)
 * Removes all entries with index < before_index
 * Sets base_index and base_term appropriately
 */
raft_status_t raft_log_truncate_before(raft_log_t* log, uint64_t before_index);

/**
 * Get the index of the last entry (0 if empty)
 */
uint64_t raft_log_last_index(raft_log_t* log);

/**
 * Get the term of the last entry (0 if empty)
 */
uint64_t raft_log_last_term(raft_log_t* log);

/**
 * Get the term of entry at given index (0 if not found)
 */
uint64_t raft_log_term_at(raft_log_t* log, uint64_t index);

/**
 * Get number of entries in log
 */
size_t raft_log_count(raft_log_t* log);

#endif /* RAFT_LOG_H */
