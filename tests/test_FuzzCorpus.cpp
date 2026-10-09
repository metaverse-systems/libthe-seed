// Replays the samples and every file committed under tests/fuzz/corpus/<target>
// through the fuzz target functions with the standard compiler, without
// mutation. A committed corpus file is a minimised input that once broke the
// library; it must now be handled cleanly.

#include "MalformedInput.hpp"
#include "MsiTestSupport.hpp"

#include "fuzz/FuzzTargets.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifndef FUZZ_CORPUS_DIR
#error "FUZZ_CORPUS_DIR must be defined by the build"
#endif

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;
using Target = void (*)(const std::uint8_t *, std::size_t);

void Replay(Target target, const Bytes &input)
{
    target(input.data(), input.size());
}

// Every regular file under the committed corpus directory of one target,
// ignoring the placeholder that keeps an empty directory in version control.
std::vector<std::filesystem::path> CorpusFiles(const std::string &name)
{
    std::vector<std::filesystem::path> files;
    const std::filesystem::path dir = std::filesystem::path(FUZZ_CORPUS_DIR) / name;
    std::error_code ec;
    if(!std::filesystem::is_directory(dir, ec))
    {
        FAIL("Corpus directory is missing: " << dir.string());
    }
    for(const auto &entry : std::filesystem::directory_iterator(dir))
    {
        if(entry.is_regular_file() && entry.path().filename() != ".gitkeep")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void ReplayCorpus(const std::string &name, Target target)
{
    for(const auto &file : CorpusFiles(name))
    {
        INFO("corpus file " << file.string());
        Replay(target, seedtest::malformed::ReadAll(file.string()));
    }
}

} // namespace

TEST_CASE("corpus: elf", "[FuzzCorpus]")
{
    Replay(seedfuzz::FuzzElf, seedfuzz::ElfSeed(true, true));
    Replay(seedfuzz::FuzzElf, seedfuzz::ElfSeed(false, false));
    Replay(seedfuzz::FuzzElf, Bytes{});
    ReplayCorpus("elf", seedfuzz::FuzzElf);
}

TEST_CASE("corpus: pe", "[FuzzCorpus]")
{
    Replay(seedfuzz::FuzzPe, seedtest::malformed::LoadSample("tiny.exe"));
    Replay(seedfuzz::FuzzPe, seedtest::malformed::LoadSample("test.dll"));
    Replay(seedfuzz::FuzzPe, Bytes{});
    ReplayCorpus("pe", seedfuzz::FuzzPe);
}

TEST_CASE("corpus: macho", "[FuzzCorpus]")
{
    Replay(seedfuzz::FuzzMachO, seedtest::malformed::LoadSample("tiny-macho-x86_64"));
    Replay(seedfuzz::FuzzMachO, seedtest::malformed::LoadSample("tiny-macho-arm64"));
    Replay(seedfuzz::FuzzMachO, seedtest::malformed::LoadSample("tiny-macho-universal"));
    // The inputs of PrepareSignature and CompleteSignature: signed by another
    // tool, no room, exactly enough room, 64-bit table, a library, data after
    // the signature.
    for(const char *name : {"tiny-macho-arm64-adhoc", "tiny-macho-x86_64-adhoc", "tiny-macho-universal-adhoc",
                            "tiny-macho-x86_64-nospace", "tiny-macho-x86_64-exactfit", "tiny-macho-universal64",
                            "tiny-macho-dylib-arm64", "tiny-macho-x86_64-data-after-sig"})
    {
        INFO("sample " << name);
        Replay(seedfuzz::FuzzMachO, seedtest::malformed::LoadSample(name));
    }
    Replay(seedfuzz::FuzzMachO, Bytes{});
    ReplayCorpus("macho", seedfuzz::FuzzMachO);
}

TEST_CASE("corpus: superblob", "[FuzzCorpus]")
{
    Replay(seedfuzz::FuzzSuperBlob, seedfuzz::SuperBlobSeed());
    Replay(seedfuzz::FuzzSuperBlob, Bytes{});
    ReplayCorpus("superblob", seedfuzz::FuzzSuperBlob);
}

TEST_CASE("corpus: msi", "[FuzzCorpus]")
{
    for(const char *name : {"tiny.msi", "tiny-v4.msi", "tiny-osslsig-small.msi", "tiny-osslsig-large.msi",
                            "tiny-osslsig-dse.msi", "nested.msi", "nested-osslsig.msi", "two-neighbours.msi",
                            "legacy-the-seed-0.6.0.msi"})
    {
        INFO("sample " << name);
        Replay(seedfuzz::FuzzMsi, seedtest::malformed::LoadSample(name));
    }
    Replay(seedfuzz::FuzzMsi, Bytes{});
    ReplayCorpus("msi", seedfuzz::FuzzMsi);
}

TEST_CASE("corpus: msi packages the library writes", "[FuzzCorpus]")
{
    // The writer's own output is an input of every later operation: the
    // samples and generated shapes (both sector sizes, nested storages,
    // many entries, names that order differently) signed with a signature
    // of each placement, and stripped again, replayed without mutation.
    namespace cfb = seedtest::cfb;
    seedtest::ScratchDir scratch("fuzz-corpus-msi");
    const std::string path = scratch.File("written.msi");
    std::vector<Bytes> inputs;
    for(const char *name : {"tiny.msi", "tiny-v4.msi", "nested.msi", "tiny-osslsig-small.msi", "tiny-osslsig-large.msi",
                            "tiny-osslsig-dse.msi", "nested-osslsig.msi", "two-neighbours.msi",
                            "legacy-the-seed-0.6.0.msi"})
    {
        inputs.push_back(seedtest::malformed::LoadSample(name));
    }
    for(const auto &shape : cfb::shapes::All())
    {
        for(const unsigned version : {3u, 4u})
        {
            cfb::BuildOptions options;
            options.version = version;
            inputs.push_back(cfb::Build(shape.root, options));
        }
    }
    for(const Bytes &input : inputs)
    {
        for(const std::size_t size : {100u, 4096u})
        {
            seedtest::malformed::WriteScratch(scratch, "written.msi", input);
            MsiSigner::EmbedSignature(path, cfb::PatternBytes(size, 3));
            Replay(seedfuzz::FuzzMsi, seedtest::malformed::ReadAll(path));
            (void)MsiSigner::StripSignature(path);
            Replay(seedfuzz::FuzzMsi, seedtest::malformed::ReadAll(path));
        }
    }
}
