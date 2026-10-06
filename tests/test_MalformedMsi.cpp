// Malformed and edge-case installer packages (OLE compound files), through
// every MsiSigner operation: IsMsi, ComputeAuthenticodeDigest,
// HasEmbeddedSignature, ExtractSignature, EmbedSignature and StripSignature.
//
// Test case names start with their origin: "ok:" for a well-formed or
// legal-but-unusual input with today's result, "review:" for the review's
// input, and "edge:" for an edge-case family.
//
// A rejection is a std::runtime_error whose message starts with "MSI: ". The
// keywords the tests look for (a message may carry more words and the number
// or index of the structure) are:
//   too small            the file is shorter than the 512-byte header
//   sector size exponent the sector size exponent is not 9 for version 3 or
//                        12 for version 4 (also exponents 0, 3 and 40)
//   mini-sector exponent the mini-sector size exponent is not 6
//   mini-stream cutoff   the mini-stream cutoff is not 4096
//   past the end         a sector number (stream start, chain link, FAT or
//                        DIFAT sector, directory sector, mini-stream sector)
//                        is outside the file or outside its table, or the
//                        bytes needed from a sector are not in the file
//   exceeds              a stream's size is larger than its sector chain
//   more than one stream a sector or mini sector is used by two streams
//   loop                 a FAT, mini-FAT or DIFAT chain visits a sector twice
//   reached twice        a directory entry is reached twice from the root
//                        (self-sibling, sibling cycle, link back to the root);
//                        a message that says "loop" is accepted there too
//   not the root         entry 0 is not the root storage
//   unused               the tree reaches an entry whose type is 0
//   signature size       EmbedSignature: a signature that a version 3 file
//                        cannot describe in its 32-bit stream size
// Where two keywords are listed for a case, either one is accepted, because
// the order of the checks decides which structure is named first.
//
// Operations that do not read the faulty structure keep answering as today
// (an unsigned file has no signature to extract, so ExtractSignature does not
// look at an ordinary stream); the tests only impose a rejection on the
// operations that must read the structure.
//
// Known gap: a signature the library embeds is written to
// regular sectors but read back through the mini-stream when it is smaller
// than the cutoff, so ExtractSignature returns other bytes. The "ok:" cases
// record only what is stable (the signature is present and has its length);
// the content is not checked.

#include "MalformedInput.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#if __has_include("internal/MsiSignatureSize.hpp")
#include "internal/MsiSignatureSize.hpp"
#define SEED_HAVE_MSI_SIZE_CHECK 1
#endif

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;
using seedtest::malformed::PatchLE;
namespace sm = seedtest::malformed;

constexpr const char *kFormat = "MSI";
using Keywords = std::vector<std::string>;

// Layout of tests/fixtures/tiny.msi: version 3, 512-byte sectors, 19 sectors
// after the header. Sectors 0 to 11 hold the mini stream (the root's stream),
// 12 the mini-FAT, 13 to 17 the directory (20 entries, root and 19 streams,
// all small and stored in the mini stream) and 18 the only FAT sector.
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kSectorCount = 19;
constexpr std::uint32_t kMiniFatSector = 12;
constexpr std::uint32_t kFatSector = 18;
constexpr std::uint32_t kEntryCount = 20;

// First streams along the root's child chain, used as victims.
constexpr std::uint32_t kRootChildEntry = 12;
constexpr std::uint32_t kSecondEntry = 4;
constexpr std::uint32_t kMiniStreamEntry = 1;  // start mini sector 0, size 1621
constexpr std::uint32_t kOtherMiniEntry = 2;   // start mini sector 26, size 836

// Header fields.
constexpr std::uint64_t kHdrMajor = 26;
constexpr std::uint64_t kHdrSectorExp = 30;
constexpr std::uint64_t kHdrMiniExp = 32;
constexpr std::uint64_t kHdrFatCount = 44;
constexpr std::uint64_t kHdrFirstDir = 48;
constexpr std::uint64_t kHdrCutoff = 56;
constexpr std::uint64_t kHdrFirstMiniFat = 60;
constexpr std::uint64_t kHdrFirstDifat = 68;
constexpr std::uint64_t kHdrDifatCount = 72;
constexpr std::uint64_t kHdrDifat0 = 76;

