/**
 * membership.h - Cluster membership changes for Raft
 *
 * Implements single-step membership changes (one node at a time).
 * This is a simplified approach that avoids the complexity of joint consensus.
 */

#ifndef RAFT_MEMBERSHIP_H
#define RAFT_MEMBERSHIP_H

#include "types.h"
#include "raft.h"

/**
 * Configuration type
 */
typedef enum {
    RAFT_CONFIG_STABLE,      /* Stable configuration */
    RAFT_CONFIG_TRANSITIONING, /* Configuration change in progress */
} raft_config_type_t;

/**
 * Cluster configuration
 */
typedef struct raft_cluster_config {
    int32_t* nodes;          /* Array of node IDs */
    int32_t node_count;      /* Number of nodes */
    int32_t pending_node;    /* Node being added/removed (-1 if none) */
    bool pending_add;        /* True if adding, false if removing */
} raft_cluster_config_t;

/**
 * Add a node to the cluster
 * Only the leader can initiate membership changes.
 * Only one change can be in progress at a time.
 *
 * @param node Raft node (must be leader)
 * @param new_node_id ID of node to add
 * @return RAFT_OK on success, RAFT_NOT_LEADER if not leader
 */
raft_status_t raft_add_node(raft_node_t* node, int32_t new_node_id);

/**
 * Remove a node from the cluster
 * Only the leader can initiate membership changes.
 * Only one change can be in progress at a time.
 *
 * @param node Raft node (must be leader)
 * @param node_id ID of node to remove
 * @return RAFT_OK on success, RAFT_NOT_LEADER if not leader
 */
raft_status_t raft_remove_node(raft_node_t* node, int32_t node_id);

/**
 * Check if a node is a voting member in the current configuration
 *
 * @param node Raft node
 * @param node_id ID to check
 * @return true if node_id is a voting member
 */
bool raft_is_voting_member(raft_node_t* node, int32_t node_id);

/**
 * Get the current configuration type
 *
 * @param node Raft node
 * @return Current configuration type
 */
raft_config_type_t raft_get_config_type(raft_node_t* node);

/**
 * Get the current number of nodes in the cluster
 *
 * @param node Raft node
 * @return Number of nodes
 */
int32_t raft_get_cluster_size(raft_node_t* node);

/**
 * Apply a configuration change entry
 * Called when a config entry is committed
 *
 * @param node Raft node
 * @param entry The configuration entry
 */
void raft_apply_config_change(raft_node_t* node, const raft_entry_t* entry);

#endif /* RAFT_MEMBERSHIP_H */
