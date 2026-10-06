#pragma once

// One function per fuzz target. Each takes the bytes of one input, runs every
// operation that applies to the format on it, and aborts the process when
// anything other than std::runtime_error escapes, when a message mentions an
// internal error, or when one call's peak heap growth exceeds ten times the
// input plus 16 MiB (plus the size of any signature handed to an embed call).
//
// Standard C++ only, so the same functions run inside the fuzz binaries and
// inside test_FuzzCorpus on every platform the tests are built for.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace seedfuzz
{

void FuzzElf(const std::uint8_t *data, std::size_t size);
void FuzzPe(const std::uint8_t *data, std::size_t size);
void FuzzMachO(const std::uint8_t *data, std::size_t size);
void FuzzSuperBlob(const std::uint8_t *data, std::size_t size);
void FuzzMsi(const std::uint8_t *data, std::size_t size);

// Well-formed inputs built in code: a small ELF shared object with two
// DT_NEEDED entries (any width and byte order) and a SuperBlob made by
// MachOSigner::BuildSuperBlob.
std::vector<std::uint8_t> ElfSeed(bool is64, bool little);
std::vector<std::uint8_t> SuperBlobSeed();

} // namespace seedfuzz
