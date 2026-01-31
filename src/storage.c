/**
 * storage.c - Persistent storage implementation for Raft state
 *
 * File formats:
 * - raft_state.dat: | magic(4) | version(4) | crc32(4) | term(8) | voted_for(4) | pad(4) |
 * - raft_log.dat: Header + Entry records
 *   Header: | magic(4) | version(4) | base_index(8) | base_term(8) |
 *   Entry:  | record_len(4) | crc32(4) | term(8) | index(8) | cmd_len(4) | command(var) |
 */

#include "storage.h"
#include "crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define STATE_FILE "raft_state.dat"
#define LOG_FILE   "raft_log.dat"
#define TEMP_SUFFIX ".tmp"

/* State file structure (28 bytes) */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    uint64_t current_term;
    int32_t voted_for;
    uint32_t padding;
} __attribute__((packed)) state_file_t;

/* Log file header (24 bytes) */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t base_index;
    uint64_t base_term;
} __attribute__((packed)) log_header_t;

/* Log entry record header (24 bytes + variable command) */
typedef struct {
    uint32_t record_len;  /* Total record length including this header */
    uint32_t crc32;       /* CRC of term + index + cmd_len + command */
    uint64_t term;
    uint64_t index;
    uint32_t cmd_len;
} __attribute__((packed)) log_record_t;

struct raft_storage {
    char* data_dir;
    bool sync_writes;
    int log_fd;           /* File descriptor for log file */
    uint64_t log_entries; /* Number of entries in log */
};

static char* make_path(const char* dir, const char* file) {
    size_t len = strlen(dir) + strlen(file) + 2;
    char* path = malloc(len);
    if (path) {
        snprintf(path, len, "%s/%s", dir, file);
    }
    return path;
}

