#pragma once

#include "PakFile.hpp"
#include "PakIndex.hpp"

#include <libthe-seed/LoadError.hpp>

#include <libecs-cpp/ecs.hpp>

#include <memory>
#include <string>
#include <vector>

namespace seed::internal
{

// The kind used in the messages of every pak failure.
inline constexpr const char *PakKind = "resource pak";

// Called from inside a catch block: throws the LoadError that describes the
// exception being handled. A pak that breaks the format, or ends while it is
// read, is NotLoadable with a detail starting "damaged: "; a system refusal is
// NotLoadable with the system text; running out of memory says so. A LoadError
// passes through unchanged. A file that does not exist is NotFound when
// `missing_is_not_found` is set and NotLoadable otherwise (the search already
// found it, so it vanished or is unreadable).
[[noreturn]] void PakFailureThrow(const std::string &name, const std::string &file,
                                  const std::vector<LoadError::Location> &locations,
                                  bool missing_is_not_found);

// Fills `resource` with the bytes of `entry`, allocated at exactly the
// entry's size and read straight into place. Throws the exceptions of
// PakFile::ReadAt or std::bad_alloc.
void PakEntryRead(const PakFile &file, const PakEntry &entry, ecs::Resource &resource);

} // namespace seed::internal
