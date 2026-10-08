// Prints what libthe-seed's binary tooling does with each file in a list, one
// line per operation:
//
//     <path> TAB <operation> TAB ok TAB <result>
//     <path> TAB <operation> TAB rejected TAB <message>
//
// (tab separated so that paths with spaces stay unambiguous)
//
// Build it against two library builds and compare the outputs with
// compare-reference.sh to check that a change rejects no well-formed file and
// alters no result.
//
// Usage: reference-dump <list file>      (one path per line, '#' starts a comment)

#include <libthe-seed/DependencyLister.hpp>
#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MsiSigner.hpp>
#include <libthe-seed/PeSigner.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

class Sha256
{
public:
    Sha256()
    {
        static const std::uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::memcpy(this->h, init, sizeof(init));
    }

    void Update(const std::uint8_t *data, std::size_t size)
    {
        this->total += size;
        while(size > 0)
        {
            std::size_t take = std::min<std::size_t>(size, 64 - this->fill);
            std::memcpy(this->block + this->fill, data, take);
            this->fill += take;
            data += take;
            size -= take;
            if(this->fill == 64)
            {
                this->Compress();
                this->fill = 0;
            }
        }
    }

    std::string Hex()
    {
        std::uint64_t bits = this->total * 8;
        std::uint8_t pad = 0x80;
        this->Update(&pad, 1);
        std::uint8_t zero = 0;
        while(this->fill != 56)
        {
            this->Update(&zero, 1);
        }
        std::uint8_t length[8];
        for(int i = 0; i < 8; ++i)
        {
            length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        }
        this->Update(length, 8);
        char out[65];
        for(int i = 0; i < 8; ++i)
        {
            std::snprintf(out + 8 * i, 9, "%08x", this->h[i]);
        }
        return std::string(out, 64);
    }

private:
    static std::uint32_t Rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Compress()
    {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        std::uint32_t w[64];
        for(int i = 0; i < 16; ++i)
        {
            w[i] = (std::uint32_t(this->block[4 * i]) << 24) |
                   (std::uint32_t(this->block[4 * i + 1]) << 16) |
                   (std::uint32_t(this->block[4 * i + 2]) << 8) | this->block[4 * i + 3];
        }
        for(int i = 16; i < 64; ++i)
        {
            std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6],
                      hh = h[7];
        for(int i = 0; i < 64; ++i)
        {
            std::uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            std::uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::uint32_t h[8];
    std::uint8_t block[64] = {};
    std::size_t fill = 0;
    std::uint64_t total = 0;
};

std::string Sha256Hex(const Bytes &bytes)
{
    Sha256 hash;
    hash.Update(bytes.data(), bytes.size());
    return hash.Hex();
}

std::string Hex(const Bytes &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for(std::uint8_t byte : bytes)
    {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

Bytes ReadFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    {
        throw std::runtime_error("cannot open " + path);
    }
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Bytes TestBlob(std::size_t size)
{
    Bytes blob(size);
    for(std::size_t i = 0; i < size; ++i)
    {
        blob[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
    }
    return blob;
}

std::string OneLine(std::string text)
{
    for(char &c : text)
    {
        if(c == '\n' || c == '\r')
        {
            c = ' ';
        }
    }
    return text;
}

class Dumper
{
public:
    explicit Dumper(const std::filesystem::path &scratch) : scratch(scratch) {}

    void Report(const std::string &path, const std::string &operation,
                const std::function<std::string()> &run)
    {
        try
        {
            std::string result = run();
            std::cout << path << '\t' << operation << "\tok\t" << OneLine(result) << '\n';
        }
        catch(const std::exception &e)
        {
            std::cout << path << '\t' << operation << "\trejected\t" << OneLine(e.what()) << '\n';
        }
        catch(...)
        {
            std::cout << path << '\t' << operation << "\trejected\t(unknown exception)\n";
        }
    }

    std::string Copy(const std::string &path)
    {
        std::filesystem::path target = this->scratch / ("copy-" + std::to_string(this->counter++));
        std::filesystem::copy_file(path, target, std::filesystem::copy_options::overwrite_existing);
        return target.string();
    }

    void Dump(const std::string &path)
    {
        Bytes head;
        try
        {
            head = ReadFile(path);
        }
        catch(const std::exception &e)
        {
            std::cout << path << "\tread\trejected\t" << OneLine(e.what()) << '\n';
            return;
        }

        std::string format = "other";
        if(head.size() >= 4 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F')
        {
            format = "ELF";
        }
        else if(head.size() >= 4 && head[0] == 0xFA && head[1] == 0xDE && head[2] == 0x0C &&
                head[3] == 0xC0)
        {
            format = "superblob";
        }
        else if(head.size() >= 2 && head[0] == 'M' && head[1] == 'Z')
        {
            format = "PE";
        }
        else if(head.size() >= 8 && std::memcmp(head.data(), "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0)
        {
            format = "MSI";
        }
        else
        {
            try
            {
                if(MachOParser::DetectFormat(path) != MachOParser::Format::NotMachO)
                {
                    format = "Mach-O";
                }
            }
            catch(...)
            {
            }
        }

        this->Report(path, "format", [&] { return format; });

        if(format == "ELF" || format == "PE" || format == "Mach-O")
        {
            this->Report(path, "dependencies", [&] {
                DependencyLister lister;
                DependencyResult result = lister.ListDependencies({path}, {});
                auto error = result.errors.find(path);
                if(error != result.errors.end())
                {
                    throw std::runtime_error(error->second);
                }
                std::string out;
                for(const auto &name : result.dependencies[path])
                {
                    out += name + ",";
                }
                return out;
            });
        }

        if(format == "PE")
        {
            this->Report(path, "digest", [&] {
                auto result = PeSigner::ComputeAuthenticodeDigest(path);
                return Hex(result.digest) + (result.is_pe32_plus ? " pe32+" : " pe32");
            });
            this->Report(path, "presence",
                         [&] { return PeSigner::HasEmbeddedSignature(path) ? "yes" : "no"; });
            this->Report(path, "extract", [&] {
                auto sig = PeSigner::ExtractSignature(path);
                return sig ? Sha256Hex(*sig) : std::string("none");
            });
            this->EmbedAndStrip(
                path, [](const std::string &p, const Bytes &b) { PeSigner::EmbedSignature(p, b); },
                [](const std::string &p) { PeSigner::StripSignature(p); });
        }
        else if(format == "MSI")
        {
            this->Report(path, "digest",
                         [&] { return Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest); });
            this->Report(path, "presence",
                         [&] { return MsiSigner::HasEmbeddedSignature(path) ? "yes" : "no"; });
            this->Report(path, "extract", [&] {
                auto sig = MsiSigner::ExtractSignature(path);
                return sig ? Sha256Hex(*sig) : std::string("none");
            });
            this->EmbedAndStrip(
                path, [](const std::string &p, const Bytes &b) { MsiSigner::EmbedSignature(p, b); },
                [](const std::string &p) { MsiSigner::StripSignature(p); });
        }
        else if(format == "Mach-O")
        {
            this->Report(path, "macho-dependencies", [&] {
                std::string out;
                for(const auto &name : MachOParser::ListDependencies(path))
                {
                    out += name + ",";
                }
                return out;
            });
            this->Report(path, "slices", [&] {
                std::ostringstream out;
                for(const auto &slice : MachOParser::GetArchSlices(path))
                {
                    out << slice.cpu_type << ':' << slice.cpu_subtype << ':' << slice.offset << ':'
                        << slice.size << ',';
                }
                return out.str();
            });
            this->Report(path, "digest", [&] {
                auto result = MachOSigner::PrepareSignature(path, "reference", 1500);
                std::string out;
                for(const auto &slice : result.slices)
                {
                    out += Hex(slice.cd_hash) + ",";
                }
                return out;
            });
            this->Report(path, "presence",
                         [&] { return MachOSigner::HasEmbeddedSignature(path) ? "yes" : "no"; });
            this->Report(path, "extract", [&] {
                auto sig = MachOSigner::ExtractSignature(path);
                return sig ? Sha256Hex(*sig) : std::string("none");
            });
            this->Report(path, "embed", [&] {
                std::string copy = this->Copy(path);
                auto prepared = MachOSigner::PrepareSignature(copy, "reference", 1500);
                MachOSigner::CompleteSignature(copy, prepared,
                                               std::vector<Bytes>(prepared.slices.size(), TestBlob(1500)));
                return Sha256Hex(ReadFile(copy));
            });
        }
        else if(format == "superblob")
        {
            this->Report(path, "superblob-cms", [&] {
                auto part = MachOSigner::ExtractCmsFromSuperBlob(head);
                return part ? Sha256Hex(*part) : std::string("none");
            });
            this->Report(path, "superblob-code-directory", [&] {
                auto part = MachOSigner::ExtractCodeDirectoryFromSuperBlob(head);
                return part ? Sha256Hex(*part) : std::string("none");
            });
        }
    }

private:
    void EmbedAndStrip(const std::string &path,
                       const std::function<void(const std::string &, const Bytes &)> &embed,
                       const std::function<void(const std::string &)> &strip)
    {
        std::string copy;
        this->Report(path, "embed", [&] {
            copy = this->Copy(path);
            embed(copy, TestBlob(1500));
            return Sha256Hex(ReadFile(copy));
        });
        this->Report(path, "strip", [&] {
            if(copy.empty())
            {
                throw std::runtime_error("no embedded copy to strip");
            }
            strip(copy);
            return Sha256Hex(ReadFile(copy));
        });
    }

    std::filesystem::path scratch;
    unsigned counter = 0;
};

} // namespace

int main(int argc, char **argv)
{
    if(argc != 2)
    {
        std::cerr << "usage: reference-dump <list file>\n";
        return 2;
    }

    std::ifstream list(argv[1]);
    if(!list)
    {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 2;
    }

    std::filesystem::path scratch = std::filesystem::temp_directory_path() /
                                    ("reference-dump-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(scratch);
    Dumper dumper(scratch);

    std::string line;
    while(std::getline(list, line))
    {
        if(line.empty() || line[0] == '#')
        {
            continue;
        }
        dumper.Dump(line);
    }

    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    return 0;
}
