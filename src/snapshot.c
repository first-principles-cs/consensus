/**
 * snapshot.c - Snapshot support implementation
 *
 * Full implementation for Phase 5, auto-compaction for Phase 6.
 */

#include "snapshot.h"
#include "crc32.h"
#include "raft.h"
#include "log.h"
#include "param.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* Global snapshot callback (per-node in production, simplified here) */
static raft_snapshot_cb g_snapshot_cb = NULL;
static void* g_snapshot_user_data = NULL;

/* Snapshot file header */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    uint32_t padding;
    uint64_t last_index;
    uint64_t last_term;
    uint64_t state_len;
} __attribute__((packed)) snapshot_header_t;

static char* make_snapshot_path(const char* data_dir) {
    size_t len = strlen(data_dir) + strlen(RAFT_SNAPSHOT_FILE) + 2;
    char* path = malloc(len);
    if (path) {
        snprintf(path, len, "%s/%s", data_dir, RAFT_SNAPSHOT_FILE);
    }
    return path;
}

bool raft_snapshot_exists(const char* data_dir) {
    if (!data_dir) return false;

    char* path = make_snapshot_path(data_dir);
    if (!path) return false;

    struct stat st;
    bool exists = (stat(path, &st) == 0 && st.st_size >= (off_t)sizeof(snapshot_header_t));
    free(path);
    return exists;
}

raft_status_t raft_snapshot_load_meta(const char* data_dir,
                                       raft_snapshot_meta_t* meta) {
    if (!data_dir || !meta) return RAFT_INVALID_ARG;

    char* path = make_snapshot_path(data_dir);
    if (!path) return RAFT_NO_MEMORY;

    int fd = open(path, O_RDONLY);
    free(path);
    if (fd < 0) return RAFT_NOT_FOUND;

    snapshot_header_t header;
    ssize_t n = read(fd, &header, sizeof(header));
    close(fd);

    if (n != sizeof(header)) return RAFT_IO_ERROR;
    if (header.magic != RAFT_SNAPSHOT_MAGIC) return RAFT_CORRUPTION;
    if (header.version != RAFT_SNAPSHOT_VERSION) return RAFT_CORRUPTION;

    /* Verify CRC (covers last_index, last_term, state_len) */
    uint32_t expected_crc = crc32(&header.last_index,
                                   sizeof(header.last_index) + sizeof(header.last_term));
    if (header.crc32 != expected_crc) return RAFT_CORRUPTION;

    meta->last_index = header.last_index;
    meta->last_term = header.last_term;
    return RAFT_OK;
}