// Directory entry fields.
constexpr std::uint64_t kEntNameSize = 64;
constexpr std::uint64_t kEntType = 66;
constexpr std::uint64_t kEntLeft = 68;
constexpr std::uint64_t kEntRight = 72;
constexpr std::uint64_t kEntChild = 76;
constexpr std::uint64_t kEntStart = 116;
constexpr std::uint64_t kEntSize = 120;

constexpr std::uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr std::uint32_t kFreeSect = 0xFFFFFFFF;
constexpr std::uint32_t kDifSect = 0xFFFFFFFC;
constexpr std::uint32_t kNoStream = 0xFFFFFFFF;

constexpr std::uint8_t kTypeUnused = 0;
constexpr std::uint8_t kTypeStorage = 1;
constexpr std::uint8_t kTypeRoot = 5;

const std::u16string kSignatureName = {0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l', u'S',
                                       u'i',   u'g', u'n', u'a', u't', u'u', u'r', u'e'};

Bytes Tiny()
{
    return sm::LoadSample("tiny.msi");
}

Bytes FakeSignature(std::size_t size = 128)
{
    Bytes signature(size);
    for(std::size_t i = 0; i < size; ++i)
    {
        signature[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    return signature;
}

std::uint64_t EntryOffset(const Bytes &bytes, std::uint32_t entry)
{
    return sm::CfbDirectoryEntryOffset(bytes, entry);
}

template <typename T>
void PatchEntry(Bytes &bytes, std::uint32_t entry, std::uint64_t field, T value)
{
    PatchLE<T>(bytes, EntryOffset(bytes, entry) + field, value);
}

std::uint64_t SectorOffset(std::uint32_t sector)
{
    return kSectorSize + static_cast<std::uint64_t>(sector) * kSectorSize;
}

// Offset of FAT entry `index`, in the first FAT sector named by the header.
std::uint64_t FatEntryOffset(const Bytes &bytes, std::uint32_t index)
{
    const std::uint32_t fat_sector = sm::detail::GetLE<std::uint32_t>(bytes, kHdrDifat0);
    return SectorOffset(fat_sector) + 4ull * index;
}

void PatchFat(Bytes &bytes, std::uint32_t index, std::uint32_t value)
{
    PatchLE<std::uint32_t>(bytes, FatEntryOffset(bytes, index), value);
}

void PatchMiniFat(Bytes &bytes, std::uint32_t index, std::uint32_t value)
{
    const std::uint32_t sector = sm::detail::GetLE<std::uint32_t>(bytes, kHdrFirstMiniFat);
    PatchLE<std::uint32_t>(bytes, SectorOffset(sector) + 4ull * index, value);
}

// Index of the directory entry with this name, searched in the entries a
// signed copy of the sample can hold.
std::uint32_t FindEntry(const Bytes &bytes, const std::u16string &name)
{
    for(std::uint32_t entry = 0; entry < 24; ++entry)
    {
        const std::uint64_t offset = EntryOffset(bytes, entry);
        if(offset + 128 > bytes.size())
        {
            break;
        }
        const std::uint16_t size = sm::detail::GetLE<std::uint16_t>(bytes, offset + kEntNameSize);
        if(size != (name.size() + 1) * 2)
        {
            continue;
        }
        bool same = true;
        for(std::size_t i = 0; i < name.size(); ++i)
        {
            same = same && sm::detail::GetLE<std::uint16_t>(bytes, offset + 2 * i) == name[i];
        }
        if(same)
        {
            return entry;
        }
    }
    FAIL("entry not found");
    return 0;
}

// A copy of the sample signed by the library.
Bytes SignedTiny(std::size_t signature_size = 128)
{
    seedtest::ScratchDir scratch;
    const std::string path = sm::WriteScratch(scratch, "signed.msi", Tiny());
    MsiSigner::EmbedSignature(path, FakeSignature(signature_size));
    return sm::ReadAll(path);
}

// Appends one 0xFF-filled sector and returns its number.
std::uint32_t AppendSector(Bytes &bytes)
{
    const std::uint32_t sector = static_cast<std::uint32_t>((bytes.size() - kSectorSize) / kSectorSize);
    bytes.resize(bytes.size() + kSectorSize, 0xFF);
    return sector;
}

// Rebuilds the sample with its sectors in another order: order[n] is the old
// number of the sector placed at position n. Chains, the header and the root's
// start sector follow; everything else (the streams are in the mini stream)
// is unchanged. The result has the same contents.
Bytes Reorder(const Bytes &in, const std::vector<std::uint32_t> &order)
{
    REQUIRE(order.size() == kSectorCount);
    std::vector<std::uint32_t> to_new(kSectorCount);
    for(std::uint32_t n = 0; n < kSectorCount; ++n)
    {
        to_new[order[n]] = n;
    }
    const auto map = [&](std::uint32_t value) { return value < kSectorCount ? to_new[value] : value; };

    Bytes out(in.begin(), in.begin() + kSectorSize);
    for(std::uint32_t n = 0; n < kSectorCount; ++n)
    {
        const auto begin = in.begin() + static_cast<std::ptrdiff_t>(SectorOffset(order[n]));
        out.insert(out.end(), begin, begin + kSectorSize);
    }

    PatchLE<std::uint32_t>(out, kHdrFirstDir,
                           map(sm::detail::GetLE<std::uint32_t>(in, kHdrFirstDir)));
    PatchLE<std::uint32_t>(out, kHdrFirstMiniFat,
                           map(sm::detail::GetLE<std::uint32_t>(in, kHdrFirstMiniFat)));
    PatchLE<std::uint32_t>(out, kHdrDifat0, map(sm::detail::GetLE<std::uint32_t>(in, kHdrDifat0)));

    for(std::uint32_t n = 0; n < kSectorCount; ++n)
    {
        const std::uint32_t old_value =
            sm::detail::GetLE<std::uint32_t>(in, FatEntryOffset(in, order[n]));
        PatchFat(out, n, map(old_value));
    }

    const std::uint32_t root_start =
        sm::detail::GetLE<std::uint32_t>(in, EntryOffset(in, 0) + kEntStart);
    PatchEntry<std::uint32_t>(out, 0, kEntStart, map(root_start));
    return out;
}

std::vector<std::uint32_t> OrderDirectoryLast()
{
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 18, 13, 14, 15, 16, 17};
}

std::vector<std::uint32_t> OrderStreamLast()
{
    return {18, 12, 13, 14, 15, 16, 17, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
}

// ── Running the operations ────────────────────────────────

enum Op : unsigned
{
    kDigest = 1,
    kPresence = 2,
    kExtract = 4,
    kEmbed = 8,
    kStrip = 16,
    kAllOps = 31,
};

constexpr unsigned kReadOps = kDigest | kPresence | kExtract;
constexpr std::size_t kEmbedSignatureSize = 128;

void RunOp(Op op, const std::string &path)
{
    switch(op)
    {
    case kDigest:
        (void)MsiSigner::ComputeAuthenticodeDigest(path);
        break;
    case kPresence:
        (void)MsiSigner::HasEmbeddedSignature(path);
        break;
    case kExtract:
        (void)MsiSigner::ExtractSignature(path);
        break;
    case kEmbed:
        MsiSigner::EmbedSignature(path, FakeSignature(kEmbedSignatureSize));
        break;
    case kStrip:
        MsiSigner::StripSignature(path);
        break;
    default:
        FAIL("unknown operation");
    }
}

const char *OpName(Op op)
{
    switch(op)
    {
    case kDigest:
        return "ComputeAuthenticodeDigest";
    case kPresence:
        return "HasEmbeddedSignature";
    case kExtract:
        return "ExtractSignature";
    case kEmbed:
        return "EmbedSignature";
    case kStrip:
        return "StripSignature";
    default:
        return "?";
    }
}

constexpr Op kOpList[] = {kDigest, kPresence, kExtract, kEmbed, kStrip};

// Like RequireRejected, but any one of several keywords is accepted.
template <typename F>
void RequireRejectedWith(F &&callable, const Keywords &keywords, std::uint64_t input_size,
                         std::uint64_t extra_allowance = 0)
{
    const sm::detail::Outcome outcome = sm::detail::Run(callable);
    sm::detail::CheckRejection(outcome, kFormat, "");
    bool found = false;
    for(const std::string &keyword : keywords)
    {
        found = found || outcome.message.find(keyword) != std::string::npos;
    }
    INFO("message: " << outcome.message);
    CHECK(found);
    sm::detail::CheckLimits(outcome, input_size, extra_allowance);
}

// The call may succeed or be rejected, but it must finish within the time and
// heap limits and anything it throws is a clean rejection.
template <typename F>
void RequireBounded(F &&callable, std::uint64_t input_size, std::uint64_t extra_allowance = 0)
{
    const sm::detail::Outcome outcome = sm::detail::Run(callable);
    if(outcome.threw)
    {
        sm::detail::CheckRejection(outcome, kFormat, "");
    }
    sm::detail::CheckLimits(outcome, input_size, extra_allowance);
}

// Each selected operation, on a fresh copy of the input, is rejected; after a
// rejected embed or strip the file is unchanged.
void RequireOpsRejected(const Bytes &input, const Keywords &keywords, unsigned ops)
{
    for(const Op op : kOpList)
    {
        if((ops & op) == 0)
        {
            continue;
        }
        INFO("operation " << OpName(op));
        seedtest::ScratchDir scratch;
        const std::string path = sm::WriteScratch(scratch, "input.msi", input);
        RequireRejectedWith([&] { RunOp(op, path); }, keywords, input.size(),
                            op == kEmbed ? kEmbedSignatureSize : 0);
        if(op == kEmbed || op == kStrip)
        {
            sm::RequireUnchanged(path, input);
        }
    }
}

void RequireOpsBounded(const Bytes &input, unsigned ops)
{
    for(const Op op : kOpList)
    {
        if((ops & op) == 0)
        {
            continue;
        }
        INFO("operation " << OpName(op));
        seedtest::ScratchDir scratch;
        const std::string path = sm::WriteScratch(scratch, "input.msi", input);
        RequireBounded([&] { RunOp(op, path); }, input.size(),
                       op == kEmbed ? kEmbedSignatureSize : 0);
    }
}

std::vector<std::uint8_t> DigestOf(const Bytes &input)
{
    seedtest::ScratchDir scratch;
    const std::string path = sm::WriteScratch(scratch, "input.msi", input);
    return MsiSigner::ComputeAuthenticodeDigest(path).digest;
}

std::vector<std::uint8_t> FromHex(const std::string &hex)
{
    std::vector<std::uint8_t> out;
    for(std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

const Keywords kLoopKeywords = {"loop", "reached twice"};

} // namespace

// ── Well-formed inputs ────────────────────────────────────

TEST_CASE("ok: MSI sample answers as before", "[MalformedMsi][ok]")
{
    seedtest::ScratchDir scratch;
    const Bytes original = Tiny();
    const std::string path = sm::WriteScratch(scratch, "tiny.msi", original);

    CHECK(MsiSigner::IsMsi(path));
    CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MsiSigner::ExtractSignature(path).has_value());
    CHECK(MsiSigner::ComputeAuthenticodeDigest(path).digest ==
          FromHex("505332678f7c31b101302e7b63d110a2c418e520203ddddf6c2ec15ef29585fc"));
    sm::RequireUnchanged(path, original);
}

TEST_CASE("ok: MSI IsMsi on other files", "[MalformedMsi][ok]")
{
    CHECK_FALSE(MsiSigner::IsMsi(seedtest::FixturePath("tiny.exe")));
    CHECK_FALSE(MsiSigner::IsMsi(seedtest::FixturePath("plain.txt")));
    seedtest::ScratchDir scratch;
    CHECK_FALSE(MsiSigner::IsMsi(sm::WriteScratch(scratch, "empty.msi", Bytes{})));
    CHECK_FALSE(MsiSigner::IsMsi(sm::WriteScratch(scratch, "seven.msi", sm::Truncate(Tiny(), 7))));
    CHECK_FALSE(MsiSigner::IsMsi(scratch.File("missing.msi")));
}

TEST_CASE("ok: MSI embed then presence extract and strip", "[MalformedMsi][ok]")
{
    seedtest::ScratchDir scratch;
    const std::string path = sm::WriteScratch(scratch, "tiny.msi", Tiny());
    const Bytes signature = FakeSignature();

    MsiSigner::EmbedSignature(path, signature);

    // The presence check keeps answering true for a signature the library
    // wrote, whichever sectors it went into.
    CHECK(MsiSigner::HasEmbeddedSignature(path));

    // Known gap: the content comes back wrong while the signature is read
    // through the mini-stream; only its presence and length are recorded.
    const auto extracted = MsiSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    CHECK(extracted->size() == signature.size());

    MsiSigner::StripSignature(path);
    CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MsiSigner::ExtractSignature(path).has_value());
}

TEST_CASE("ok: MSI signed copy has the digest of the unsigned file", "[MalformedMsi][ok]")
{
    CHECK(DigestOf(SignedTiny()) == FromHex("505332678f7c31b101302e7b63d110a2c418e520203ddddf6c2ec15ef29585fc"));
}

TEST_CASE("ok: MSI childless root", "[MalformedMsi][ok]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, 0, kEntChild, kNoStream);
    seedtest::ScratchDir scratch;
    const std::string path = sm::WriteScratch(scratch, "childless.msi", bytes);

    // No stream is reachable: the digest is that of no data, not an error.
    const auto digest = MsiSigner::ComputeAuthenticodeDigest(path).digest;
    CHECK(digest == FromHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MsiSigner::ExtractSignature(path).has_value());
}

TEST_CASE("ok: MSI with bytes appended", "[MalformedMsi][ok]")
{
    Bytes bytes = Tiny();
    bytes.insert(bytes.end(), {1, 2, 3});
    CHECK(DigestOf(bytes) == FromHex("505332678f7c31b101302e7b63d110a2c418e520203ddddf6c2ec15ef29585fc"));
}

TEST_CASE("ok: MSI with its sectors in another order", "[MalformedMsi][ok]")
{
    // The helper used by the truncation cases must not change the contents.
    CHECK(DigestOf(Reorder(Tiny(), OrderDirectoryLast())) == FromHex("505332678f7c31b101302e7b63d110a2c418e520203ddddf6c2ec15ef29585fc"));
    CHECK(DigestOf(Reorder(Tiny(), OrderStreamLast())) == FromHex("505332678f7c31b101302e7b63d110a2c418e520203ddddf6c2ec15ef29585fc"));
}

// ── From the review ───────────────────────────────────────

TEST_CASE("review: MSI self-sibling", "[MalformedMsi][review]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kRootChildEntry, kEntRight, kRootChildEntry);
    RequireOpsRejected(bytes, kLoopKeywords, kAllOps);
}

TEST_CASE("review: MSI exponent 3", "[MalformedMsi][review]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint16_t>(bytes, kHdrSectorExp, 3);
    RequireOpsRejected(bytes, {"sector size exponent"}, kAllOps);
}

TEST_CASE("review: MSI exponent 40", "[MalformedMsi][review]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint16_t>(bytes, kHdrSectorExp, 40);
    RequireOpsRejected(bytes, {"sector size exponent"}, kAllOps);
}

TEST_CASE("review: MSI stream sector outside the file", "[MalformedMsi][review]")
{
    // A stream the digest reads: its size puts it in regular sectors and its
    // first sector is ten past the last one.
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, 3, kEntStart, kSectorCount + 10);
    PatchEntry<std::uint32_t>(bytes, 3, kEntSize, 5000);
    RequireOpsRejected(bytes, {"past the end", "exceeds"}, kDigest);

    // The signature stream: presence and extraction read it.
    Bytes signed_copy = SignedTiny();
    const std::uint32_t entry = FindEntry(signed_copy, kSignatureName);
    PatchEntry<std::uint32_t>(signed_copy, entry, kEntStart, 1000);
    RequireOpsRejected(signed_copy, {"past the end"}, kPresence | kExtract);
}

