#include "FuzzTargets.hpp"

#include "../HeapCounter.hpp"

#include <libthe-seed/DependencyLister.hpp>
#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MsiSigner.hpp>
#include <libthe-seed/PeSigner.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace seedfuzz
{

namespace
{

constexpr std::size_t kHeapAllowance = 16u * 1024u * 1024u;
constexpr const char *kInternalMarker = "internal error (";

[[noreturn]] void Finding(const std::string &what)
{
    std::fprintf(stderr, "fuzz finding: %s\n", what.c_str());
    std::fflush(stderr);
    std::abort();
}

// A private directory under the temporary directory, created on first use and
// removed when the process exits.
class Scratch
{
public:
    Scratch()
    {
        std::random_device device;
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        for(int attempt = 0; attempt < 100; ++attempt)
        {
            const std::uint64_t suffix = (static_cast<std::uint64_t>(device()) << 32) | device();
            std::filesystem::path candidate =
                base / ("seed-fuzz-" + std::to_string(suffix));
            if(std::filesystem::create_directory(candidate, ec))
            {
                this->dir = candidate;
                return;
            }
        }
        Finding("cannot create a scratch directory");
    }

    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;

    ~Scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(this->dir, ec);
    }

    std::string File(const std::string &name) const { return (this->dir / name).string(); }

private:
    std::filesystem::path dir;
};

Scratch &SharedScratch()
{
    static Scratch scratch;
    return scratch;
}

void WriteFile(const std::string &path, const std::uint8_t *data, std::size_t size)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out)
    {
        Finding("cannot write " + path);
    }
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    out.close();
    if(!out)
    {
        Finding("cannot write " + path);
    }
}

void CheckMessage(const char *operation, const std::string &message)
{
    if(message.find(kInternalMarker) != std::string::npos)
    {
        Finding(std::string(operation) + " reported an internal error: " + message);
    }
}

// Runs one operation on its own: a std::runtime_error is an acceptable
// answer, anything else is a finding, and so is too much heap growth.
template <typename F>
void Call(const char *operation, std::size_t input_size, std::size_t extra, F &&callable)
{
    seedtest::heap::HeapGrowthScope scope;
    try
    {
        callable();
    }
    catch(const std::runtime_error &e)
    {
        CheckMessage(operation, e.what());
    }
    catch(const std::exception &e)
    {
        Finding(std::string(operation) + " threw " + typeid(e).name() + ": " + e.what());
    }
    catch(...)
    {
        Finding(std::string(operation) + " threw a value that is not a standard exception");
    }
    if(seedtest::heap::HeapGrowthScope::Active() &&
       scope.PeakGrowth() > 10 * input_size + kHeapAllowance + extra)
    {
        Finding(std::string(operation) + " used " + std::to_string(scope.PeakGrowth()) +
                " bytes of heap for an input of " + std::to_string(input_size) + " bytes");
    }
}

void ListDependencies(const std::string &path, std::size_t size)
{
    Call("DependencyLister::ListDependencies", size, 0, [&] {
        DependencyLister lister;
        const DependencyResult result = lister.ListDependencies({path}, {});
        for(const auto &entry : result.errors)
        {
            CheckMessage("DependencyLister::ListDependencies", entry.second);
        }
    });
}

void PutInt(std::vector<std::uint8_t> &bytes, std::uint64_t offset, std::uint64_t value,
            std::size_t width, bool little)
{
    for(std::size_t i = 0; i < width; ++i)
    {
        const std::size_t shift = little ? i : width - 1 - i;
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * shift)) & 0xFF);
    }
}

// A shared object with a PT_LOAD over the whole file, a PT_DYNAMIC and two
// DT_NEEDED entries.
std::vector<std::uint8_t> ElfBuilder(bool is64, bool little)
{
    const std::uint64_t ehdr = is64 ? 64 : 52;
    const std::uint64_t phdr = is64 ? 56 : 32;
    const std::uint64_t dyn = is64 ? 16 : 8;
    const std::size_t word = is64 ? 8 : 4;
    const std::string strtab = std::string(1, '\0') + "libfoo.so.1" + std::string(1, '\0') +
                               "libbar.so.2" + std::string(1, '\0');
    const std::uint64_t dyn_offset = (ehdr + 2 * phdr + 7) / 8 * 8;
    const std::uint64_t dyn_count = 4;
    const std::uint64_t str_offset = dyn_offset + dyn_count * dyn;
    const std::uint64_t size = str_offset + strtab.size();
    const std::uint64_t base = 0x400000;

    std::vector<std::uint8_t> bytes(size, 0);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = is64 ? 2 : 1;
    bytes[5] = little ? 1 : 2;
    bytes[6] = 1;
    PutInt(bytes, 16, 3, 2, little);
    PutInt(bytes, 18, 62, 2, little);
    PutInt(bytes, 20, 1, 4, little);
    PutInt(bytes, is64 ? 32 : 28, ehdr, word, little);
    PutInt(bytes, is64 ? 52 : 40, ehdr, 2, little);
    PutInt(bytes, is64 ? 54 : 42, phdr, 2, little);
    PutInt(bytes, is64 ? 56 : 44, 2, 2, little);

    auto set_phdr = [&](std::uint64_t index, std::uint32_t type, std::uint64_t offset,
                        std::uint64_t filesz) {
        const std::uint64_t at = ehdr + index * phdr;
        PutInt(bytes, at, type, 4, little);
        PutInt(bytes, at + (is64 ? 8 : 4), offset, word, little);
        PutInt(bytes, at + (is64 ? 16 : 8), base + offset, word, little);
        PutInt(bytes, at + (is64 ? 32 : 16), filesz, word, little);
        PutInt(bytes, at + (is64 ? 40 : 20), filesz, word, little);
    };
    set_phdr(0, 1, 0, size);
    set_phdr(1, 2, dyn_offset, dyn_count * dyn);

    auto set_dyn = [&](std::uint64_t index, std::uint64_t tag, std::uint64_t value) {
        PutInt(bytes, dyn_offset + index * dyn, tag, word, little);
        PutInt(bytes, dyn_offset + index * dyn + word, value, word, little);
    };
    set_dyn(0, 1, 1);
    set_dyn(1, 1, 13);
    set_dyn(2, 5, base + str_offset);
    set_dyn(3, 0, 0);
    for(std::size_t i = 0; i < strtab.size(); ++i)
    {
        bytes[str_offset + i] = static_cast<std::uint8_t>(strtab[i]);
    }
    return bytes;
}

} // namespace

