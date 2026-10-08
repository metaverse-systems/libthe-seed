#include <libthe-seed/MachOSigner.hpp>

#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"
#include "internal/MachOLayout.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "../external/picosha2.h"

namespace {

using seed::internal::ByteOrder;
using seed::internal::ByteSpan;
using seed::internal::ContainerForm;
using seed::internal::GuardEntryPoint;
using seed::internal::MachOContainer;
using seed::internal::ContainerEntry;
using seed::internal::ParseMachOContainer;
using seed::internal::SliceLayout;
using seed::internal::SliceSupport;

constexpr const char *kProgramFormat = "Mach-O";

// Code Signing constants (big-endian on disk)
constexpr std::uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0; // SuperBlob
constexpr std::uint32_t CSMAGIC_CODEDIRECTORY      = 0xFADE0C02;
constexpr std::uint32_t CSMAGIC_REQUIREMENTS       = 0xFADE0C01;
constexpr std::uint32_t CSMAGIC_BLOBWRAPPER        = 0xFADE0B01;

constexpr std::uint32_t CSSLOT_CODEDIRECTORY = 0;
constexpr std::uint32_t CSSLOT_REQUIREMENTS  = 2;
constexpr std::uint32_t CSSLOT_CMS_SIGNATURE = 0x10000;

constexpr std::uint32_t CS_HASHTYPE_SHA256   = 2;
constexpr std::uint32_t CS_HASH_SIZE_SHA256  = 32;
constexpr std::uint32_t CS_PAGE_SIZE_LOG2    = 12; // 4096 bytes
constexpr std::uint64_t CS_PAGE_SIZE         = 4096;
constexpr std::uint32_t CS_EXECSEG_MAIN_BINARY = 1;

// CodeDirectory version that carries the executable segment fields
constexpr std::uint32_t CS_SUPPORTSEXECSEG   = 0x20400;
// Fixed part of that CodeDirectory, through execSegFlags.
constexpr std::uint64_t kCodeDirectoryFixedSize = 88;
// Special slots: -2 (requirements) and -1 (Info.plist, absent: zero).
constexpr std::uint32_t kSpecialSlots = 2;

constexpr std::uint32_t MH_EXECUTE = 2;
constexpr std::uint32_t CPU_TYPE_ARM64 = 0x0100000C;
constexpr std::uint32_t CPU_TYPE_X86_64 = 0x01000007;

constexpr std::uint32_t LC_CODE_SIGNATURE_CMD = 0x1D;
constexpr std::uint64_t kSignatureCommandSize = 16;

// The largest CMS capacity accepted.
constexpr std::uint32_t kMaxCapacity = 0x80000000u;

// Offsets inside a 64-bit segment command.
constexpr std::uint64_t kSegmentVmsizeField = 32;
constexpr std::uint64_t kSegmentFilesizeField = 48;

struct BlobIndex
{
    std::uint32_t type;
    std::uint32_t offset;
};

void WriteBE32(std::vector<std::uint8_t> &buf, std::size_t offset, std::uint32_t value)
{
    buf[offset]     = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    buf[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    buf[offset + 2] = static_cast<std::uint8_t>((value >> 8)  & 0xFF);
    buf[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

void WriteBE64(std::vector<std::uint8_t> &buf, std::size_t offset, std::uint64_t value)
{
    for(int i = 7; i >= 0; --i)
    {
        buf[offset + static_cast<std::size_t>(7 - i)] =
            static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

void WriteLE32(std::vector<std::uint8_t> &buf, std::size_t offset, std::uint32_t value)
{
    for(int i = 0; i < 4; ++i)
    {
        buf[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

void WriteLE64(std::vector<std::uint8_t> &buf, std::size_t offset, std::uint64_t value)
{
    for(int i = 0; i < 8; ++i)
    {
        buf[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

std::vector<std::uint8_t> HashSHA256(const std::uint8_t *data, std::size_t size)
{
    std::vector<std::uint8_t> hash(picosha2::k_digest_size);
    picosha2::hash256(data, data + size, hash.begin(), hash.end());
    return hash;
}

// Build the empty requirements blob: FADE0C01 0000000C 00000000
std::vector<std::uint8_t> BuildEmptyRequirements()
{
    std::vector<std::uint8_t> req(12, 0);
    WriteBE32(req, 0, CSMAGIC_REQUIREMENTS);
    WriteBE32(req, 4, 12);
    WriteBE32(req, 8, 0);
    return req;
}

std::uint64_t Align16(std::uint64_t value)
{
    return (value + 15) / 16 * 16;
}

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t unit)
{
    return (value + unit - 1) / unit * unit;
}

// Reads the file; a failure to read it begins with the format name.
std::vector<std::uint8_t> ReadInput(const std::string &path)
{
    try
    {
        return ReadFileBytes(path);
    }
    catch(const std::runtime_error &error)
    {
        seed::internal::ThrowMalformed(kProgramFormat, error.what());
    }
}

// Reads the layout of the file's programs; a file under four bytes cannot be one.
MachOContainer ParseInput(const std::vector<std::uint8_t> &bytes)
{
    if(bytes.size() < 4)
    {
        seed::internal::ThrowMalformed(kProgramFormat, "File too small to be a Mach-O binary");
    }
    return ParseMachOContainer(ByteSpan(bytes, kProgramFormat));
}

[[noreturn]] void Refuse(const std::string &path, const std::string &text)
{
    throw std::runtime_error(path + ": " + text);
}

// Refuses with the reason; in a universal file the slice is named first.
[[noreturn]] void RefuseSlice(const std::string &path, bool universal, std::size_t index,
                              std::uint32_t cputype, const std::string &reason)
{
    if(universal)
    {
        Refuse(path, "slice " + std::to_string(index) + " (" + seed::internal::ArchName(cputype) + "): " + reason);
    }
    Refuse(path, reason);
}

void RequireSupported(const std::string &path, const MachOContainer &container)
{
    for(std::size_t i = 0; i < container.entries.size(); ++i)
    {
        const ContainerEntry &entry = container.entries[i];
        if(entry.slice.support != SliceSupport::Supported)
        {
            RefuseSlice(path, container.form != ContainerForm::Thin, i, entry.cputype,
                        seed::internal::DeclineText(entry.slice.support));
        }
    }
}

// The point where the old signature data begins, after checking that it is the
// last thing in the program: it ends where the program ends. Anything else would
// have to be moved to make the new signature fit, which is refused.
template <typename RefuseFn>
std::uint64_t RequireSignatureAtEnd(const RefuseFn &refuse, const SliceLayout &layout)
{
    const seed::internal::CodeSignatureLayout &sig = *layout.codesig;
    if(sig.dataoff == 0 && sig.datasize == 0)
    {
        return layout.size; // a command that points at nothing
    }
    const std::uint64_t end = static_cast<std::uint64_t>(sig.dataoff) + sig.datasize;
    if(end != layout.size)
    {
        const std::uint64_t follow = end < layout.size ? layout.size - end : 0;
        refuse("the signature data is not at the end of the program (" + std::to_string(follow) +
               " bytes follow it); signing would have to move them. Re-link or strip the signature "
               "with another tool first.");
    }
    return sig.dataoff;
}

// One program as it will be after signing: the bytes with the signature region
// reserved (zero), and the CodeDirectory computed over the bytes before it.
struct SlicePlan
{
    std::uint32_t cpu_type = 0;
    std::uint32_t cpu_subtype = 0;
    std::vector<std::uint8_t> bytes;
    std::uint64_t dataoff = 0;
    std::uint64_t datasize = 0;
    std::vector<std::uint8_t> code_directory;
    std::vector<std::uint8_t> cd_hash;
};

std::uint64_t SuperBlobFixedSize()
{
    // header (12) + three index entries (24) + requirements (12) + CMS wrapper header (8)
    return 12 + 3 * sizeof(BlobIndex) + 12 + 8;
}

// Builds the finished layout of one slice in memory and its CodeDirectory.
SlicePlan PlanSlice(const std::string &path, bool universal, std::size_t index, const ByteSpan &file,
                    const ContainerEntry &entry, const std::string &identity, std::uint32_t capacity)
{
    const SliceLayout &layout = entry.slice;
    const auto refuse = [&](const std::string &reason) {
        RefuseSlice(path, universal, index, entry.cputype, reason);
    };

    // An existing signature is replaced in place: its command stays where it is
    // and the old data is cut off. That is only possible when nothing follows it.
    const bool resign = layout.codesig.has_value();
    std::uint64_t kept = layout.size; // bytes of the program that stay
    if(resign)
    {
        kept = RequireSignatureAtEnd(refuse, layout);
    }
    if(!layout.linkedit.has_value() || layout.linkedit->fileoff > kept ||
       layout.linkedit->fileoff + layout.linkedit->filesize != layout.size)
    {
        refuse("the program has no __LINKEDIT segment at its end; it cannot be signed");
    }
    if(!resign && layout.FreeHeaderSpace() < kSignatureCommandSize)
    {
        refuse("no room for the code signature command: 16 bytes needed, " +
               std::to_string(layout.FreeHeaderSpace()) + " available between the end of the load commands (offset " +
               std::to_string(layout.header_end) + ") and the first section (offset " +
               std::to_string(layout.first_content) +
               "); relink with extra header space (for example -headerpad 0x20)");
    }
    if(!resign && layout.ncmds == UINT32_MAX)
    {
        refuse("the program has too many load commands to add one");
    }
    if(identity.find('\0') != std::string::npos)
    {
        refuse("the identity contains a NUL byte");
    }

    // Sizes first: none of them depends on a hash.
    const std::uint64_t dataoff = Align16(kept);
    const std::uint64_t n_code_slots = (dataoff + CS_PAGE_SIZE - 1) / CS_PAGE_SIZE;
    const std::uint64_t ident_size = identity.size() + 1;
    const std::uint64_t hash_offset = kCodeDirectoryFixedSize + ident_size;
    const std::uint64_t cd_size = hash_offset + (kSpecialSlots + n_code_slots) * CS_HASH_SIZE_SHA256;
    const std::uint64_t datasize = Align16(SuperBlobFixedSize() + cd_size + capacity);
    if(dataoff + datasize > UINT32_MAX || cd_size > UINT32_MAX)
    {
        refuse("the signature would end at offset " + std::to_string(dataoff + datasize) +
               ", past the 4294967295 bytes that the signature command can describe");
    }

    SlicePlan plan;
    plan.cpu_type = entry.cputype;
    plan.cpu_subtype = entry.cpusubtype;
    plan.dataoff = dataoff;
    plan.datasize = datasize;
    std::vector<std::uint8_t> &out = plan.bytes;
    out.assign(file.Data() + entry.offset, file.Data() + entry.offset + kept);
    out.resize(static_cast<std::size_t>(dataoff + datasize), 0);

    // The signature command after the last one, and the counts that cover it.
    const std::size_t command_at = static_cast<std::size_t>(resign ? layout.codesig->command_offset
                                                                    : layout.header_end);
    // An existing command keeps its size, which may be larger than 16 bytes.
    if(!resign)
    {
        WriteLE32(out, command_at, LC_CODE_SIGNATURE_CMD);
        WriteLE32(out, command_at + 4, static_cast<std::uint32_t>(kSignatureCommandSize));
    }
    WriteLE32(out, command_at + 8, static_cast<std::uint32_t>(dataoff));
    WriteLE32(out, command_at + 12, static_cast<std::uint32_t>(datasize));
    if(!resign)
    {
        WriteLE32(out, 16, layout.ncmds + 1);
        WriteLE32(out, 20, static_cast<std::uint32_t>(layout.sizeofcmds + kSignatureCommandSize));
    }

    // __LINKEDIT ends where the signature region ends.
    const std::uint64_t page = entry.cputype == CPU_TYPE_ARM64 ? 16384 : 4096;
    const std::uint64_t linkedit_size = dataoff + datasize - layout.linkedit->fileoff;
    WriteLE64(out, static_cast<std::size_t>(layout.linkedit->command_offset + kSegmentFilesizeField),
              linkedit_size);
    WriteLE64(out, static_cast<std::size_t>(layout.linkedit->command_offset + kSegmentVmsizeField),
              AlignUp(linkedit_size, page));

    // CodeDirectory over everything before the signature data.
    const auto empty_req = BuildEmptyRequirements();
    const auto req_hash = HashSHA256(empty_req.data(), empty_req.size());
    std::vector<std::uint8_t> cd(static_cast<std::size_t>(cd_size), 0);
    WriteBE32(cd, 0, CSMAGIC_CODEDIRECTORY);
    WriteBE32(cd, 4, static_cast<std::uint32_t>(cd_size));
    WriteBE32(cd, 8, CS_SUPPORTSEXECSEG);
    WriteBE32(cd, 12, 0); // flags
    WriteBE32(cd, 16, static_cast<std::uint32_t>(hash_offset + kSpecialSlots * CS_HASH_SIZE_SHA256));
    WriteBE32(cd, 20, static_cast<std::uint32_t>(kCodeDirectoryFixedSize)); // identOffset
    WriteBE32(cd, 24, kSpecialSlots);
    WriteBE32(cd, 28, static_cast<std::uint32_t>(n_code_slots));
    WriteBE32(cd, 32, static_cast<std::uint32_t>(dataoff)); // codeLimit
    cd[36] = CS_HASH_SIZE_SHA256;
    cd[37] = CS_HASHTYPE_SHA256;
    cd[38] = 0; // platform
    cd[39] = CS_PAGE_SIZE_LOG2;
    if(layout.text.has_value())
    {
        WriteBE64(cd, 64, layout.text->fileoff);  // execSegBase
        WriteBE64(cd, 72, layout.text->filesize); // execSegLimit
    }
    WriteBE64(cd, 80, layout.filetype == MH_EXECUTE ? CS_EXECSEG_MAIN_BINARY : 0); // execSegFlags
    std::memcpy(cd.data() + kCodeDirectoryFixedSize, identity.c_str(), identity.size() + 1);
    // Slot -2 (requirements) comes first, then slot -1 (Info.plist, zero).
    std::memcpy(cd.data() + hash_offset, req_hash.data(), CS_HASH_SIZE_SHA256);
    const std::size_t code_hashes = static_cast<std::size_t>(hash_offset + kSpecialSlots * CS_HASH_SIZE_SHA256);
    for(std::uint64_t i = 0; i < n_code_slots; ++i)
    {
        const std::uint64_t start = i * CS_PAGE_SIZE;
        const std::uint64_t end = std::min<std::uint64_t>(start + CS_PAGE_SIZE, dataoff);
        const auto hash = HashSHA256(out.data() + start, static_cast<std::size_t>(end - start));
        std::memcpy(cd.data() + code_hashes + i * CS_HASH_SIZE_SHA256, hash.data(), CS_HASH_SIZE_SHA256);
    }
    plan.cd_hash = HashSHA256(cd.data(), cd.size());
    plan.code_directory = std::move(cd);
    return plan;
}

// The bytes between two slices of a universal file are rewritten as zero padding
// when slice lengths change, so they must be zero already; anything else would be
// lost. (Bytes before the first slice and after the last are kept as they are.)
void RequireZeroPadding(const std::string &path, const ByteSpan &file, const MachOContainer &container)
{
    if(container.form == ContainerForm::Thin)
    {
        return;
    }
    std::vector<std::size_t> order(container.entries.size());
    for(std::size_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return container.entries[a].offset < container.entries[b].offset;
    });
    for(std::size_t k = 1; k < order.size(); ++k)
    {
        const ContainerEntry &previous = container.entries[order[k - 1]];
        const ContainerEntry &next = container.entries[order[k]];
        for(std::uint64_t at = previous.offset + previous.size; at < next.offset; ++at)
        {
            if(file.Data()[at] != 0)
            {
                Refuse(path, "the bytes between slice " + std::to_string(order[k - 1]) + " (" +
                                 seed::internal::ArchName(previous.cputype) + ") and slice " +
                                 std::to_string(order[k]) + " (" + seed::internal::ArchName(next.cputype) +
                                 ") are not all zero (the first is at offset " + std::to_string(at) +
                                 "); they would be replaced by zero padding");
            }
        }
    }
}

std::vector<SlicePlan> PlanAll(const std::string &path, const ByteSpan &file, const MachOContainer &container,
                               const std::string &identity, std::uint32_t capacity)
{
    if(capacity > kMaxCapacity)
    {
        Refuse(path, "the capacity " + std::to_string(capacity) +
                         " is larger than the 2147483648 bytes that can be reserved");
    }
    RequireSupported(path, container);
    RequireZeroPadding(path, file, container);
    const bool universal = container.form != ContainerForm::Thin;
    std::vector<SlicePlan> plans;
    for(std::size_t i = 0; i < container.entries.size(); ++i)
    {
        plans.push_back(PlanSlice(path, universal, i, file, container.entries[i], identity, capacity));
    }
    return plans;
}

// Puts the finished slices into a universal file: the table is kept in form and
// order, offsets are aligned and ascending, lengths are rewritten.
std::vector<std::uint8_t> AssembleUniversal(const std::string &path, const ByteSpan &file,
                                            const MachOContainer &container,
                                            const std::vector<std::vector<std::uint8_t>> &slices)
{
    const bool wide = container.form == ContainerForm::Fat64;
    const std::size_t entry_size = wide ? 32 : 20;
    const std::size_t count = container.entries.size();

    std::vector<std::size_t> order(count);
    for(std::size_t i = 0; i < count; ++i)
    {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return container.entries[a].offset < container.entries[b].offset;
    });

    std::vector<std::uint64_t> offsets(count);
    std::uint64_t cursor = container.entries[order[0]].offset;
    std::uint64_t old_end = 0;
    for(const std::size_t i : order)
    {
        const ContainerEntry &entry = container.entries[i];
        if(entry.align >= 32)
        {
            Refuse(path, "slice " + std::to_string(i) + " (" + seed::internal::ArchName(entry.cputype) +
                             "): alignment 2^" + std::to_string(entry.align) + " is not supported");
        }
        offsets[i] = AlignUp(cursor, std::uint64_t{1} << entry.align);
        cursor = offsets[i] + slices[i].size();
        old_end = std::max(old_end, entry.offset + entry.size);
        if(!wide && cursor > UINT32_MAX)
        {
            Refuse(path, "slice " + std::to_string(i) + " (" + seed::internal::ArchName(entry.cputype) + ") would end at offset " +
                             std::to_string(cursor) + ", past what a 32-bit universal table can describe");
        }
    }

    const std::uint64_t first = container.entries[order[0]].offset;
    std::vector<std::uint8_t> out(file.Data(), file.Data() + first);
    for(const std::size_t i : order)
    {
        out.resize(static_cast<std::size_t>(offsets[i]), 0);
        out.insert(out.end(), slices[i].begin(), slices[i].end());
    }
    // Bytes after the last slice are kept as they were.
    out.insert(out.end(), file.Data() + old_end, file.Data() + file.Size());

    for(std::size_t i = 0; i < count; ++i)
    {
        const std::size_t at = 8 + i * entry_size;
        if(wide)
        {
            WriteBE64(out, at + 8, offsets[i]);
            WriteBE64(out, at + 16, slices[i].size());
        }
        else
        {
            WriteBE32(out, at + 8, static_cast<std::uint32_t>(offsets[i]));
            WriteBE32(out, at + 12, static_cast<std::uint32_t>(slices[i].size()));
        }
    }
    return out;
}

// The signature region of a slice, or nullopt when the slice has none.
std::optional<std::vector<std::uint8_t>> SignatureOfSlice(const std::string &path, const ByteSpan &file,
                                                          const MachOContainer &container, std::size_t index)
{
    const ContainerEntry &entry = container.entries[index];
    if(entry.slice.support != SliceSupport::Supported)
    {
        RefuseSlice(path, container.form != ContainerForm::Thin, index, entry.cputype,
                    seed::internal::DeclineText(entry.slice.support));
    }
    if(!entry.slice.codesig.has_value() || entry.slice.codesig->dataoff == 0 ||
       entry.slice.codesig->datasize == 0)
    {
        return std::nullopt;
    }
    const ByteSpan region = file.Sub(entry.offset + entry.slice.codesig->dataoff, entry.slice.codesig->datasize,
                                     "LC_CODE_SIGNATURE data");
    std::uint64_t length = region.Size();
    if(region.Size() >= 12 &&
       region.Read<std::uint32_t>(0, ByteOrder::Big, "SuperBlob magic") == CSMAGIC_EMBEDDED_SIGNATURE)
    {
        const std::uint64_t own = region.Read<std::uint32_t>(4, ByteOrder::Big, "SuperBlob length");
        if(own >= 12 && own <= region.Size())
        {
            length = own;
        }
    }
    return std::vector<std::uint8_t>(region.Data(), region.Data() + length);
}

} // anonymous namespace

MachOSigner::PreparedSignature MachOSigner::PrepareSignature(
    const std::string &file_path, const std::string &identity, std::uint32_t cms_capacity)
{
    return GuardEntryPoint(kProgramFormat, [&]() {
        const auto bytes = ReadInput(file_path);
        const ByteSpan file(bytes, kProgramFormat);
        const MachOContainer container = ParseInput(bytes);
        const std::vector<SlicePlan> plans = PlanAll(file_path, file, container, identity, cms_capacity);

        PreparedSignature prepared;
        prepared.identity = identity;
        prepared.cms_capacity = cms_capacity;
        for(const SlicePlan &plan : plans)
        {
            prepared.slices.push_back({plan.cpu_type, plan.cpu_subtype, plan.code_directory, plan.cd_hash});
        }
        return prepared;
    });
}

void MachOSigner::CompleteSignature(const std::string &file_path, const PreparedSignature &prepared,
                                    const std::vector<std::vector<std::uint8_t>> &cms_signatures)
{
    GuardEntryPoint(kProgramFormat, [&]() {
        const auto bytes = ReadInput(file_path);
        const ByteSpan file(bytes, kProgramFormat);
        const MachOContainer container = ParseInput(bytes);
        std::vector<SlicePlan> plans = PlanAll(file_path, file, container, prepared.identity, prepared.cms_capacity);

        // The CodeDirectories of the program as it is now must be the ones that were prepared.
        bool same = plans.size() == prepared.slices.size();
        for(std::size_t i = 0; same && i < plans.size(); ++i)
        {
            same = plans[i].cpu_type == prepared.slices[i].cpu_type &&
                   plans[i].cpu_subtype == prepared.slices[i].cpu_subtype &&
                   plans[i].code_directory == prepared.slices[i].code_directory &&
                   plans[i].cd_hash == prepared.slices[i].cd_hash;
        }
        if(!same)
        {
            Refuse(file_path, "the program, the identity or the capacity changed since PrepareSignature; "
                              "prepare again");
        }
        if(cms_signatures.size() != plans.size())
        {
            Refuse(file_path, std::to_string(cms_signatures.size()) + " signatures were given for " +
                                  std::to_string(plans.size()) + " slices");
        }
        for(const auto &cms : cms_signatures)
        {
            if(cms.size() > prepared.cms_capacity)
            {
                Refuse(file_path, "the signature is " + std::to_string(cms.size()) + " bytes but only " +
                                      std::to_string(prepared.cms_capacity) +
                                      " were reserved; prepare again with a larger capacity");
            }
        }

        std::vector<std::vector<std::uint8_t>> finished;
        for(std::size_t i = 0; i < plans.size(); ++i)
        {
            const auto blob = BuildSuperBlob(plans[i].code_directory, cms_signatures[i]);
            std::vector<std::uint8_t> &out = plans[i].bytes;
            std::copy(blob.begin(), blob.end(), out.begin() + static_cast<std::ptrdiff_t>(plans[i].dataoff));
            finished.push_back(std::move(out));
        }

        if(container.form == ContainerForm::Thin)
        {
            WriteFileBytes(file_path, finished[0]);
        }
        else
        {
            WriteFileBytes(file_path, AssembleUniversal(file_path, file, container, finished));
        }
    });
}

namespace {

// One slice without its signature: the command removed from the table (later
// commands move up, the freed bytes are zero), the counts lowered, the data cut
// off and __LINKEDIT ended at the cut.
std::vector<std::uint8_t> StripSlice(const std::string &path, bool universal, std::size_t index,
                                     const ByteSpan &file, const ContainerEntry &entry)
{
    const SliceLayout &layout = entry.slice;
    const auto refuse = [&](const std::string &reason) {
        RefuseSlice(path, universal, index, entry.cputype, reason);
    };
    const std::uint64_t cut = RequireSignatureAtEnd(refuse, layout);
    if(!layout.linkedit.has_value() || layout.linkedit->fileoff > cut)
    {
        refuse("the program has no __LINKEDIT segment at its end; it cannot be signed");
    }
    std::uint64_t cmdsize = kSignatureCommandSize;
    for(const auto &command : layout.commands)
    {
        if(command.offset == layout.codesig->command_offset)
        {
            cmdsize = command.cmdsize;
        }
    }
    std::vector<std::uint8_t> out(file.Data() + entry.offset, file.Data() + entry.offset + cut);
    const std::size_t at = static_cast<std::size_t>(layout.codesig->command_offset);
    const std::size_t table_end = static_cast<std::size_t>(layout.header_end);
    std::copy(out.begin() + static_cast<std::ptrdiff_t>(at + cmdsize),
              out.begin() + static_cast<std::ptrdiff_t>(table_end), out.begin() + static_cast<std::ptrdiff_t>(at));
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(table_end - cmdsize),
              out.begin() + static_cast<std::ptrdiff_t>(table_end), static_cast<std::uint8_t>(0));
    WriteLE32(out, 16, layout.ncmds - 1);
    WriteLE32(out, 20, static_cast<std::uint32_t>(layout.sizeofcmds - cmdsize));

    // The command that held the link-edit segment may have moved up.
    std::uint64_t segment_at = layout.linkedit->command_offset;
    if(segment_at > layout.codesig->command_offset)
    {
        segment_at -= cmdsize;
    }
    const std::uint64_t filesize = cut - layout.linkedit->fileoff;
    const std::uint64_t page = entry.cputype == CPU_TYPE_ARM64 ? 16384 : 4096;
    WriteLE64(out, static_cast<std::size_t>(segment_at + kSegmentFilesizeField), filesize);
    if(layout.linkedit->vmsize < filesize)
    {
        WriteLE64(out, static_cast<std::size_t>(segment_at + kSegmentVmsizeField), AlignUp(filesize, page));
    }
    return out;
}

} // anonymous namespace

void MachOSigner::StripSignature(const std::string &file_path)
{
    GuardEntryPoint(kProgramFormat, [&]() {
        const auto bytes = ReadInput(file_path);
        const ByteSpan file(bytes, kProgramFormat);
        const MachOContainer container = ParseInput(bytes);
        RequireSupported(file_path, container);
        RequireZeroPadding(file_path, file, container);
        const bool universal = container.form != ContainerForm::Thin;

        bool any = false;
        std::vector<std::vector<std::uint8_t>> slices;
        for(std::size_t i = 0; i < container.entries.size(); ++i)
        {
            const ContainerEntry &entry = container.entries[i];
            if(entry.slice.codesig.has_value())
            {
                any = true;
                slices.push_back(StripSlice(file_path, universal, i, file, entry));
            }
            else
            {
                slices.emplace_back(file.Data() + entry.offset, file.Data() + entry.offset + entry.slice.size);
            }
        }
        if(!any)
        {
            return; // nothing to remove: the file is not written
        }
        if(!universal)
        {
            WriteFileBytes(file_path, slices[0]);
        }
        else
        {
            WriteFileBytes(file_path, AssembleUniversal(file_path, file, container, slices));
        }
    });
}

std::vector<std::uint8_t> MachOSigner::BuildSuperBlob(
    const std::vector<std::uint8_t> &code_directory,
    const std::vector<std::uint8_t> &cms_signature)
{
    const auto requirements = BuildEmptyRequirements();

    // Build CMS BlobWrapper
    std::vector<std::uint8_t> cms_blob(8 + cms_signature.size());
    WriteBE32(cms_blob, 0, CSMAGIC_BLOBWRAPPER);
    WriteBE32(cms_blob, 4, static_cast<std::uint32_t>(cms_blob.size()));
    if(!cms_signature.empty())
    {
        std::memcpy(cms_blob.data() + 8, cms_signature.data(), cms_signature.size());
    }

    // SuperBlob: header (12 bytes) + 3 blob index entries (8 bytes each) + blobs
    const std::uint32_t n_blobs = 3;
    const std::size_t index_size = n_blobs * sizeof(BlobIndex);
    const std::size_t header_size = 12 + index_size;

    const std::size_t cd_offset = header_size;
    const std::size_t req_offset = cd_offset + code_directory.size();
    const std::size_t cms_offset = req_offset + requirements.size();
    const std::size_t total_size = cms_offset + cms_blob.size();

    std::vector<std::uint8_t> blob(total_size, 0);

    WriteBE32(blob, 0, CSMAGIC_EMBEDDED_SIGNATURE);
    WriteBE32(blob, 4, static_cast<std::uint32_t>(total_size));
    WriteBE32(blob, 8, n_blobs);

    WriteBE32(blob, 12, CSSLOT_CODEDIRECTORY);
    WriteBE32(blob, 16, static_cast<std::uint32_t>(cd_offset));
    WriteBE32(blob, 20, CSSLOT_REQUIREMENTS);
    WriteBE32(blob, 24, static_cast<std::uint32_t>(req_offset));
    WriteBE32(blob, 28, CSSLOT_CMS_SIGNATURE);
    WriteBE32(blob, 32, static_cast<std::uint32_t>(cms_offset));

    if(!code_directory.empty())
    {
        std::memcpy(blob.data() + cd_offset, code_directory.data(), code_directory.size());
    }
    std::memcpy(blob.data() + req_offset, requirements.data(), requirements.size());
    std::memcpy(blob.data() + cms_offset, cms_blob.data(), cms_blob.size());

    return blob;
}

std::vector<std::optional<std::vector<std::uint8_t>>> MachOSigner::ExtractSignatures(
    const std::string &file_path)
{
    return GuardEntryPoint(kProgramFormat, [&]() {
        const auto bytes = ReadInput(file_path);
        const ByteSpan file(bytes, kProgramFormat);
        const MachOContainer container = ParseInput(bytes);
        std::vector<std::optional<std::vector<std::uint8_t>>> out;
        for(std::size_t i = 0; i < container.entries.size(); ++i)
        {
            out.push_back(SignatureOfSlice(file_path, file, container, i));
        }
        return out;
    });
}

std::optional<std::vector<std::uint8_t>> MachOSigner::ExtractSignature(
    const std::string &file_path)
{
    return GuardEntryPoint(kProgramFormat, [&]() -> std::optional<std::vector<std::uint8_t>> {
        const auto bytes = ReadInput(file_path);
        const ByteSpan file(bytes, kProgramFormat);
        const MachOContainer container = ParseInput(bytes);
        // A universal file answers for its first slice that can be read; when none
        // can, the first slice is reported as declined.
        std::size_t index = 0;
        for(std::size_t i = 0; i < container.entries.size(); ++i)
        {
            if(container.entries[i].slice.support == SliceSupport::Supported)
            {
                index = i;
                break;
            }
        }
        return SignatureOfSlice(file_path, file, container, index);
    });
}

bool MachOSigner::HasEmbeddedSignature(const std::string &file_path)
{
    return GuardEntryPoint(kProgramFormat, [&]() {
        const auto bytes = ReadInput(file_path);
        const ByteSpan file(bytes, kProgramFormat);
        const MachOContainer container = ParseInput(bytes);
        for(std::size_t i = 0; i < container.entries.size(); ++i)
        {
            if(!SignatureOfSlice(file_path, file, container, i).has_value())
            {
                return false;
            }
        }
        return true;
    });
}

namespace
{

constexpr const char *kSignatureFormat = "Mach-O code signature";

// Locates the blob in `slot` of a SuperBlob and returns its byte range as
// (offset, length) within the SuperBlob, or nullopt when the SuperBlob is
// well formed but has no such slot. Everything else that is wrong with the
// data is a std::runtime_error.
std::optional<std::pair<std::uint64_t, std::uint64_t>> FindSuperBlobSlot(
    const std::vector<std::uint8_t> &super_blob, std::uint32_t slot, std::uint32_t blob_magic,
    const char *blob_name)
{
    using seed::internal::ByteOrder;
    using seed::internal::ByteSpan;
    using seed::internal::RangeFits;
    using seed::internal::ThrowMalformed;

    const ByteSpan data(super_blob, kSignatureFormat, "the SuperBlob");
    if(data.Size() < 12)
    {
        ThrowMalformed(kSignatureFormat, "SuperBlob header needs 12 bytes but only " +
                                             std::to_string(data.Size()) + " are present");
    }

    const auto magic = data.Read<std::uint32_t>(0, ByteOrder::Big, "SuperBlob magic");
    if(magic != CSMAGIC_EMBEDDED_SIGNATURE)
    {
        ThrowMalformed(kSignatureFormat, "SuperBlob magic is not the embedded signature magic");
    }

    const std::uint64_t length = data.Read<std::uint32_t>(4, ByteOrder::Big, "SuperBlob length");
    if(length < 12 || length > data.Size())
    {
        ThrowMalformed(kSignatureFormat, "SuperBlob length " + std::to_string(length) +
                                             " is smaller than its header or past the " +
                                             std::to_string(data.Size()) + " bytes present");
    }

    const std::uint64_t count = data.Read<std::uint32_t>(8, ByteOrder::Big, "SuperBlob count");
    const std::uint64_t index_end = 12 + count * 8;
    if(index_end > length)
    {
        ThrowMalformed(kSignatureFormat, "SuperBlob blob index of " + std::to_string(count) +
                                             " entries does not fit in the SuperBlob length " +
                                             std::to_string(length));
    }

    // Slots are looked up in the first `length` bytes only.
    const ByteSpan blob = data.Sub(0, length, "SuperBlob");
    std::optional<std::uint64_t> wanted_offset;
    for(std::uint64_t i = 0; i < count; ++i)
    {
        const std::uint64_t idx_offset = 12 + i * 8;
        const auto slot_type = blob.Read<std::uint32_t>(idx_offset, ByteOrder::Big, "blob index slot");
        const std::uint64_t blob_offset =
            blob.Read<std::uint32_t>(idx_offset + 4, ByteOrder::Big, "blob index offset");
        if(blob_offset < index_end)
        {
            ThrowMalformed(kSignatureFormat, "blob index " + std::to_string(i) + " offset " +
                                                 std::to_string(blob_offset) + " is inside the index (" +
                                                 std::to_string(index_end) + " bytes)");
        }
        if(!RangeFits(blob_offset, 8, length))
        {
            ThrowMalformed(kSignatureFormat, "blob index " + std::to_string(i) + " offset " +
                                                 std::to_string(blob_offset) +
                                                 " is past the end of the SuperBlob (" +
                                                 std::to_string(length) + " bytes)");
        }
        if(slot_type == slot && !wanted_offset.has_value())
        {
            wanted_offset = blob_offset;
        }
    }

    if(!wanted_offset.has_value())
    {
        return std::nullopt;
    }

    const std::uint64_t blob_offset = *wanted_offset;
    const auto inner_magic = blob.Read<std::uint32_t>(blob_offset, ByteOrder::Big, "blob magic");
    if(inner_magic != blob_magic)
    {
        ThrowMalformed(kSignatureFormat, std::string("blob magic of the ") + blob_name +
                                             " is not the expected value");
    }

    const std::uint64_t inner_length =
        blob.Read<std::uint32_t>(blob_offset + 4, ByteOrder::Big, "blob length");
    if(inner_length < 8 || !RangeFits(blob_offset, inner_length, length))
    {
        ThrowMalformed(kSignatureFormat, std::string("blob length ") + std::to_string(inner_length) +
                                             " of the " + blob_name +
                                             " is below 8 or runs past the SuperBlob");
    }
    return std::make_pair(blob_offset, inner_length);
}

} // anonymous namespace

std::optional<std::vector<std::uint8_t>> MachOSigner::ExtractCmsFromSuperBlob(
    const std::vector<std::uint8_t> &super_blob)
{
    return seed::internal::GuardEntryPoint(
        kSignatureFormat, [&]() -> std::optional<std::vector<std::uint8_t>> {
            const auto found =
                FindSuperBlobSlot(super_blob, CSSLOT_CMS_SIGNATURE, CSMAGIC_BLOBWRAPPER, "CMS blob");
            if(!found)
            {
                return std::nullopt;
            }
            const auto first = super_blob.begin() + static_cast<std::ptrdiff_t>(found->first + 8);
            const auto last = super_blob.begin() + static_cast<std::ptrdiff_t>(found->first + found->second);
            return std::vector<std::uint8_t>(first, last);
        });
}

std::optional<std::vector<std::uint8_t>> MachOSigner::ExtractCodeDirectoryFromSuperBlob(
    const std::vector<std::uint8_t> &super_blob)
{
    return seed::internal::GuardEntryPoint(
        kSignatureFormat, [&]() -> std::optional<std::vector<std::uint8_t>> {
            const auto found = FindSuperBlobSlot(super_blob, CSSLOT_CODEDIRECTORY,
                                                 CSMAGIC_CODEDIRECTORY, "code directory");
            if(!found)
            {
                return std::nullopt;
            }
            const auto first = super_blob.begin() + static_cast<std::ptrdiff_t>(found->first);
            const auto last = super_blob.begin() + static_cast<std::ptrdiff_t>(found->first + found->second);
            return std::vector<std::uint8_t>(first, last);
        });
}