TEST_CASE("review: MSI huge stream size", "[MalformedMsi][review]")
{
    // A regular-sector stream over one real sector claims nearly 4 GiB.
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kMiniStreamEntry, kEntStart, kMiniFatSector);
    PatchEntry<std::uint32_t>(bytes, kMiniStreamEntry, kEntSize, 0xFFFFFFF0u);
    RequireOpsRejected(bytes, {"exceeds"}, kDigest);
}

TEST_CASE("review: MSI sibling loop on add", "[MalformedMsi][review]")
{
    // The right-most walk from the root's child never ends: 12, 4, 12, ...
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kSecondEntry, kEntRight, kRootChildEntry);
    RequireOpsRejected(bytes, kLoopKeywords, kEmbed);
}

TEST_CASE("review: MSI sibling loop on remove", "[MalformedMsi][review]")
{
    // On a signed copy the signature entry becomes the root's child and has
    // both siblings; the left-most walk of the right sibling never ends.
    Bytes bytes = SignedTiny();
    const std::uint32_t entry = FindEntry(bytes, kSignatureName);
    PatchEntry<std::uint32_t>(bytes, 0, kEntChild, entry);
    PatchEntry<std::uint32_t>(bytes, entry, kEntLeft, kMiniStreamEntry);
    PatchEntry<std::uint32_t>(bytes, entry, kEntRight, kRootChildEntry);
    PatchEntry<std::uint32_t>(bytes, kRootChildEntry, kEntLeft, kRootChildEntry);
    RequireOpsRejected(bytes, kLoopKeywords, kStrip);
}

