#pragma once

// Writer for compound files (the container of Windows installer packages).
//
// A package is written whole, from the model that CfbReader parsed, in one
// canonical layout. Nothing is patched in place, so every operation exercises
// the same code and the result depends only on the model and the signature:
// the same inputs always give the same bytes.
//
// Layout after the header (512 bytes, or 4096 for a version 4 file), each
// region made of whole sectors, in this order:
//
//   stream sectors   streams of at least the header's cut-off, one contiguous
//                    run each, in directory order
//   mini stream      every stream below the cut-off, in 64-byte mini sectors,
//                    one contiguous run each; the root entry records its start
//                    and its exact length (mini sectors times 64)
//   mini index       one entry per mini sector, padded with free entries
//   directory        the root, then each storage's entries next to each other;
//                    the last sector holds free entries only as padding
//   extended index   only when the allocation index needs more than 109
//                    sectors (the header lists the first 109)
//   allocation index the chains of every region above
//
// The number of allocation and extended index sectors is computed, not
// incremented. Where a stream is kept depends only on its size: below the
// cut-off in the mini stream, otherwise in ordinary sectors. A package with no
// stream below the cut-off has no mini stream and no mini index.
//
// The tree of each storage is rebuilt as a balanced search tree (all entries
// black) from the sequence of its entries, in the order they had. The two
// signature streams of the root are the only entries the writer places by the
// format's ordering: both are dropped, and a new \005DigitalSignature is
// inserted before the first entry that orders after it. Every other entry
// keeps its position relative to the others.
//
// Kept for every storage and stream: name, bytes, class identifier, state bits
// and the creation and modification times; and from the header: version,
// sector size, header class identifier and transaction signature. Dropped:
// sectors no chain refers to, free directory entries, the old mini stream's
// slack, tree colours and the signature streams.
//
// After writing, the buffer is parsed again and compared with what was meant to
// be written (names, kinds, class identifiers, state bits, times, bytes, and
// that a search of every storage finds every entry); a difference throws and
// nothing is returned.
//
// Rejections are std::runtime_error whose message starts with "MSI: ".

#include "CfbReader.hpp"

#include <cstdint>
#include <vector>

namespace seed::internal
{

// The highest sector number the format allows is 0xFFFFFFF9.
inline constexpr std::uint64_t kCfbMaxSectors = 0xFFFFFFFA;

// Serialises the package into a new buffer without its signature streams. When
// `signature` is not null (and not empty) it becomes the \005DigitalSignature
// stream. The old signature streams are never read.
//
// Throws when two entries of one storage have the same name by the format's
// ordering, when a stream cannot be read, or when the package would need more
// sectors than the format allows.
std::vector<std::uint8_t> WriteCfb(PackageModel &model, const std::vector<std::uint8_t> *signature);

} // namespace seed::internal
