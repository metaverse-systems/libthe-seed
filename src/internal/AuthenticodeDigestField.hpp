#pragma once

// Reads the fingerprint stored inside an Authenticode signature blob.
//
// The blob is a CMS SignedData whose content is an SpcIndirectDataContent;
// the fingerprint is the digest of its DigestInfo:
//
//   ContentInfo { OID signedData, [0] SignedData { version, digestAlgorithms,
//     ContentInfo { OID SpcIndirectDataContent, [0] SpcIndirectDataContent {
//       SpcAttributeTypeAndOptionalValue, DigestInfo { AlgorithmIdentifier,
//       OCTET STRING digest } } }, ... } }
//
// The walk follows only that path. Every length is checked against the bytes
// that contain it, so nothing outside the blob is read, and nothing is
// allocated from a length. A blob that does not have this shape is reported as
// unreadable, never as an exception.

#include <cstdint>
#include <string>
#include <vector>

namespace seed::internal
{

struct AuthenticodeDigestField
{
    bool readable = false;
    std::string algorithm;            // dotted object identifier, when readable
    std::vector<std::uint8_t> digest; // the stored fingerprint, when readable
    std::string detail;               // why it is unreadable

    // True for SHA-256 (2.16.840.1.101.3.4.2.1).
    bool IsSha256() const;
};

AuthenticodeDigestField ReadAuthenticodeDigestField(const std::vector<std::uint8_t> &blob);

} // namespace seed::internal