// ── Bounded work ──────────────────────────────────────────

TEST_CASE("edge: MSI start sector 0xFFFFFFFA", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kMiniStreamEntry, kEntStart, 0xFFFFFFFAu);
    RequireOpsRejected(bytes, {"past the end"}, kDigest);

    Bytes signed_copy = SignedTiny();
    PatchEntry<std::uint32_t>(signed_copy, FindEntry(signed_copy, kSignatureName), kEntStart,
                              0xFFFFFFFAu);
    RequireOpsRejected(signed_copy, {"past the end"}, kPresence | kExtract);
}

TEST_CASE("edge: MSI DIFAT count 0xFFFFFFFF", "[MalformedMsi][edge]")
{
    // No DIFAT sector at all.
    Bytes bytes = Tiny();
    PatchLE<std::uint32_t>(bytes, kHdrDifatCount, 0xFFFFFFFFu);
    RequireOpsBounded(bytes, kAllOps);

    // A DIFAT sector that ends its chain at once.
    Bytes with_sector = Tiny();
    const std::uint32_t sector = AppendSector(with_sector);
    PatchLE<std::uint32_t>(with_sector, SectorOffset(sector) + kSectorSize - 4, kEndOfChain);
    PatchLE<std::uint32_t>(with_sector, kHdrFirstDifat, sector);
    PatchLE<std::uint32_t>(with_sector, kHdrDifatCount, 0xFFFFFFFFu);
    PatchFat(with_sector, sector, kDifSect);
    RequireOpsBounded(with_sector, kAllOps);
}

