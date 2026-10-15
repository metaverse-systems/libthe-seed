#pragma once

#include "PakFile.hpp"
#include "PakIndex.hpp"

#include <libthe-seed/LoadError.hpp>

#include <libecs-cpp/ecs.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace seed::internal
{

// The kind used in the messages of every pak failure.
inline constexpr const char *PakKind = "resource pak";

// Thrown by PakEntryRead when the memory for one resource cannot be had. The
// text names the resource and its size.
class PakNoMemory : public std::runtime_error
{
  public:
    PakNoMemory(const std::string &name, std::uint64_t size)
        : std::runtime_error("not enough memory to read resource \"" + name + "\" (" + std::to_string(size) +
                             " bytes)")
    {
    }
};

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
// entry's size and read straight into place. Throws PakNoMemory, PakDamaged
// when the file ends before the resource does (it was changed after it was
// checked) and the other exceptions of PakFile::ReadAt.
void PakEntryRead(const PakFile &file, const PakEntry &entry, ecs::Resource &resource);

} // namespace seed::internal
