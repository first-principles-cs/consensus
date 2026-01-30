/**
 * election.h - Election logic for Raft consensus
 *
 * Handles leader election, vote requests, and state transitions.
 */

#ifndef RAFT_ELECTION_H
#define RAFT_ELECTION_H

#include "types.h"
#include "rpc.h"

/**
 * Start a new election
 * Transitions to candidate, increments term, votes for self, sends RequestVote
 */
raft_status_t raft_start_election(raft_node_t* node);

/**
 * Handle incoming RequestVote RPC
 */
raft_status_t raft_handle_request_vote(raft_node_t* node,
                                        const raft_request_vote_t* request,
                                        raft_request_vote_response_t* response);

/**
 * Handle RequestVote response
 */
raft_status_t raft_handle_request_vote_response(raft_node_t* node,
                                                 int32_t from_node,
                                                 const raft_request_vote_response_t* response);

/**
 * Handle incoming AppendEntries RPC
 */
raft_status_t raft_handle_append_entries(raft_node_t* node,
                                          const raft_append_entries_t* request,
                                          raft_append_entries_response_t* response);

/**
 * Step down to follower state with new term
 */
void raft_step_down(raft_node_t* node, uint64_t new_term);

/**
 * Send heartbeats to all peers (leader only)
 */
raft_status_t raft_send_heartbeats(raft_node_t* node);

/**
 * Receive and dispatch an RPC message
 */
raft_status_t raft_receive_message(raft_node_t* node, int32_t from_node,
                                    const void* msg, size_t msg_len);

#endif /* RAFT_ELECTION_H */