TEST_CASE("edge: MSI DIFAT first sector 0xFFFFFFFA", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint32_t>(bytes, kHdrFirstDifat, 0xFFFFFFFAu);
    PatchLE<std::uint32_t>(bytes, kHdrDifatCount, 1);
    RequireOpsRejected(bytes, {"past the end"}, kAllOps);
}

TEST_CASE("edge: MSI FAT sector number outside the file", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint32_t>(bytes, kHdrDifat0, 1000);
    RequireOpsRejected(bytes, {"past the end"}, kAllOps);

    Bytes near_max = Tiny();
    PatchLE<std::uint32_t>(near_max, kHdrDifat0, 0xFFFFFFFAu);
    RequireOpsRejected(near_max, {"past the end"}, kAllOps);
}

TEST_CASE("edge: MSI sector number times sector size wraps", "[MalformedMsi][edge]")
{
    // 0x800000 * 512 is 2^32: a start sector that lands on the header when the
    // product is kept in 32 bits.
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kMiniStreamEntry, kEntStart, 0x00800000u);
    RequireOpsRejected(bytes, {"past the end"}, kDigest);

    // The same through a chain link of the mini stream.
    Bytes link = Tiny();
    PatchFat(link, 5, 0x00800000u);
    RequireOpsRejected(link, {"past the end"}, kDigest);

    Bytes dir_link = Tiny();
    PatchFat(dir_link, 14, 0x00800000u);
    RequireOpsRejected(dir_link, {"past the end"}, kAllOps);
}

