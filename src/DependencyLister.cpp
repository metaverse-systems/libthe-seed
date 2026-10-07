#include <libthe-seed/DependencyLister.hpp>

#include "ElfParser.hpp"
#include "PeParser.hpp"
#include "internal/DependencyGraph.hpp"
#include "internal/FileIO.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;
using seed::internal::DependencyGraph;
using seed::internal::DependencyNode;
using seed::internal::NodeFormat;
using seed::internal::NodeId;
using seed::internal::NodeStatus;

std::string LowerAscii(std::string text)
{
    for(char &c : text)
    {
        if(c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// Reads the format from the bytes already in memory and lists the names the
// file records.
std::vector<std::string> ParseBytes(const std::vector<std::uint8_t> &bytes, NodeFormat &format)
{
    if(bytes.size() < 2)
    {
        throw std::runtime_error("File is too small to determine binary format");
    }

    if(bytes.size() >= 4 && bytes[0] == ELFMAG0 && bytes[1] == ELFMAG1 && bytes[2] == ELFMAG2 &&
       bytes[3] == ELFMAG3)
    {
        format = NodeFormat::Elf;
        return ElfParser::ListDependenciesFromBytes(bytes);
    }

    if(bytes[0] == 'M' && bytes[1] == 'Z')
    {
        format = NodeFormat::Pe;
        return PeParser::ListDependenciesFromBytes(bytes);
    }

    throw std::runtime_error("Unsupported binary format");
}

// A recorded name that must never be joined to a search folder: it could lead
// outside the folders the caller named.
bool EscapesSearchFolders(const std::string &name)
{
    if(name.empty() || name == "." || name == "..")
    {
        return true;
    }
    if(name.find_first_of("/\\") != std::string::npos || name.find('\0') != std::string::npos)
    {
        return true;
    }
    return name.size() >= 2 && name[1] == ':';
}

NodeId NewNode(DependencyGraph &graph, std::string key, NodeStatus status)
{
    DependencyNode node;
    node.key = std::move(key);
    node.status = status;
    graph.nodes.push_back(std::move(node));
    return graph.nodes.size() - 1;
}

NodeId UnresolvedNode(DependencyGraph &graph, const std::string &name)
{
    const auto found = graph.by_unresolved_name.find(name);
    if(found != graph.by_unresolved_name.end())
    {
        return found->second;
    }
    const NodeId id = NewNode(graph, name, NodeStatus::Unresolved);
    graph.by_unresolved_name.emplace(name, id);
    return id;
}

// The node of a file, created, read and parsed on first use. Two paths to one
// file give one node.
NodeId FileNode(DependencyGraph &graph, const std::string &path, const std::string *known_canonical = nullptr)
{
    std::error_code error;
    std::string key;
    if(known_canonical != nullptr)
    {
        key = *known_canonical;
    }
    else
    {
        const fs::path canonical = fs::canonical(path, error);
        key = error ? path : canonical.string();
    }

    const auto found = graph.by_key.find(key);
    if(found != graph.by_key.end())
    {
        return found->second;
    }

    const NodeId id = NewNode(graph, key, NodeStatus::Unreadable);
    graph.by_key.emplace(key, id);

    std::error_code status_error;
    const bool regular = !error && fs::is_regular_file(key, status_error) && !status_error;
    if(!regular)
    {
        std::error_code entry_error;
        const auto entry = fs::symlink_status(path, entry_error);
        if(entry_error || entry.type() == fs::file_type::not_found)
        {
            graph.nodes[id].reason = "Unable to open file: " + path;
        }
        else
        {
            graph.nodes[id].reason = key + ": not a readable regular file";
        }
        return id;
    }

    try
    {
        const std::vector<std::uint8_t> bytes = ReadFileBytes(key);
        NodeFormat format = NodeFormat::None;
        std::vector<std::string> refs = ParseBytes(bytes, format);
        DependencyNode &node = graph.nodes[id];
        node.format = format;
        node.refs = std::move(refs);
        node.status = NodeStatus::Parsed;
    }
    catch(const std::exception &failure)
    {
        graph.nodes[id].reason = failure.what();
    }
    return id;
}

bool EntryExists(const fs::path &candidate)
{
    std::error_code error;
    const auto entry = fs::symlink_status(candidate, error);
    return !error && entry.type() != fs::file_type::not_found;
}

const std::vector<std::string> *FolderMatches(
    DependencyGraph &graph,
    const std::string &folder,
    const std::string &name
)
{
    auto listing = graph.folder_listings.find(folder);
    if(listing == graph.folder_listings.end())
    {
        std::unordered_map<std::string, std::vector<std::string>> entries;
        std::error_code error;
        for(fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            const std::string real = it->path().filename().string();
            entries[LowerAscii(real)].push_back(real);
        }
        for(auto &entry : entries)
        {
            std::sort(entry.second.begin(), entry.second.end());
        }
        listing = graph.folder_listings.emplace(folder, std::move(entries)).first;
    }
    const auto match = listing->second.find(LowerAscii(name));
    return match == listing->second.end() ? nullptr : &match->second;
}

// Finds the node a recorded name stands for, looking only in the search
// folders. ELF names match exactly; PE names match ignoring ASCII case.
NodeId ResolveName(
    DependencyGraph &graph,
    const std::vector<std::string> &search_paths,
    const std::string &name,
    bool ignore_case
)
{
    auto &cache = ignore_case ? graph.resolved_ignoring_case : graph.resolved_exact;
    const auto cached = cache.find(name);
    if(cached != cache.end())
    {
        return cached->second;
    }

    NodeId result = 0;
    bool found = false;
    if(!EscapesSearchFolders(name))
    {
        for(const std::string &folder : search_paths)
        {
            std::error_code error;
            if(folder.empty() || !fs::is_directory(folder, error))
            {
                continue;
            }

            fs::path candidate = fs::path(folder) / name;
            if(!EntryExists(candidate))
            {
                const std::vector<std::string> *matches =
                    ignore_case ? FolderMatches(graph, folder, name) : nullptr;
                if(matches == nullptr)
                {
                    continue;
                }
                candidate = fs::path(folder) / matches->front();
            }

            result = FileNode(graph, candidate.string());
            found = true;
            break;
        }
    }

    if(!found)
    {
        result = UnresolvedNode(graph, name);
    }
    cache.emplace(name, result);
    return result;
}

void Expand(DependencyGraph &graph, const std::vector<std::string> &search_paths, NodeId id)
{
    std::vector<std::string> refs = std::move(graph.nodes[id].refs);
    graph.nodes[id].refs.clear();
    const bool ignore_case = graph.nodes[id].format == NodeFormat::Pe;

    std::vector<NodeId> successors;
    successors.reserve(refs.size());
    for(const std::string &name : refs)
    {
        // May add nodes; no reference into graph.nodes is held across it.
        successors.push_back(ResolveName(graph, search_paths, name, ignore_case));
    }
    graph.nodes[id].successors = std::move(successors);
    graph.nodes[id].expanded = true;
}

struct Input
{
    std::string spelling;
    // Canonical path of the file; empty when the path cannot be resolved.
    std::string canonical;
    NodeId node = 0;
};

// One spelling per distinct input: duplicates collapse, and two spellings of
// one file keep the smallest in byte order, so the order of the request does
// not matter. The result is sorted by spelling.
std::vector<Input> NormaliseInputs(const std::vector<std::string> &binary_paths)
{
    std::map<std::string, Input> by_identity;
    for(const std::string &path : binary_paths)
    {
        std::error_code error;
        const fs::path canonical = fs::canonical(path, error);
        const std::string identity = error ? "raw:" + path : "file:" + canonical.string();
        auto slot = by_identity.find(identity);
        if(slot == by_identity.end())
        {
            Input input;
            input.spelling = path;
            if(!error)
            {
                input.canonical = canonical.string();
            }
            by_identity.emplace(identity, std::move(input));
        }
        else if(path < slot->second.spelling)
        {
            slot->second.spelling = path;
        }
    }

    std::vector<Input> inputs;
    inputs.reserve(by_identity.size());
    for(auto &entry : by_identity)
    {
        inputs.push_back(std::move(entry.second));
    }
    std::sort(inputs.begin(), inputs.end(),
              [](const Input &left, const Input &right) { return left.spelling < right.spelling; });
    return inputs;
}
} // namespace

DependencyResult DependencyLister::ListDependencies(
    const std::vector<std::string> &binary_paths,
    const std::vector<std::string> &search_paths
)
{
    DependencyResult result;
    DependencyGraph graph;

    std::vector<Input> inputs = NormaliseInputs(binary_paths);
    for(Input &input : inputs)
    {
        input.node = FileNode(graph, input.spelling, input.canonical.empty() ? nullptr : &input.canonical);
        if(graph.nodes[input.node].status == NodeStatus::Unreadable)
        {
            result.errors[input.spelling] = graph.nodes[input.node].reason;
        }
    }

    // Read and parse every reachable file once. Nodes are appended while this
    // runs, so it walks by index.
    for(NodeId id = 0; id < graph.nodes.size(); ++id)
    {
        if(graph.nodes[id].status == NodeStatus::Parsed && !graph.nodes[id].expanded)
        {
            Expand(graph, search_paths, id);
        }
    }

    // Credit each input with everything reachable from it. Inputs are visited
    // in sorted order, so each node's credit list is already sorted.
    std::vector<std::vector<std::uint32_t>> credits(graph.nodes.size());
    std::vector<std::uint32_t> seen(graph.nodes.size(), 0);
    std::vector<NodeId> queue;
    for(std::size_t index = 0; index < inputs.size(); ++index)
    {
        const std::uint32_t stamp = static_cast<std::uint32_t>(index + 1);
        queue.clear();
        for(const NodeId next : graph.nodes[inputs[index].node].successors)
        {
            if(seen[next] != stamp)
            {
                seen[next] = stamp;
                queue.push_back(next);
            }
        }
        for(std::size_t head = 0; head < queue.size(); ++head)
        {
            const NodeId current = queue[head];
            credits[current].push_back(static_cast<std::uint32_t>(index));
            for(const NodeId next : graph.nodes[current].successors)
            {
                if(seen[next] != stamp)
                {
                    seen[next] = stamp;
                    queue.push_back(next);
                }
            }
        }
    }

    for(NodeId id = 0; id < graph.nodes.size(); ++id)
    {
        if(credits[id].empty())
        {
            continue;
        }
        auto &listed = result.dependencies[graph.nodes[id].key];
        // A node's credits are already in sorted order, so only a key that
        // two nodes share (an unresolved name equal to a found path) needs
        // sorting again.
        const bool shared_key = !listed.empty();
        listed.reserve(listed.size() + credits[id].size());
        for(const std::uint32_t index : credits[id])
        {
            listed.push_back(inputs[index].spelling);
        }
        if(shared_key)
        {
            std::sort(listed.begin(), listed.end());
            listed.erase(std::unique(listed.begin(), listed.end()), listed.end());
        }
    }

    return result;
}