raft_status_t raft_snapshot_create(const char* data_dir,
                                    uint64_t last_index,
                                    uint64_t last_term,
                                    const void* state_data,
                                    size_t state_len) {
    if (!data_dir) return RAFT_INVALID_ARG;

    char* path = make_snapshot_path(data_dir);
    if (!path) return RAFT_NO_MEMORY;

    /* Write to temp file first, then rename for atomicity */
    size_t tmp_len = strlen(path) + 5;
    char* tmp_path = malloc(tmp_len);
    if (!tmp_path) {
        free(path);
        return RAFT_NO_MEMORY;
    }
    snprintf(tmp_path, tmp_len, "%s.tmp", path);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(path);
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    /* Prepare header */
    snapshot_header_t header = {
        .magic = RAFT_SNAPSHOT_MAGIC,
        .version = RAFT_SNAPSHOT_VERSION,
        .padding = 0,
        .last_index = last_index,
        .last_term = last_term,
        .state_len = state_len,
    };
    header.crc32 = crc32(&header.last_index,
                          sizeof(header.last_index) + sizeof(header.last_term));

    /* Write header */
    ssize_t n = write(fd, &header, sizeof(header));
    if (n != sizeof(header)) {
        close(fd);
        unlink(tmp_path);
        free(path);
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    /* Write state data */
    if (state_data && state_len > 0) {
        n = write(fd, state_data, state_len);
        if (n != (ssize_t)state_len) {
            close(fd);
            unlink(tmp_path);
            free(path);
            free(tmp_path);
            return RAFT_IO_ERROR;
        }
    }

    fsync(fd);
    close(fd);

    /* Atomic rename */
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        free(path);
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    free(path);
    free(tmp_path);
    return RAFT_OK;
}

raft_status_t raft_snapshot_load(const char* data_dir,
                                  raft_snapshot_meta_t* meta,
                                  void** state_data,
                                  size_t* state_len) {
    if (!data_dir || !meta || !state_data || !state_len) return RAFT_INVALID_ARG;

    char* path = make_snapshot_path(data_dir);
    if (!path) return RAFT_NO_MEMORY;

    int fd = open(path, O_RDONLY);
    free(path);
    if (fd < 0) return RAFT_NOT_FOUND;

    snapshot_header_t header;
    ssize_t n = read(fd, &header, sizeof(header));
    if (n != sizeof(header)) {
        close(fd);
        return RAFT_IO_ERROR;
    }

    if (header.magic != RAFT_SNAPSHOT_MAGIC) {
        close(fd);
        return RAFT_CORRUPTION;
    }
    if (header.version != RAFT_SNAPSHOT_VERSION) {
        close(fd);
        return RAFT_CORRUPTION;
    }

    /* Verify CRC */
    uint32_t expected_crc = crc32(&header.last_index,
                                   sizeof(header.last_index) + sizeof(header.last_term));
    if (header.crc32 != expected_crc) {
        close(fd);
        return RAFT_CORRUPTION;
    }

    meta->last_index = header.last_index;
    meta->last_term = header.last_term;
    *state_len = header.state_len;

    /* Read state data */
    if (header.state_len > 0) {
        *state_data = malloc(header.state_len);
        if (!*state_data) {
            close(fd);
            return RAFT_NO_MEMORY;
        }
        n = read(fd, *state_data, header.state_len);
        if (n != (ssize_t)header.state_len) {
            free(*state_data);
            *state_data = NULL;
            close(fd);
            return RAFT_IO_ERROR;
        }
    } else {
        *state_data = NULL;
    }

    close(fd);
    return RAFT_OK;
}

raft_status_t raft_snapshot_install(raft_node_t* node,
                                     const raft_snapshot_meta_t* meta,
                                     const void* state_data,
                                     size_t state_len) {
    if (!node || !meta) return RAFT_INVALID_ARG;

    /* Save snapshot to disk if persistence is enabled */
    if (node->data_dir) {
        raft_status_t status = raft_snapshot_create(node->data_dir,
                                                     meta->last_index,
                                                     meta->last_term,
                                                     state_data,
                                                     state_len);
        if (status != RAFT_OK) return status;
    }

    /* Discard entire log and reset to snapshot state */
    raft_log_t* log = node->log;

    /* Free all existing entries */
    for (size_t i = 0; i < log->count; i++) {
        free(log->entries[i].command);
    }
    log->count = 0;

    /* Set log base to snapshot point */
    log->base_index = meta->last_index;
    log->base_term = meta->last_term;

    /* Update volatile state */
    if (meta->last_index > node->volatile_state.commit_index) {
        node->volatile_state.commit_index = meta->last_index;
    }
    if (meta->last_index > node->volatile_state.last_applied) {
        node->volatile_state.last_applied = meta->last_index;
    }

    return RAFT_OK;
}

void raft_set_snapshot_callback(raft_node_t* node, raft_snapshot_cb callback,
                                 void* user_data) {
    (void)node;  /* Would be per-node in production */
    g_snapshot_cb = callback;
    g_snapshot_user_data = user_data;
}

uint64_t raft_entries_since_snapshot(raft_node_t* node) {
    if (!node || !node->log) return 0;
    return raft_log_count(node->log);
}

raft_status_t raft_maybe_compact(raft_node_t* node) {
    if (!node || !node->data_dir) return RAFT_OK;

    /* Check if we have enough entries to compact */
    uint64_t entries = raft_entries_since_snapshot(node);
    if (entries < RAFT_AUTO_COMPACTION_THRESHOLD) {
        return RAFT_OK;
    }

    /* Need a snapshot callback to create state */
    if (!g_snapshot_cb) {
        return RAFT_OK;
    }

    /* Only compact up to last_applied */
    uint64_t compact_index = node->volatile_state.last_applied;
    if (compact_index == 0) {
        return RAFT_OK;
    }

    /* Get the term at compact_index */
    const raft_entry_t* entry = raft_log_get(node->log, compact_index);
    uint64_t compact_term = entry ? entry->term : node->log->base_term;

    /* Get state data from callback */
    void* state_data = NULL;
    size_t state_len = 0;
    raft_status_t status = g_snapshot_cb(node, &state_data, &state_len,
                                          g_snapshot_user_data);
    if (status != RAFT_OK) {
        return status;
    }

    /* Create snapshot */
    status = raft_snapshot_create(node->data_dir, compact_index, compact_term,
                                   state_data, state_len);
    free(state_data);

    if (status != RAFT_OK) {
        return status;
    }

    /* Truncate log up to compact_index */
    raft_log_t* log = node->log;

    /* Free entries up to compact_index */
    uint64_t entries_to_remove = compact_index - log->base_index;
    for (uint64_t i = 0; i < entries_to_remove && i < log->count; i++) {
        free(log->entries[i].command);
    }

    /* Shift remaining entries */
    if (entries_to_remove < log->count) {
        size_t remaining = log->count - entries_to_remove;
        memmove(log->entries, log->entries + entries_to_remove,
                remaining * sizeof(raft_entry_t));
        log->count = remaining;
    } else {
        log->count = 0;
    }

    log->base_index = compact_index;
    log->base_term = compact_term;

    return RAFT_OK;
}

/* Reset function for testing */
void raft_snapshot_reset_callback(void) {
    g_snapshot_cb = NULL;
    g_snapshot_user_data = NULL;
}
