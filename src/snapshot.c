/**
 * snapshot.c - Snapshot support implementation
 *
 * Minimal implementation for Phase 4.
 */

#include "snapshot.h"
#include "crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* Snapshot file header */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    uint32_t padding;
    uint64_t last_index;
    uint64_t last_term;
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

    /* Verify CRC */
    uint32_t expected_crc = crc32(&header.last_index,
                                   sizeof(header.last_index) + sizeof(header.last_term));
    if (header.crc32 != expected_crc) return RAFT_CORRUPTION;

    meta->last_index = header.last_index;
    meta->last_term = header.last_term;
    return RAFT_OK;
}
