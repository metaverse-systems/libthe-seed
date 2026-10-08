#pragma once

// The Authenticode fingerprint of an installer package, and the names of the
// two signature streams.
//
// The fingerprint is the SHA-256 of, for each storage starting at the root:
// the children ordered by the raw bytes of their UTF-16LE names (on a common
// prefix the shorter name first), each stream's bytes and each child
// storage's fingerprint input in turn, and then the 16-byte class identifier
// of the storage itself. Names are not hashed. In the root, the signature
// streams are left out, so the value is the same before signing, after
// signing and after the signature is removed.

#include "CfbReader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace seed::internal
{

// \005DigitalSignature
const std::u16string &MsiSignatureName();

// \005MsiDigitalSignatureEx
const std::u16string &MsiSignatureExName();

// True for either signature stream name.
bool IsMsiSignatureName(const std::u16string &name);

// The fingerprint (32 bytes). Reads every stream through the model. Throws
// std::runtime_error ("MSI: ...") when two entries of one storage have the
// same name by the format's ordering, or when a stream cannot be read.
std::vector<std::uint8_t> ComputeMsiFingerprint(PackageModel &model);

} // namespace seed::internal