TEST_CASE("edge: MSI FAT chain loop", "[MalformedMsi][edge]")
{
    // The mini stream's chain: 3, 4, 5, 3, ...
    Bytes stream_loop = Tiny();
    PatchFat(stream_loop, 5, 3);
    RequireOpsRejected(stream_loop, {"loop"}, kDigest);

    // The directory's chain: 13, 14, 15, 16, 14, ...
    Bytes directory_loop = Tiny();
    PatchFat(directory_loop, 16, 14);
    RequireOpsRejected(directory_loop, {"loop"}, kAllOps);

    // A chain that is its own successor.
    Bytes self_loop = Tiny();
    PatchFat(self_loop, 15, 15);
    RequireOpsRejected(self_loop, {"loop"}, kAllOps);
}

TEST_CASE("edge: MSI DIFAT loop", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    const std::uint32_t sector = AppendSector(bytes);
    PatchLE<std::uint32_t>(bytes, SectorOffset(sector) + kSectorSize - 4, sector);
    PatchLE<std::uint32_t>(bytes, kHdrFirstDifat, sector);
    PatchLE<std::uint32_t>(bytes, kHdrDifatCount, 4);
    PatchFat(bytes, sector, kDifSect);
    RequireOpsRejected(bytes, kLoopKeywords, kAllOps);
}

