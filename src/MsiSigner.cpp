#include <libthe-seed/MsiSigner.hpp>

#include "internal/AuthenticodeDigestField.hpp"
#include "internal/BoundedBytes.hpp"
#include "internal/CfbReader.hpp"
#include "internal/CfbWriter.hpp"
#include "internal/FileIO.hpp"
#include "internal/MsiAuthenticode.hpp"
#include "internal/MsiSignatureSize.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using seed::internal::AuthenticodeDigestField;
using seed::internal::CfbNode;
using seed::internal::CfbReader;
using seed::internal::CheckMsiSignatureSize;
using seed::internal::ComputeMsiFingerprint;
using seed::internal::GuardEntryPoint;
using seed::internal::MsiSignatureExName;
using seed::internal::MsiSignatureName;
using seed::internal::PackageModel;
using seed::internal::ReadAuthenticodeDigestField;
using seed::internal::WriteCfb;

constexpr const char *kFormat = "MSI";

constexpr std::uint8_t CFBF_MAGIC[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

// The message of a rejection with the file named: "MSI: <file>: <reason>".
std::string Refusal(const std::string &file_path, const std::string &reason)
{
    return std::string(kFormat) + ": " + file_path + ": " + reason;
}

// Runs an operation on a file. A rejection from the reader or the writer
// ("MSI: <reason>") comes out naming the file.
template <typename F>
auto Guarded(const std::string &file_path, F &&operation) -> decltype(operation())
{
    return GuardEntryPoint(kFormat, [&] {
        try
        {
            return operation();
        }
        catch(const std::runtime_error &error)
        {
            const std::string prefix = std::string(kFormat) + ": ";
            const std::string message = error.what();
            if(message.compare(0, prefix.size(), prefix) != 0 ||
               message.find(file_path) != std::string::npos)
            {
                throw;
            }
            throw std::runtime_error(Refusal(file_path, message.substr(prefix.size())));
        }
    });
}

// The first four bytes of a fingerprint in hex, as shown in messages.
std::string Prefix(const std::vector<std::uint8_t> &digest)
{
    std::string text;
    for(std::size_t i = 0; i < digest.size() && i < 4; ++i)
    {
        char pair[3];
        std::snprintf(pair, sizeof(pair), "%02x", static_cast<unsigned>(digest[i]));
        text += pair;
    }
    return text;
}

std::string MismatchText(const std::vector<std::uint8_t> &stored,
                         const std::vector<std::uint8_t> &computed)
{
    return "signature does not match the package contents (stored " + Prefix(stored) +
           "..., computed " + Prefix(computed) + "...)";
}

// The signature stream among the root's children, found by enumeration.
const CfbNode *FindSignatureEntry(const PackageModel &model)
{
    return PackageModel::FindByEnumeration(model.Root(), MsiSignatureName());
}

} // anonymous namespace

bool MsiSigner::IsMsi(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
        std::ifstream file(file_path, std::ios::binary);
        if(!file)
        {
            return false;
        }

        std::uint8_t magic[8] = {0};
        file.read(reinterpret_cast<char*>(magic), 8);
        if(file.gcount() < 8)
        {
            return false;
        }

        return std::memcmp(magic, CFBF_MAGIC, 8) == 0;
    });
}

MsiSigner::DigestResult MsiSigner::ComputeAuthenticodeDigest(const std::string &file_path)
{
    return Guarded(file_path, [&] {
        const std::vector<std::uint8_t> bytes = ReadFileBytes(file_path);
        PackageModel model = CfbReader::Parse(bytes);

        DigestResult result;
        result.digest = ComputeMsiFingerprint(model);
        return result;
    });
}

