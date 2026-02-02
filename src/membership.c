/**
 * membership.c - Cluster membership changes implementation
 *
 * Implements single-step membership changes for simplicity.
 * Configuration changes are logged as special entries.
 */

#include "membership.h"
#include "log.h"
#include "storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Configuration change command format:
 * First byte: 'A' for add, 'R' for remove
 * Remaining bytes: node_id as int32_t
 */
#define CONFIG_CMD_ADD    'A'
#define CONFIG_CMD_REMOVE 'R'
#define CONFIG_CMD_SIZE   (1 + sizeof(int32_t))

/* Static cluster config - simplified for educational purposes */
static raft_cluster_config_t g_config = {
    .nodes = NULL,
    .node_count = 0,
    .pending_node = -1,
    .pending_add = false,
};

static void ensure_config_initialized(raft_node_t* node) {
    if (g_config.nodes == NULL && node->num_nodes > 0) {
        g_config.nodes = malloc(sizeof(int32_t) * (size_t)node->num_nodes);
        if (g_config.nodes) {
            for (int32_t i = 0; i < node->num_nodes; i++) {
                g_config.nodes[i] = i;
            }
            g_config.node_count = node->num_nodes;
        }
    }
}

raft_status_t raft_add_node(raft_node_t* node, int32_t new_node_id) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    ensure_config_initialized(node);

    /* Check if already a member */
    for (int32_t i = 0; i < g_config.node_count; i++) {
        if (g_config.nodes[i] == new_node_id) {
            return RAFT_INVALID_ARG;  /* Already a member */
        }
    }

    /* Check if another change is in progress */
    if (g_config.pending_node >= 0) {
        return RAFT_INVALID_ARG;  /* Change already in progress */
    }

    /* Create config change command */
    char cmd[CONFIG_CMD_SIZE];
    cmd[0] = CONFIG_CMD_ADD;
    memcpy(&cmd[1], &new_node_id, sizeof(int32_t));

    /* Append as config entry to log */
    uint64_t index;
    raft_status_t status = raft_log_append(node->log, node->persistent.current_term,
                                            cmd, CONFIG_CMD_SIZE, &index);
    if (status != RAFT_OK) return status;

    /* Mark the entry type as config (we need to set this after append) */
    raft_entry_t* entry = (raft_entry_t*)raft_log_get(node->log, index);
    if (entry) {
        entry->type = RAFT_ENTRY_CONFIG;
    }

    /* Mark change as pending */
    g_config.pending_node = new_node_id;
    g_config.pending_add = true;

    /* Persist if storage is enabled */
    if (node->storage) {
        raft_entry_t log_entry = {
            .term = node->persistent.current_term,
            .index = index,
            .type = RAFT_ENTRY_CONFIG,
            .command = cmd,
            .command_len = CONFIG_CMD_SIZE,
        };
        raft_storage_append_entry(node->storage, &log_entry);
    }

    return RAFT_OK;
}

raft_status_t raft_remove_node(raft_node_t* node, int32_t node_id) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    ensure_config_initialized(node);

    /* Check if actually a member */
    bool found = false;
    for (int32_t i = 0; i < g_config.node_count; i++) {
        if (g_config.nodes[i] == node_id) {
            found = true;
            break;
        }
    }
    if (!found) {
        return RAFT_INVALID_ARG;  /* Not a member */
    }

    /* Check if another change is in progress */
    if (g_config.pending_node >= 0) {
        return RAFT_INVALID_ARG;  /* Change already in progress */
    }

    /* Create config change command */
    char cmd[CONFIG_CMD_SIZE];
    cmd[0] = CONFIG_CMD_REMOVE;
    memcpy(&cmd[1], &node_id, sizeof(int32_t));

    /* Append as config entry to log */
    uint64_t index;
    raft_status_t status = raft_log_append(node->log, node->persistent.current_term,
                                            cmd, CONFIG_CMD_SIZE, &index);
    if (status != RAFT_OK) return status;

    /* Mark the entry type as config */
    raft_entry_t* entry = (raft_entry_t*)raft_log_get(node->log, index);
    if (entry) {
        entry->type = RAFT_ENTRY_CONFIG;
    }

    /* Mark change as pending */
    g_config.pending_node = node_id;
    g_config.pending_add = false;

    /* Persist if storage is enabled */
    if (node->storage) {
        raft_entry_t log_entry = {
            .term = node->persistent.current_term,
            .index = index,
            .type = RAFT_ENTRY_CONFIG,
            .command = cmd,
            .command_len = CONFIG_CMD_SIZE,
        };
        raft_storage_append_entry(node->storage, &log_entry);
    }

    return RAFT_OK;
}

bool raft_is_voting_member(raft_node_t* node, int32_t node_id) {
    if (!node) return false;

    ensure_config_initialized(node);

    for (int32_t i = 0; i < g_config.node_count; i++) {
        if (g_config.nodes[i] == node_id) {
            return true;
        }
    }

    /* Also check pending add */
    if (g_config.pending_add && g_config.pending_node == node_id) {
        return true;
    }

    return false;
}

raft_config_type_t raft_get_config_type(raft_node_t* node) {
    if (!node) return RAFT_CONFIG_STABLE;

    ensure_config_initialized(node);

    if (g_config.pending_node >= 0) {
        return RAFT_CONFIG_TRANSITIONING;
    }
    return RAFT_CONFIG_STABLE;
}

int32_t raft_get_cluster_size(raft_node_t* node) {
    if (!node) return 0;

    ensure_config_initialized(node);

    int32_t size = g_config.node_count;

    /* Include pending add in count for quorum calculation */
    if (g_config.pending_add && g_config.pending_node >= 0) {
        size++;
    }

    return size;
}

void raft_apply_config_change(raft_node_t* node, const raft_entry_t* entry) {
    if (!node || !entry || entry->type != RAFT_ENTRY_CONFIG) return;
    if (entry->command_len < CONFIG_CMD_SIZE) return;

    ensure_config_initialized(node);

    char op = entry->command[0];
    int32_t target_node;
    memcpy(&target_node, &entry->command[1], sizeof(int32_t));

    if (op == CONFIG_CMD_ADD) {
        /* Add node to config */
        int32_t* new_nodes = realloc(g_config.nodes,
                                      sizeof(int32_t) * (size_t)(g_config.node_count + 1));
        if (new_nodes) {
            g_config.nodes = new_nodes;
            g_config.nodes[g_config.node_count] = target_node;
            g_config.node_count++;
        }

        /* Update node's num_nodes */
        node->num_nodes = g_config.node_count;

    } else if (op == CONFIG_CMD_REMOVE) {
        /* Remove node from config */
        for (int32_t i = 0; i < g_config.node_count; i++) {
            if (g_config.nodes[i] == target_node) {
                /* Shift remaining nodes */
                memmove(&g_config.nodes[i], &g_config.nodes[i + 1],
                        sizeof(int32_t) * (size_t)(g_config.node_count - i - 1));
                g_config.node_count--;
                break;
            }
        }

        /* Update node's num_nodes */
        node->num_nodes = g_config.node_count;
    }

    /* Clear pending state */
    g_config.pending_node = -1;
    g_config.pending_add = false;
}

/* Reset config for testing */
void raft_membership_reset(void) {
    free(g_config.nodes);
    g_config.nodes = NULL;
    g_config.node_count = 0;
    g_config.pending_node = -1;
    g_config.pending_add = false;
}
