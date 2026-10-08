#include "MsiAuthenticode.hpp"

#include <algorithm>
#include <cstdio>

#include "../../external/picosha2.h"

namespace seed::internal
{

namespace
{

constexpr const char *kFormat = "MSI";

// Streams are fed to the hash in pieces so that no copy of a stream is made.
constexpr std::uint64_t kHashPiece = 64 * 1024;

// The raw-byte order of two names: the bytes of the UTF-16LE form compared in
// turn (the low byte of a unit first); on a common prefix the shorter first.
bool RawNameLess(const std::u16string &a, const std::u16string &b)
{
    const std::size_t common = std::min(a.size(), b.size());
    for(std::size_t i = 0; i < common; ++i)
    {
        if(a[i] == b[i])
        {
            continue;
        }
        const auto low_a = static_cast<std::uint8_t>(a[i] & 0xFF);
        const auto low_b = static_cast<std::uint8_t>(b[i] & 0xFF);
        if(low_a != low_b)
        {
            return low_a < low_b;
        }
        return static_cast<std::uint8_t>(a[i] >> 8) < static_cast<std::uint8_t>(b[i] >> 8);
    }
    return a.size() < b.size();
}

// A name for a message: printable ASCII as it is, any other unit as \uXXXX.
std::string Printable(const std::u16string &name)
{
    std::string text;
    for(const char16_t unit : name)
    {
        if(unit >= 0x20 && unit < 0x7F)
        {
            text.push_back(static_cast<char>(unit));
        }
        else
        {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(unit));
            text += escaped;
        }
    }
    return text;
}

// Two entries that the format's ordering treats as one name cannot be told
// apart by a reader that searches the directory, so no fingerprint is defined.
void RejectDuplicateNames(const CfbNode &storage)
{
    std::vector<const CfbNode *> sorted;
    sorted.reserve(storage.children.size());
    for(const CfbNode &child : storage.children)
    {
        sorted.push_back(&child);
    }
    std::sort(sorted.begin(), sorted.end(), [](const CfbNode *a, const CfbNode *b)
              { return PackageModel::CompareNames(a->name, b->name) < 0; });
    for(std::size_t i = 1; i < sorted.size(); ++i)
    {
        if(PackageModel::CompareNames(sorted[i - 1]->name, sorted[i]->name) == 0)
        {
            ThrowMalformed(kFormat, "the package holds two entries named \"" +
                                        Printable(sorted[i]->name) +
                                        "\" in one storage; no fingerprint is defined");
        }
    }
}

void HashStorage(PackageModel &model, const CfbNode &storage, bool is_root,
                 picosha2::hash256_one_by_one &hasher)
{
    RejectDuplicateNames(storage);

    std::vector<const CfbNode *> children;
    children.reserve(storage.children.size());
    for(const CfbNode &child : storage.children)
    {
        if(is_root && IsMsiSignatureName(child.name))
        {
            continue;
        }
        children.push_back(&child);
    }
    std::sort(children.begin(), children.end(),
              [](const CfbNode *a, const CfbNode *b) { return RawNameLess(a->name, b->name); });

    for(const CfbNode *child : children)
    {
        if(child->is_storage)
        {
            HashStorage(model, *child, false, hasher);
            continue;
        }
        for(const ByteExtent &extent : model.Resolve(*child))
        {
            const std::uint8_t *begin = model.Source() + extent.offset;
            std::uint64_t left = extent.length;
            while(left != 0)
            {
                const std::uint64_t piece = std::min(left, kHashPiece);
                hasher.process(begin, begin + piece);
                begin += piece;
                left -= piece;
            }
        }
    }

    hasher.process(storage.class_id.begin(), storage.class_id.end());
}

} // namespace

const std::u16string &MsiSignatureName()
{
    static const std::u16string name = {0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
                                        u'S',   u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'};
    return name;
}

const std::u16string &MsiSignatureExName()
{
    static const std::u16string name = {0x0005, u'M', u's', u'i', u'D', u'i', u'g', u'i',
                                        u't',   u'a', u'l', u'S', u'i', u'g', u'n', u'a',
                                        u't',   u'u', u'r', u'e', u'E', u'x'};
    return name;
}

bool IsMsiSignatureName(const std::u16string &name)
{
    return name == MsiSignatureName() || name == MsiSignatureExName();
}

std::vector<std::uint8_t> ComputeMsiFingerprint(PackageModel &model)
{
    picosha2::hash256_one_by_one hasher;
    hasher.init();
    HashStorage(model, model.Root(), true, hasher);
    hasher.finish();

    std::vector<std::uint8_t> digest(picosha2::k_digest_size);
    hasher.get_hash_bytes(digest.begin(), digest.end());
    return digest;
}

} // namespace seed::internal