static raft_status_t write_file_atomic(const char* path, const void* data,
                                        size_t len, bool do_sync) {
    size_t tmp_len = strlen(path) + strlen(TEMP_SUFFIX) + 1;
    char* tmp_path = malloc(tmp_len);
    if (!tmp_path) return RAFT_NO_MEMORY;
    snprintf(tmp_path, tmp_len, "%s%s", path, TEMP_SUFFIX);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    ssize_t written = write(fd, data, len);
    if (written != (ssize_t)len) {
        close(fd);
        unlink(tmp_path);
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    if (do_sync && fsync(fd) < 0) {
        close(fd);
        unlink(tmp_path);
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    close(fd);

    if (rename(tmp_path, path) < 0) {
        unlink(tmp_path);
        free(tmp_path);
        return RAFT_IO_ERROR;
    }

    free(tmp_path);
    return RAFT_OK;
}

static uint64_t count_log_entries(int fd) {
    log_header_t header;
    if (lseek(fd, 0, SEEK_SET) < 0) return 0;
    if (read(fd, &header, sizeof(header)) != sizeof(header)) return 0;
    if (header.magic != RAFT_LOG_MAGIC) return 0;

    uint64_t count = 0;
    log_record_t rec;
    while (read(fd, &rec, sizeof(rec)) == sizeof(rec)) {
        if (rec.record_len < sizeof(rec)) break;
        /* Skip command data */
        off_t skip = rec.record_len - sizeof(rec);
        if (lseek(fd, skip, SEEK_CUR) < 0) break;
        count++;
    }
    return count;
}

raft_storage_t* raft_storage_open(const char* data_dir, bool sync_writes) {
    if (!data_dir) return NULL;

    /* Create directory if it doesn't exist */
    if (mkdir(data_dir, 0755) < 0 && errno != EEXIST) {
        return NULL;
    }

    raft_storage_t* storage = calloc(1, sizeof(raft_storage_t));
    if (!storage) return NULL;

    storage->data_dir = strdup(data_dir);
    if (!storage->data_dir) {
        free(storage);
        return NULL;
    }
    storage->sync_writes = sync_writes;
    storage->log_fd = -1;

    /* Open or create log file */
    char* log_path = make_path(data_dir, LOG_FILE);
    if (!log_path) {
        free(storage->data_dir);
        free(storage);
        return NULL;
    }

    storage->log_fd = open(log_path, O_RDWR | O_CREAT, 0644);
    if (storage->log_fd < 0) {
        free(log_path);
        free(storage->data_dir);
        free(storage);
        return NULL;
    }

    /* Check if log file needs header */
    struct stat st;
    if (fstat(storage->log_fd, &st) == 0 && st.st_size == 0) {
        log_header_t header = {
            .magic = RAFT_LOG_MAGIC,
            .version = RAFT_STORAGE_VERSION,
            .base_index = 0,
            .base_term = 0,
        };
        if (write(storage->log_fd, &header, sizeof(header)) != sizeof(header)) {
            close(storage->log_fd);
            free(log_path);
            free(storage->data_dir);
            free(storage);
            return NULL;
        }
        if (sync_writes) fsync(storage->log_fd);
    }

    storage->log_entries = count_log_entries(storage->log_fd);
    free(log_path);
    return storage;
}

void raft_storage_close(raft_storage_t* storage) {
    if (!storage) return;
    if (storage->log_fd >= 0) {
        close(storage->log_fd);
    }
    free(storage->data_dir);
    free(storage);
}

raft_status_t raft_storage_save_state(raft_storage_t* storage,
                                       uint64_t current_term,
                                       int32_t voted_for) {
    if (!storage) return RAFT_INVALID_ARG;

    state_file_t state = {
        .magic = RAFT_STATE_MAGIC,
        .version = RAFT_STORAGE_VERSION,
        .current_term = current_term,
        .voted_for = voted_for,
        .padding = 0,
    };

    /* Calculate CRC over term and voted_for */
    state.crc32 = crc32(&state.current_term, sizeof(state.current_term) + sizeof(state.voted_for));

    char* path = make_path(storage->data_dir, STATE_FILE);
    if (!path) return RAFT_NO_MEMORY;

    raft_status_t status = write_file_atomic(path, &state, sizeof(state), storage->sync_writes);
    free(path);
    return status;
}

raft_status_t raft_storage_load_state(raft_storage_t* storage,
                                       uint64_t* current_term,
                                       int32_t* voted_for) {
    if (!storage || !current_term || !voted_for) return RAFT_INVALID_ARG;

    char* path = make_path(storage->data_dir, STATE_FILE);
    if (!path) return RAFT_NO_MEMORY;

    int fd = open(path, O_RDONLY);
    free(path);
    if (fd < 0) {
        if (errno == ENOENT) return RAFT_NOT_FOUND;
        return RAFT_IO_ERROR;
    }

    state_file_t state;
    ssize_t n = read(fd, &state, sizeof(state));
    close(fd);

    if (n != sizeof(state)) return RAFT_IO_ERROR;
    if (state.magic != RAFT_STATE_MAGIC) return RAFT_CORRUPTION;
    if (state.version != RAFT_STORAGE_VERSION) return RAFT_CORRUPTION;

    /* Verify CRC */
    uint32_t expected_crc = crc32(&state.current_term,
                                   sizeof(state.current_term) + sizeof(state.voted_for));
    if (state.crc32 != expected_crc) return RAFT_CORRUPTION;

    *current_term = state.current_term;
    *voted_for = state.voted_for;
    return RAFT_OK;
}

raft_status_t raft_storage_append_entry(raft_storage_t* storage,
                                         const raft_entry_t* entry) {
    if (!storage || !entry) return RAFT_INVALID_ARG;
    if (storage->log_fd < 0) return RAFT_IO_ERROR;

    /* Seek to end of file */
    if (lseek(storage->log_fd, 0, SEEK_END) < 0) return RAFT_IO_ERROR;

    /* Prepare record */
    log_record_t rec = {
        .record_len = sizeof(log_record_t) + entry->command_len,
        .term = entry->term,
        .index = entry->index,
        .cmd_len = (uint32_t)entry->command_len,
    };

    /* Calculate CRC over term, index, cmd_len, and command */
    uint32_t crc = crc32(&rec.term, sizeof(rec.term) + sizeof(rec.index) + sizeof(rec.cmd_len));
    if (entry->command && entry->command_len > 0) {
        crc = crc32_update(crc, entry->command, entry->command_len);
    }
    rec.crc32 = crc;

    /* Write record header */
    if (write(storage->log_fd, &rec, sizeof(rec)) != sizeof(rec)) {
        return RAFT_IO_ERROR;
    }

    /* Write command data */
    if (entry->command && entry->command_len > 0) {
        if (write(storage->log_fd, entry->command, entry->command_len) != (ssize_t)entry->command_len) {
            return RAFT_IO_ERROR;
        }
    }

    if (storage->sync_writes && fsync(storage->log_fd) < 0) {
        return RAFT_IO_ERROR;
    }

    storage->log_entries++;
    return RAFT_OK;
}

raft_status_t raft_storage_truncate_log(raft_storage_t* storage,
                                         uint64_t after_index) {
    if (!storage) return RAFT_INVALID_ARG;
    if (storage->log_fd < 0) return RAFT_IO_ERROR;

    /* Seek to start of entries */
    if (lseek(storage->log_fd, sizeof(log_header_t), SEEK_SET) < 0) {
        return RAFT_IO_ERROR;
    }

    off_t truncate_pos = sizeof(log_header_t);
    uint64_t count = 0;
    log_record_t rec;

    while (read(storage->log_fd, &rec, sizeof(rec)) == sizeof(rec)) {
        if (rec.index > after_index) {
            /* Truncate here */
            break;
        }
        truncate_pos = lseek(storage->log_fd, 0, SEEK_CUR);
        if (truncate_pos < 0) return RAFT_IO_ERROR;
        /* Skip command data */
        off_t skip = rec.record_len - sizeof(rec);
        if (skip > 0 && lseek(storage->log_fd, skip, SEEK_CUR) < 0) {
            return RAFT_IO_ERROR;
        }
        truncate_pos = lseek(storage->log_fd, 0, SEEK_CUR);
        count++;
    }

    if (ftruncate(storage->log_fd, truncate_pos) < 0) {
        return RAFT_IO_ERROR;
    }

    if (storage->sync_writes && fsync(storage->log_fd) < 0) {
        return RAFT_IO_ERROR;
    }

    storage->log_entries = count;
    return RAFT_OK;
}

raft_status_t raft_storage_sync(raft_storage_t* storage) {
    if (!storage) return RAFT_INVALID_ARG;
    if (storage->log_fd >= 0 && fsync(storage->log_fd) < 0) {
        return RAFT_IO_ERROR;
    }
    return RAFT_OK;
}

const char* raft_storage_get_dir(raft_storage_t* storage) {
    return storage ? storage->data_dir : NULL;
}

raft_status_t raft_storage_iterate_log(raft_storage_t* storage,
                                        raft_log_iter_fn fn,
                                        void* ctx) {
    if (!storage || !fn) return RAFT_INVALID_ARG;
    if (storage->log_fd < 0) return RAFT_IO_ERROR;

    /* Seek to start of entries */
    if (lseek(storage->log_fd, sizeof(log_header_t), SEEK_SET) < 0) {
        return RAFT_IO_ERROR;
    }

    log_record_t rec;
    char* cmd_buf = NULL;
    size_t cmd_buf_size = 0;

    while (read(storage->log_fd, &rec, sizeof(rec)) == sizeof(rec)) {
        if (rec.record_len < sizeof(rec)) {
            free(cmd_buf);
            return RAFT_CORRUPTION;
        }

        /* Read command data */
        size_t cmd_len = rec.cmd_len;
        if (cmd_len > 0) {
            if (cmd_len > cmd_buf_size) {
                char* new_buf = realloc(cmd_buf, cmd_len);
                if (!new_buf) {
                    free(cmd_buf);
                    return RAFT_NO_MEMORY;
                }
                cmd_buf = new_buf;
                cmd_buf_size = cmd_len;
            }
            if (read(storage->log_fd, cmd_buf, cmd_len) != (ssize_t)cmd_len) {
                free(cmd_buf);
                return RAFT_IO_ERROR;
            }
        }

        /* Verify CRC */
        uint32_t crc = crc32(&rec.term, sizeof(rec.term) + sizeof(rec.index) + sizeof(rec.cmd_len));
        if (cmd_len > 0) {
            crc = crc32_update(crc, cmd_buf, cmd_len);
        }
        if (crc != rec.crc32) {
            free(cmd_buf);
            return RAFT_CORRUPTION;
        }

        /* Call callback */
        raft_status_t status = fn(ctx, rec.term, rec.index, cmd_buf, cmd_len);
        if (status != RAFT_OK) {
            free(cmd_buf);
            return status;
        }
    }

    free(cmd_buf);
    return RAFT_OK;
}

raft_status_t raft_storage_get_log_info(raft_storage_t* storage,
                                         uint64_t* base_index,
                                         uint64_t* base_term,
                                         uint64_t* entry_count) {
    if (!storage) return RAFT_INVALID_ARG;
    if (storage->log_fd < 0) return RAFT_IO_ERROR;

    if (lseek(storage->log_fd, 0, SEEK_SET) < 0) return RAFT_IO_ERROR;

    log_header_t header;
    if (read(storage->log_fd, &header, sizeof(header)) != sizeof(header)) {
        return RAFT_IO_ERROR;
    }

    if (header.magic != RAFT_LOG_MAGIC) return RAFT_CORRUPTION;

    if (base_index) *base_index = header.base_index;
    if (base_term) *base_term = header.base_term;
    if (entry_count) *entry_count = storage->log_entries;

    return RAFT_OK;
}
