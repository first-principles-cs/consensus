/**
 * log.c - Raft log management implementation
 */

#include "log.h"
#include "param.h"
#include <stdlib.h>
#include <string.h>

raft_log_t* raft_log_create(void) {
    raft_log_t* log = calloc(1, sizeof(raft_log_t));
    if (!log) return NULL;

    log->entries = calloc(RAFT_LOG_INITIAL_CAPACITY, sizeof(raft_entry_t));
    if (!log->entries) {
        free(log);
        return NULL;
    }

    log->capacity = RAFT_LOG_INITIAL_CAPACITY;
    log->count = 0;
    log->base_index = 0;
    log->base_term = 0;

    return log;
}

void raft_log_destroy(raft_log_t* log) {
    if (!log) return;

    for (size_t i = 0; i < log->count; i++) {
        free(log->entries[i].command);
    }
    free(log->entries);
    free(log);
}

static raft_status_t log_grow(raft_log_t* log) {
    size_t new_capacity = log->capacity * 2;
    raft_entry_t* new_entries = realloc(log->entries,
                                        new_capacity * sizeof(raft_entry_t));
    if (!new_entries) return RAFT_NO_MEMORY;

    log->entries = new_entries;
    log->capacity = new_capacity;
    return RAFT_OK;
}

raft_status_t raft_log_append(raft_log_t* log, uint64_t term,
                              const char* command, size_t command_len,
                              uint64_t* out_index) {
    if (!log) return RAFT_INVALID_ARG;

    if (log->count >= log->capacity) {
        raft_status_t status = log_grow(log);
        if (status != RAFT_OK) return status;
    }

    raft_entry_t* entry = &log->entries[log->count];
    entry->term = term;
    entry->index = log->base_index + log->count + 1;

    if (command && command_len > 0) {
        entry->command = malloc(command_len);
        if (!entry->command) return RAFT_NO_MEMORY;
        memcpy(entry->command, command, command_len);
        entry->command_len = command_len;
    } else {
        entry->command = NULL;
        entry->command_len = 0;
    }

    log->count++;

    if (out_index) *out_index = entry->index;
    return RAFT_OK;
}

const raft_entry_t* raft_log_get(raft_log_t* log, uint64_t index) {
    if (!log || index == 0) return NULL;
    if (index <= log->base_index) return NULL;

    size_t offset = index - log->base_index - 1;
    if (offset >= log->count) return NULL;

    return &log->entries[offset];
}

raft_status_t raft_log_truncate_after(raft_log_t* log, uint64_t after_index) {
    if (!log) return RAFT_INVALID_ARG;

    uint64_t last = raft_log_last_index(log);
    if (after_index >= last) return RAFT_OK;

    /* Free entries being removed */
    for (uint64_t i = after_index + 1; i <= last; i++) {
        size_t offset = i - log->base_index - 1;
        free(log->entries[offset].command);
        log->entries[offset].command = NULL;
    }

    if (after_index <= log->base_index) {
        log->count = 0;
    } else {
        log->count = after_index - log->base_index;
    }

    return RAFT_OK;
}

raft_status_t raft_log_truncate_before(raft_log_t* log, uint64_t before_index) {
    if (!log) return RAFT_INVALID_ARG;
    if (before_index <= log->base_index + 1) return RAFT_OK;

    uint64_t last = raft_log_last_index(log);
    if (before_index > last + 1) {
        before_index = last + 1;
    }

    /* Save term of entry just before truncation point */
    const raft_entry_t* prev_entry = raft_log_get(log, before_index - 1);
    uint64_t new_base_term = prev_entry ? prev_entry->term : log->base_term;

    /* Free entries being removed */
    for (uint64_t i = log->base_index + 1; i < before_index; i++) {
        size_t offset = i - log->base_index - 1;
        free(log->entries[offset].command);
    }

    /* Shift remaining entries */
    size_t entries_to_remove = before_index - log->base_index - 1;
    size_t remaining = log->count - entries_to_remove;

    if (remaining > 0) {
        memmove(log->entries, &log->entries[entries_to_remove],
                remaining * sizeof(raft_entry_t));
    }

    log->count = remaining;
    log->base_index = before_index - 1;
    log->base_term = new_base_term;

    return RAFT_OK;
}

uint64_t raft_log_last_index(raft_log_t* log) {
    if (!log || log->count == 0) return log ? log->base_index : 0;
    return log->base_index + log->count;
}

uint64_t raft_log_last_term(raft_log_t* log) {
    if (!log || log->count == 0) return log ? log->base_term : 0;
    return log->entries[log->count - 1].term;
}

uint64_t raft_log_term_at(raft_log_t* log, uint64_t index) {
    if (!log || index == 0) return 0;
    if (index == log->base_index) return log->base_term;

    const raft_entry_t* entry = raft_log_get(log, index);
    return entry ? entry->term : 0;
}

size_t raft_log_count(raft_log_t* log) {
    return log ? log->count : 0;
}