std::vector<std::uint8_t> ElfSeed(bool is64, bool little)
{
    return ElfBuilder(is64, little);
}

std::vector<std::uint8_t> SuperBlobSeed()
{
    std::vector<std::uint8_t> code_directory(96, 0x11);
    code_directory[0] = 0xFA;
    code_directory[1] = 0xDE;
    code_directory[2] = 0x0C;
    code_directory[3] = 0x02;
    code_directory[4] = 0;
    code_directory[5] = 0;
    code_directory[6] = 0;
    code_directory[7] = static_cast<std::uint8_t>(code_directory.size());
    return MachOSigner::BuildSuperBlob(code_directory, std::vector<std::uint8_t>(70, 0xA5));
}

void FuzzElf(const std::uint8_t *data, std::size_t size)
{
    const std::string path = SharedScratch().File("input.bin");
    WriteFile(path, data, size);
    ListDependencies(path, size);
}

void FuzzPe(const std::uint8_t *data, std::size_t size)
{
    const std::string path = SharedScratch().File("input.bin");
    const std::string work = SharedScratch().File("work.bin");
    WriteFile(path, data, size);

    ListDependencies(path, size);
    Call("PeSigner::ComputeAuthenticodeDigest", size, 0,
         [&] { (void)PeSigner::ComputeAuthenticodeDigest(path); });
    Call("PeSigner::HasEmbeddedSignature", size, 0,
         [&] { (void)PeSigner::HasEmbeddedSignature(path); });
    Call("PeSigner::ExtractSignature", size, 0, [&] { (void)PeSigner::ExtractSignature(path); });

    const std::vector<std::uint8_t> blob(64, 0x5A);
    WriteFile(work, data, size);
    Call("PeSigner::EmbedSignature", size, blob.size(),
         [&] { PeSigner::EmbedSignature(work, blob); });
    Call("PeSigner::StripSignature", size, blob.size(), [&] { PeSigner::StripSignature(work); });
}

void FuzzMachO(const std::uint8_t *data, std::size_t size)
{
    const std::string path = SharedScratch().File("input.bin");
    const std::string work = SharedScratch().File("work.bin");
    WriteFile(path, data, size);

    Call("MachOParser::DetectFormat", size, 0, [&] { (void)MachOParser::DetectFormat(path); });
    Call("MachOParser::GetArchSlices", size, 0, [&] { (void)MachOParser::GetArchSlices(path); });
    Call("MachOParser::ListDependencies", size, 0,
         [&] { (void)MachOParser::ListDependencies(path); });
    Call("MachOSigner::ComputeCodeDirectory", size, 0,
         [&] { (void)MachOSigner::ComputeCodeDirectory(path, "fuzz"); });
    Call("MachOSigner::HasEmbeddedSignature", size, 0,
         [&] { (void)MachOSigner::HasEmbeddedSignature(path); });
    Call("MachOSigner::ExtractSignature", size, 0,
         [&] { (void)MachOSigner::ExtractSignature(path); });

    static const std::vector<std::uint8_t> blob = SuperBlobSeed();
    WriteFile(work, data, size);
    Call("MachOSigner::EmbedSignature", size, blob.size(),
         [&] { MachOSigner::EmbedSignature(work, blob); });
}

void FuzzSuperBlob(const std::uint8_t *data, std::size_t size)
{
    const std::vector<std::uint8_t> bytes(data, data + size);
    Call("MachOSigner::ExtractCmsFromSuperBlob", size, 0,
         [&] { (void)MachOSigner::ExtractCmsFromSuperBlob(bytes); });
    Call("MachOSigner::ExtractCodeDirectoryFromSuperBlob", size, 0,
         [&] { (void)MachOSigner::ExtractCodeDirectoryFromSuperBlob(bytes); });
}

void FuzzMsi(const std::uint8_t *data, std::size_t size)
{
    const std::string path = SharedScratch().File("input.bin");
    const std::string work = SharedScratch().File("work.bin");
    WriteFile(path, data, size);

    Call("MsiSigner::IsMsi", size, 0, [&] { (void)MsiSigner::IsMsi(path); });
    Call("MsiSigner::ComputeAuthenticodeDigest", size, 0,
         [&] { (void)MsiSigner::ComputeAuthenticodeDigest(path); });
    Call("MsiSigner::HasEmbeddedSignature", size, 0,
         [&] { (void)MsiSigner::HasEmbeddedSignature(path); });
    Call("MsiSigner::ExtractSignature", size, 0,
         [&] { (void)MsiSigner::ExtractSignature(path); });

    const std::vector<std::uint8_t> blob(1500, 0x5A);
    WriteFile(work, data, size);
    Call("MsiSigner::EmbedSignature", size, blob.size(),
         [&] { MsiSigner::EmbedSignature(work, blob); });
    Call("MsiSigner::StripSignature", size, blob.size(),
         [&] { MsiSigner::StripSignature(work); });
}

} // namespace seedfuzz