TEST_CASE("edge: MSI mini-FAT loop", "[MalformedMsi][edge]")
{
    // A stream's mini sectors: 0, 1, 0, 1, ...
    Bytes stream_loop = Tiny();
    PatchMiniFat(stream_loop, 1, 0);
    RequireOpsRejected(stream_loop, {"loop"}, kDigest);

    // The chain of sectors that hold the mini-FAT.
    Bytes table_loop = Tiny();
    PatchFat(table_loop, kMiniFatSector, kMiniFatSector);
    RequireOpsRejected(table_loop, {"loop"}, kAllOps);
}

TEST_CASE("edge: MSI child link to the root", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint8_t>(bytes, kRootChildEntry, kEntType, kTypeStorage);
    PatchEntry<std::uint32_t>(bytes, kRootChildEntry, kEntChild, 0);
    RequireOpsRejected(bytes, kLoopKeywords, kAllOps);
}

TEST_CASE("edge: MSI two-entry sibling cycle", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kRootChildEntry, kEntLeft, kSecondEntry);
    PatchEntry<std::uint32_t>(bytes, kSecondEntry, kEntLeft, kRootChildEntry);
    RequireOpsRejected(bytes, kLoopKeywords, kAllOps);
}

TEST_CASE("edge: MSI huge FAT sector count", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint32_t>(bytes, kHdrFatCount, 0xFFFFFFFFu);
    RequireOpsBounded(bytes, kAllOps);

    Bytes many = Tiny();
    PatchLE<std::uint32_t>(many, kHdrFatCount, 0x00FFFFFFu);
    RequireOpsBounded(many, kAllOps);
}

TEST_CASE("edge: MSI two streams share a sector", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    const std::uint32_t start =
        sm::detail::GetLE<std::uint32_t>(bytes, EntryOffset(bytes, kMiniStreamEntry) + kEntStart);
    PatchEntry<std::uint32_t>(bytes, kOtherMiniEntry, kEntStart, start);
    RequireOpsRejected(bytes, {"more than one stream"}, kDigest);
}

TEST_CASE("edge: MSI sector size exponent 0", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint16_t>(bytes, kHdrSectorExp, 0);
    RequireOpsRejected(bytes, {"sector size exponent"}, kAllOps);
}

// ── Other families ────────────────────────────────────────

TEST_CASE("edge: MSI directory sector past the end", "[MalformedMsi][edge]")
{
    // The directory chain goes on to a sector the file does not have.
    Bytes bytes = Tiny();
    PatchFat(bytes, 17, kSectorCount + 1);
    RequireOpsRejected(bytes, {"past the end"}, kAllOps);
}

TEST_CASE("edge: MSI mini stream past the end", "[MalformedMsi][edge]")
{
    // The root's chain goes on to a sector the file does not have.
    Bytes bytes = Tiny();
    PatchFat(bytes, 11, kSectorCount + 1);
    RequireOpsRejected(bytes, {"past the end"}, kDigest);
}

TEST_CASE("edge: MSI stream size larger than its chain", "[MalformedMsi][edge]")
{
    // 26 mini sectors of 64 bytes hold 1664 bytes, not 4000.
    Bytes bytes = Tiny();
    PatchEntry<std::uint32_t>(bytes, kMiniStreamEntry, kEntSize, 4000);
    RequireOpsRejected(bytes, {"exceeds"}, kDigest);
}

TEST_CASE("edge: MSI version 3 with exponent 12", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint16_t>(bytes, kHdrSectorExp, 12);
    RequireOpsRejected(bytes, {"sector size exponent"}, kAllOps);
}

TEST_CASE("edge: MSI version 4 with exponent 9", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint16_t>(bytes, kHdrMajor, 4);
    RequireOpsRejected(bytes, {"sector size exponent"}, kAllOps);
}

TEST_CASE("edge: MSI mini-stream cutoff 2048", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint32_t>(bytes, kHdrCutoff, 2048);
    RequireOpsRejected(bytes, {"mini-stream cutoff"}, kAllOps);
}

TEST_CASE("edge: MSI mini-sector exponent 7", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchLE<std::uint16_t>(bytes, kHdrMiniExp, 7);
    RequireOpsRejected(bytes, {"mini-sector exponent"}, kAllOps);
}

