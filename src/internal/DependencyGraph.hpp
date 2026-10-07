#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace seed::internal
{

// Index of a node in a DependencyGraph.
using NodeId = std::size_t;

enum class NodeStatus
{
    // Not found in any search folder: only the recorded name is known.
    Unresolved,
    // Found, read and parsed; `refs` holds the names it records.
    Parsed,
    // Found, but the file could not be read or is malformed; `reason` says why.
    Unreadable
};

enum class NodeFormat
{
    None,
    Elf,
    Pe
};

// One file (or one name that was not found) in a dependency request.
struct DependencyNode
{
    NodeStatus status = NodeStatus::Unresolved;
    // Canonical path of the file; the candidate path for a broken entry; the
    // recorded name for an unresolved reference.
    std::string key;
    // For Unreadable nodes, the message of the failure.
    std::string reason;
    NodeFormat format = NodeFormat::None;
    // Names recorded in the file, in the order the parser returned them.
    // Emptied once the node has been expanded.
    std::vector<std::string> refs;
    // Nodes this one needs, filled when the node is expanded.
    std::vector<NodeId> successors;
    // True once `successors` has been filled.
    bool expanded = false;
};

// All the nodes of one request, with the lookups that make each distinct file
// be read and parsed once. It lives for the duration of one request.
struct DependencyGraph
{
    std::vector<DependencyNode> nodes;
    // Canonical key -> node, for files that were found.
    std::unordered_map<std::string, NodeId> by_key;
    // Recorded name -> node, for names that were not found.
    std::unordered_map<std::string, NodeId> by_unresolved_name;
    // Search folder -> lower-case entry name -> real entry names in byte order.
    // Used only for references held in PE files.
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>>
        folder_listings;
    // Reference name -> node it resolves to, per case rule. A request has one
    // list of search folders, so the name alone identifies the answer.
    std::unordered_map<std::string, NodeId> resolved_exact;
    std::unordered_map<std::string, NodeId> resolved_ignoring_case;
};

} // namespace seed::internal