void MsiSigner::EmbedSignature(
    const std::string &file_path,
    const std::vector<std::uint8_t> &pkcs7_der,
    bool require_matching_digest)
{
    Guarded(file_path, [&] {
        if(pkcs7_der.empty())
        {
            throw std::runtime_error(Refusal(file_path, "the signature is empty"));
        }

        // The old package and the model that refers to it are gone before the file is written.
        std::vector<std::uint8_t> rebuilt;
        {
            const std::vector<std::uint8_t> bytes = ReadFileBytes(file_path);
            PackageModel model = CfbReader::Parse(bytes);

            // Nothing is written until the signature is known to fit
            CheckMsiSignatureSize(pkcs7_der.size(), model.Header().major_version);

            AuthenticodeDigestField field;
            if(require_matching_digest)
            {
                field = ReadAuthenticodeDigestField(pkcs7_der);
                if(!field.readable || !field.IsSha256())
                {
                    throw std::runtime_error(Refusal(
                        file_path, "the signature holds no SHA-256 package fingerprint (" +
                                       (field.readable ? "digest algorithm " + field.algorithm
                                                       : field.detail) +
                                       ")"));
                }
            }

            rebuilt = WriteCfb(model, &pkcs7_der);

            if(require_matching_digest)
            {
                PackageModel written = CfbReader::Parse(rebuilt);
                const std::vector<std::uint8_t> computed = ComputeMsiFingerprint(written);
                if(computed != field.digest)
                {
                    throw std::runtime_error(
                        Refusal(file_path, MismatchText(field.digest, computed)));
                }
            }
        }

        // Replace the file as one step
        WriteFileBytes(file_path, rebuilt);
    });
}

std::optional<std::vector<std::uint8_t>> MsiSigner::ExtractSignature(
    const std::string &file_path)
{
    return Guarded(file_path, [&] {
        const std::vector<std::uint8_t> bytes = ReadFileBytes(file_path);
        PackageModel model = CfbReader::Parse(bytes);

        // A signature of size zero is no signature.
        const CfbNode *entry = FindSignatureEntry(model);
        if(entry == nullptr || entry->size == 0)
        {
            return std::optional<std::vector<std::uint8_t>>();
        }
        return std::optional<std::vector<std::uint8_t>>(model.ReadStream(*entry));
    });
}

bool MsiSigner::HasEmbeddedSignature(const std::string &file_path)
{
    return Guarded(file_path, [&] {
        const std::vector<std::uint8_t> bytes = ReadFileBytes(file_path);
        PackageModel model = CfbReader::Parse(bytes);

        const CfbNode *entry = FindSignatureEntry(model);
        if(entry == nullptr || entry->size == 0)
        {
            return false;
        }

        // The chain must be valid for the size; no byte is copied or parsed.
        (void)model.Resolve(*entry);
        return true;
    });
}

bool MsiSigner::StripSignature(const std::string &file_path)
{
    return Guarded(file_path, [&] {
        std::vector<std::uint8_t> rebuilt;
        {
            const std::vector<std::uint8_t> bytes = ReadFileBytes(file_path);
            PackageModel model = CfbReader::Parse(bytes);

            // Neither stream: nothing to remove, and the file is not touched.
            if(PackageModel::FindByEnumeration(model.Root(), MsiSignatureName()) == nullptr &&
               PackageModel::FindByEnumeration(model.Root(), MsiSignatureExName()) == nullptr)
            {
                return false;
            }
            rebuilt = WriteCfb(model, nullptr);
        }

        WriteFileBytes(file_path, rebuilt);
        return true;
    });
}

MsiSigner::SignatureCheck MsiSigner::CheckSignature(const std::string &file_path)
{
    return Guarded(file_path, [&] {
        const std::vector<std::uint8_t> bytes = ReadFileBytes(file_path);
        PackageModel model = CfbReader::Parse(bytes);

        SignatureCheck check;
        check.computed_digest = ComputeMsiFingerprint(model);

        const CfbNode *entry = FindSignatureEntry(model);
        if(entry == nullptr || entry->size == 0)
        {
            return check;
        }

        // A signature that cannot be read is a state, not an error.
        check.state = SignatureState::Unreadable;
        std::vector<std::uint8_t> blob;
        try
        {
            blob = model.ReadStream(*entry);
        }
        catch(const std::runtime_error &error)
        {
            check.detail = error.what();
            return check;
        }

        const AuthenticodeDigestField field = ReadAuthenticodeDigestField(blob);
        if(!field.readable)
        {
            check.detail = "the signature cannot be read: " + field.detail;
            return check;
        }
        if(!field.IsSha256())
        {
            check.detail = "the signature uses digest algorithm " + field.algorithm +
                           ", not SHA-256";
            return check;
        }

        check.stored_digest = field.digest;
        if(field.digest == check.computed_digest)
        {
            check.state = SignatureState::Matches;
            return check;
        }
        check.state = SignatureState::Mismatch;
        check.detail = MismatchText(field.digest, check.computed_digest);
        return check;
    });
}