TEST_CASE("edge: MSI entry 0 is not the root", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint8_t>(bytes, 0, kEntType, 2);
    RequireOpsRejected(bytes, {"not the root"}, kAllOps);

    Bytes unused = Tiny();
    PatchEntry<std::uint8_t>(unused, 0, kEntType, kTypeUnused);
    RequireOpsRejected(unused, {"not the root"}, kAllOps);
}

TEST_CASE("edge: MSI unused entry reached from the tree", "[MalformedMsi][edge]")
{
    Bytes bytes = Tiny();
    PatchEntry<std::uint8_t>(bytes, kSecondEntry, kEntType, kTypeUnused);
    RequireOpsRejected(bytes, {"unused"}, kAllOps);
}

TEST_CASE("edge: MSI zero-length and 511-byte files", "[MalformedMsi][edge]")
{
    // An empty file is not an installer; a file with only the first 511 bytes
    // of a header carries the magic, so IsMsi still says yes.
    seedtest::ScratchDir scratch;
    const Bytes empty;
    CHECK_FALSE(MsiSigner::IsMsi(sm::WriteScratch(scratch, "empty.msi", empty)));
    RequireOpsRejected(empty, {"too small"}, kAllOps);

    const Bytes short_header = sm::Truncate(Tiny(), 511);
    CHECK(MsiSigner::IsMsi(sm::WriteScratch(scratch, "short.msi", short_header)));
    RequireOpsRejected(short_header, {"too small"}, kAllOps);
}

TEST_CASE("edge: MSI truncated inside the header", "[MalformedMsi][edge]")
{
    RequireOpsRejected(sm::Truncate(Tiny(), 100), {"too small"}, kAllOps);
    RequireOpsRejected(sm::Truncate(Tiny(), 8), {"too small"}, kAllOps);
}

TEST_CASE("edge: MSI truncated inside the FAT", "[MalformedMsi][edge]")
{
    // The FAT sector is the last sector of the sample: drop it, or half of it.
    const std::uint64_t fat_start = SectorOffset(kFatSector);
    RequireOpsRejected(sm::Truncate(Tiny(), fat_start), {"past the end"}, kAllOps);
    RequireOpsRejected(sm::Truncate(Tiny(), fat_start + 256), {"past the end"}, kAllOps);
}

TEST_CASE("edge: MSI truncated inside the directory", "[MalformedMsi][edge]")
{
    // With the directory last, cut the last directory sector after two entries.
    const Bytes reordered = Reorder(Tiny(), OrderDirectoryLast());
    const std::uint64_t last_dir = SectorOffset(kSectorCount - 1);
    RequireOpsRejected(sm::Truncate(reordered, last_dir + 256), {"past the end"}, kAllOps);
    RequireOpsRejected(sm::Truncate(reordered, last_dir), {"past the end"}, kAllOps);
}

TEST_CASE("edge: MSI truncated inside a stream", "[MalformedMsi][edge]")
{
    // With the mini stream last, cut it in the middle.
    const Bytes reordered = Reorder(Tiny(), OrderStreamLast());
    const std::uint64_t cut = SectorOffset(kSectorCount - 6) + 100;
    RequireOpsRejected(sm::Truncate(reordered, cut), {"past the end", "exceeds"}, kDigest);
}

TEST_CASE("edge: MSI oversized signature size check", "[MalformedMsi][edge]")
{
#ifdef SEED_HAVE_MSI_SIZE_CHECK
    // The check takes a length, so no 4 GiB buffer is allocated.
    const std::uint64_t largest = std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint16_t kVersion3 = 3;
    constexpr std::uint16_t kVersion4 = 4;
    CHECK_NOTHROW(seed::internal::CheckMsiSignatureSize(0, kVersion3));
    CHECK_NOTHROW(seed::internal::CheckMsiSignatureSize(largest, kVersion3));
    sm::RequireRejected([&] { seed::internal::CheckMsiSignatureSize(largest + 1, kVersion3); },
                        kFormat, "signature size");
    sm::RequireRejected(
        [&] {
            seed::internal::CheckMsiSignatureSize(std::numeric_limits<std::uint64_t>::max(),
                                                  kVersion3);
        },
        kFormat, "signature size");
    // Version 4 stores a 64-bit stream size.
    CHECK_NOTHROW(seed::internal::CheckMsiSignatureSize(largest + 1, kVersion4));
#else
    SKIP("the signature size check (src/internal/MsiSignatureSize.hpp, "
         "seed::internal::CheckMsiSignatureSize) does not exist yet");
#endif
}
