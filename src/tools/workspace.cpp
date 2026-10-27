// src/tools/workspace.cpp — YOU IMPLEMENT (spec §4.4).
//
// Workspace::resolve: a purely lexical check, steps 1–6 of the spec.
// Do not touch the filesystem here (no exists(), canonical(), ...).
// The constructor, root() and check_symlinks() are provided in
// workspace_provided.cpp.

#include "tools/workspace.h"

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    // 1. Empty path.
    if (user_path.empty()) {
        *reason = "empty path";
        return false;
    }
    // 2. Embedded NUL: the OS would silently truncate the path there.
    if (user_path.find('\0') != std::string_view::npos) {
        *reason = "path contains a NUL byte";
        return false;
    }
    try {
        // 3. Only workspace-relative paths.
        fs::path p{std::string(user_path)};
        if (p.is_absolute() || p.has_root_path()) {
            *reason = "absolute paths are not allowed: " + std::string(user_path);
            return false;
        }
        // 4. Fold ".", "//" and "x/.." textually.
        fs::path n = p.lexically_normal();
        // 5. A leading ".." means the path climbs above root at some point.
        if (n.begin() != n.end() && *n.begin() == "..") {
            *reason = "path escapes the workspace: " + std::string(user_path);
            return false;
        }
        // 6. Anchor at root.
        *out = (n.empty() || n == ".") ? root() : root() / n;
        return true;
    } catch (...) {
        // std::filesystem may throw bad_alloc; resolve must never throw.
        *reason = "could not resolve path";
        return false;
    }
}
