// libFuzzer entry point. The target is chosen at compile time with
// -DSEED_FUZZ_TARGET=<function name from FuzzTargets.hpp>, for example
// -DSEED_FUZZ_TARGET=FuzzPe.
//
// Helper mode: when the environment variable SEED_FUZZ_WRITE_SEEDS names a
// directory, the binary writes the seed files that are built in code for its
// target (two ELF images, or a SuperBlob) into that directory and exits
// without fuzzing. run-fuzzers.sh uses it to seed the working corpus.

#include "FuzzTargets.hpp"

#include "../HeapCounter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifndef SEED_FUZZ_TARGET
#error "SEED_FUZZ_TARGET must name one of the functions in FuzzTargets.hpp"
#endif

#define SEED_FUZZ_STRINGIFY_(x) #x
#define SEED_FUZZ_STRINGIFY(x) SEED_FUZZ_STRINGIFY_(x)
#define SEED_FUZZ_TARGET_NAME SEED_FUZZ_STRINGIFY(SEED_FUZZ_TARGET)

SEED_DEFINE_HEAP_COUNTER()

namespace
{

void WriteSeed(const std::string &directory, const std::string &name,
               const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(directory + "/" + name, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    const char *directory = std::getenv("SEED_FUZZ_WRITE_SEEDS");
    if(directory == nullptr || *directory == '\0')
    {
        return 0;
    }
    const std::string target = SEED_FUZZ_TARGET_NAME;
    if(target == "FuzzElf")
    {
        WriteSeed(directory, "elf64-little.bin", seedfuzz::ElfSeed(true, true));
        WriteSeed(directory, "elf32-big.bin", seedfuzz::ElfSeed(false, false));
    }
    else if(target == "FuzzSuperBlob")
    {
        WriteSeed(directory, "superblob.bin", seedfuzz::SuperBlobSeed());
    }
    std::_Exit(0);
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    seedfuzz::SEED_FUZZ_TARGET(data, size);
    return 0;
}
