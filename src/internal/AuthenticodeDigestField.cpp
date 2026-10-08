#include "AuthenticodeDigestField.hpp"

#include "BoundedBytes.hpp"

#include <cstddef>
#include <stdexcept>

namespace seed::internal
{

namespace
{

constexpr const char *kFormat = "signature";

// Bounds of the walk: the path is fixed, so these only guard the loops that
// skip over siblings and the decoding of lengths and identifiers.
constexpr unsigned kMaxSkippedSiblings = 16;
constexpr unsigned kMaxLengthBytes = 4;
constexpr unsigned kMaxOidBytes = 32;

constexpr std::uint8_t kTagInteger = 0x02;
constexpr std::uint8_t kTagOctetString = 0x04;
constexpr std::uint8_t kTagOid = 0x06;
constexpr std::uint8_t kTagSequence = 0x30;
constexpr std::uint8_t kTagSet = 0x31;
constexpr std::uint8_t kTagContext0 = 0xA0;

struct Element
{
    std::uint8_t tag;
    ByteSpan content;
    std::uint64_t total; // header and content
};

// The element that starts at `offset` of `parent`.
Element ReadElement(const ByteSpan &parent, std::uint64_t offset)
{
    const auto tag = parent.Read<std::uint8_t>(offset, ByteOrder::Big, "tag");
    if((tag & 0x1F) == 0x1F)
    {
        ThrowMalformed(kFormat, "tag at offset " + std::to_string(offset) + " uses the long form");
    }
    std::uint64_t length = parent.Read<std::uint8_t>(offset + 1, ByteOrder::Big, "length");
    std::uint64_t header = 2;
    if(length >= 0x80)
    {
        const unsigned count = static_cast<unsigned>(length & 0x7F);
        if(count == 0 || count > kMaxLengthBytes)
        {
            ThrowMalformed(kFormat, "length at offset " + std::to_string(offset + 1) +
                                        " is indefinite or too long");
        }
        length = 0;
        for(unsigned i = 0; i < count; ++i)
        {
            length = (length << 8) |
                     parent.Read<std::uint8_t>(offset + 2 + i, ByteOrder::Big, "length byte");
        }
        header += count;
    }
    return {tag, parent.Sub(offset + header, length, "element"), header + length};
}

// The n-th child (from 0) of a constructed element, which must have this tag.
Element Child(const Element &parent, unsigned n, std::uint8_t tag, const char *what)
{
    if(n > kMaxSkippedSiblings)
    {
        ThrowMalformed(kFormat, std::string(what) + " is too far from the start of its parent");
    }
    std::uint64_t offset = 0;
    for(unsigned i = 0; i < n; ++i)
    {
        offset += ReadElement(parent.content, offset).total;
    }
    const Element child = ReadElement(parent.content, offset);
    if(child.tag != tag)
    {
        ThrowMalformed(kFormat, std::string(what) + " has tag " + std::to_string(child.tag) +
                                    ", expected " + std::to_string(tag));
    }
    return child;
}

std::string DecodeOid(const ByteSpan &content)
{
    if(content.Size() == 0 || content.Size() > kMaxOidBytes)
    {
        ThrowMalformed(kFormat, "object identifier has " + std::to_string(content.Size()) +
                                    " bytes");
    }
    std::string text;
    std::uint64_t value = 0;
    bool first = true;
    for(std::uint64_t i = 0; i < content.Size(); ++i)
    {
        const auto byte = content.Read<std::uint8_t>(i, ByteOrder::Big, "object identifier");
        value = (value << 7) | (byte & 0x7F);
        if((byte & 0x80) != 0)
        {
            if(i + 1 == content.Size() || value > (UINT64_MAX >> 8))
            {
                ThrowMalformed(kFormat, "object identifier is not well formed");
            }
            continue;
        }
        if(first)
        {
            const std::uint64_t head = value < 80 ? value / 40 : 2;
            text = std::to_string(head) + "." + std::to_string(value - head * 40);
            first = false;
        }
        else
        {
            text += "." + std::to_string(value);
        }
        value = 0;
    }
    return text;
}

std::string OidOf(const Element &element)
{
    return DecodeOid(element.content);
}

} // namespace

bool AuthenticodeDigestField::IsSha256() const
{
    return this->algorithm == "2.16.840.1.101.3.4.2.1";
}

AuthenticodeDigestField ReadAuthenticodeDigestField(const std::vector<std::uint8_t> &blob)
{
    AuthenticodeDigestField field;
    try
    {
        const ByteSpan whole(blob, kFormat, "the signature");
        const Element content_info = ReadElement(whole, 0);
        if(content_info.tag != kTagSequence)
        {
            ThrowMalformed(kFormat, "the signature does not start with a sequence");
        }
        if(OidOf(Child(content_info, 0, kTagOid, "content type")) != "1.2.840.113549.1.7.2")
        {
            ThrowMalformed(kFormat, "the signature is not a signed-data structure");
        }

        const Element signed_data_tag = Child(content_info, 1, kTagContext0, "signed data");
        const Element signed_data = Child(signed_data_tag, 0, kTagSequence, "signed data body");
        (void)Child(signed_data, 0, kTagInteger, "signed data version");
        (void)Child(signed_data, 1, kTagSet, "digest algorithms");
        const Element inner = Child(signed_data, 2, kTagSequence, "signed content");
        if(OidOf(Child(inner, 0, kTagOid, "signed content type")) != "1.3.6.1.4.1.311.2.1.4")
        {
            ThrowMalformed(kFormat, "the signed content is not an indirect data content");
        }

        const Element data_tag = Child(inner, 1, kTagContext0, "indirect data");
        const Element data = Child(data_tag, 0, kTagSequence, "indirect data body");
        const Element digest_info = Child(data, 1, kTagSequence, "digest information");
        const Element algorithm = Child(digest_info, 0, kTagSequence, "digest algorithm");
        field.algorithm = OidOf(Child(algorithm, 0, kTagOid, "digest algorithm identifier"));
        const Element digest = Child(digest_info, 1, kTagOctetString, "digest");

        field.digest.assign(digest.content.Data(), digest.content.Data() + digest.content.Size());
        field.readable = true;
    }
    catch(const std::runtime_error &e)
    {
        field.readable = false;
        field.digest.clear();
        field.algorithm.clear();
        field.detail = e.what();
    }
    return field;
}

} // namespace seed::internal
