// src/tools/tool_registry.cpp — YOU IMPLEMENT (spec §4.3).
//
// A hand-written hash table. No std::unordered_map / std::map / std::set.
// Separate chaining with a power-of-two bucket array; the table doubles
// before an add() would push the load factor above kMaxLoadFactor.

#include "tools/tool_registry.h"

#include <cstdint>
#include <utility>

ToolRegistry::ToolRegistry() : buckets_(kInitialBuckets) {}

// The unique_ptrs in the buckets destroy every tool exactly once.
ToolRegistry::~ToolRegistry() = default;

// FNV-1a, 64-bit (spec Appendix A). Uses every byte, order-sensitive, and
// returns the full-width value; index_for() reduces it to a bucket.
std::size_t ToolRegistry::hash(std::string_view key) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}

std::size_t ToolRegistry::index_for(std::string_view key) const noexcept {
    return hash(key) & (buckets_.size() - 1);
}

// Moves every unique_ptr into a fresh array of new_bucket_count buckets,
// recomputing each index. Only the pointers move; the Tool objects stay put.
void ToolRegistry::rehash(std::size_t new_bucket_count) {
    std::vector<Bucket> old = std::move(buckets_);
    buckets_ = std::vector<Bucket>(new_bucket_count);
    for (Bucket& b : old)
        for (std::unique_ptr<Tool>& t : b)
            buckets_[index_for(t->name())].push_back(std::move(t));
}

bool ToolRegistry::add(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) return false;
    const std::string key = tool->name();
    if (find(key) != nullptr) return false;   // `tool` goes out of scope and is destroyed

    // Check before inserting so load_factor() never exceeds the threshold.
    if (static_cast<double>(size_ + 1) > kMaxLoadFactor * static_cast<double>(buckets_.size()))
        rehash(buckets_.size() * 2);

    buckets_[index_for(key)].push_back(std::move(tool));
    ++size_;
    return true;
}

Tool* ToolRegistry::find(std::string_view name) const {
    const Bucket& b = buckets_[index_for(name)];
    for (const std::unique_ptr<Tool>& t : b)
        if (t->name() == name) return t.get();
    return nullptr;
}

std::size_t ToolRegistry::size() const noexcept { return size_; }

void ToolRegistry::for_each(const std::function<void(const Tool&)>& fn) const {
    for (const Bucket& b : buckets_)
        for (const std::unique_ptr<Tool>& t : b) fn(*t);
}

std::size_t ToolRegistry::bucket_count() const noexcept { return buckets_.size(); }

double ToolRegistry::load_factor() const noexcept {
    return static_cast<double>(size_) / static_cast<double>(buckets_.size());
}

std::size_t ToolRegistry::max_probe_length() const {
    std::size_t longest = 0;
    for (const Bucket& b : buckets_)
        if (b.size() > longest) longest = b.size();
    return longest;
}
