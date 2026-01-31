/**
 * crc32.h - CRC32 checksum for data integrity
 *
 * Provides CRC32 calculation for verifying data integrity in storage.
 */

#ifndef RAFT_CRC32_H
#define RAFT_CRC32_H

#include <stdint.h>
#include <stddef.h>

/**
 * Calculate CRC32 checksum for data
 */
uint32_t crc32(const void* data, size_t len);

/**
 * Update CRC32 incrementally (for streaming data)
 */
uint32_t crc32_update(uint32_t crc, const void* data, size_t len);

#endif /* RAFT_CRC32_H */
