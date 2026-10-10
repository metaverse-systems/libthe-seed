// What a request for a resource pak costs.
//
// A pak of 256 MiB (1,025 resources: one of 1 KiB and 1,024 of 256 KiB) is
// generated here by PakTestSupport.hpp and never checked in. Single-threaded.
//
//   - the first request, for the 1 KiB resource, reads the description, the
//     resource and less than 1 MiB more, and grows the heap by less than 2 MiB;
//   - a second request through the same loader reads no description and does
//     not parse it again;
//   - every resource, requested by name and all at once, grows the heap by no
//     more than 110% of the bytes returned (plus a small fixed allowance for
//     the bookkeeping of a request, which matters only for the 1 KiB resource);
//   - ResourcePak::Load(name) reads only that resource.

#include "HeapCounter.hpp"
#include "PakTestSupport.hpp"
#include "TestPaths.hpp"

#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>

#include "internal/PakFile.hpp"
#include "internal/PakIndex.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

using namespace seed::internal;

namespace
{
    constexpr std::uint64_t kMebibyte = 1024 * 1024;
    // Bookkeeping of one request: the map node, the shared pointer, the name.
    constexpr std::uint64_t kRequestAllowance = 16 * 1024;

    // The bytes of the description line and its newline.
    std::uint64_t DescriptionBytes(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        std::getline(in, line);
        return line.size() + 1;
    }

    struct Pak
    {
        seedtest::ScratchDir scratch;
        std::filesystem::path path;
        std::uint64_t description = 0;
        std::uint64_t total = 0;

        Pak() : scratch("pak-cost")
        {
            this->path = seedtest::WriteLargePak(this->scratch.Path(), "cost");
            this->description = DescriptionBytes(this->path);
            for(std::size_t i = 0; i < seedtest::LargePakResourceCount; ++i)
            {
                this->total += seedtest::LargePakResourceSize(i);
            }
        }
    };

    std::uint64_t Returned(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources)
    {
        std::uint64_t bytes = 0;
        for(const auto &entry : resources)
        {
            bytes += entry.second->Data.size();
        }
        return bytes;
    }
}

TEST_CASE("a request reads and holds only what it asks for", "[PakCost]")
{
    REQUIRE(seedtest::heap::HeapGrowthScope::Active());
    Pak pak;
    PakLoader loader;
    loader.PathAdd(pak.scratch.Path().string());

    // One 1 KiB resource of 256 MiB.
    {
        ResetPakBytesReadCount();
        ResetPakDescriptionParseCount();
        std::size_t growth = 0;
        {
            seedtest::heap::HeapGrowthScope heap;
            auto resources = loader.Load("cost", {seedtest::LargePakResourceName(0)});
            growth = heap.PeakGrowth();
            REQUIRE(resources.size() == 1);
            const auto &data = resources.at("small")->Data;
            REQUIRE(data.size() == 1024);
            for(std::size_t i = 0; i < data.size(); ++i)
            {
                REQUIRE(data[i] == seedtest::LargePakByte(0, i));
            }
        }
        const std::uint64_t read = PakBytesReadCount();
        std::cout << "pak cost: one 1 KiB resource: read " << read << " bytes (description " << pak.description
                  << "), heap growth " << growth << " bytes" << std::endl;
        CHECK(read >= pak.description + 1024);
        CHECK(read < pak.description + 1024 + kMebibyte);
        CHECK(growth < 2 * kMebibyte);
        CHECK(PakDescriptionParseCount() == 1);
    }

    // A second resource through the same loader.
    {
        ResetPakBytesReadCount();
        auto resources = loader.Load("cost", {seedtest::LargePakResourceName(1)});
        const std::uint64_t read = PakBytesReadCount();
        std::cout << "pak cost: second resource, same pak: read " << read << " bytes, description parses "
                  << (PakDescriptionParseCount() - 1) << std::endl;
        REQUIRE(resources.size() == 1);
        CHECK(resources.begin()->second->Data.size() == 256 * 1024);
        // Only the resource: no description bytes.
        CHECK(read == 256 * 1024);
        CHECK(PakDescriptionParseCount() == 1);
    }

    // Every resource by name, the heap grows by about what is returned.
    for(std::size_t i = 0; i < seedtest::LargePakResourceCount; ++i)
    {
        const std::string name = seedtest::LargePakResourceName(i);
        INFO(name);
        std::size_t growth = 0;
        std::uint64_t returned = 0;
        {
            seedtest::heap::HeapGrowthScope heap;
            auto resources = loader.Load("cost", {name});
            growth = heap.PeakGrowth();
            returned = Returned(resources);
            REQUIRE(resources.size() == 1);
        }
        REQUIRE(returned == seedtest::LargePakResourceSize(i));
        CHECK(growth <= returned * 11 / 10 + kRequestAllowance);
    }
    CHECK(PakDescriptionParseCount() == 1);

    // Every resource at once.
    {
        ResetPakBytesReadCount();
        std::size_t growth = 0;
        std::uint64_t returned = 0;
        {
            seedtest::heap::HeapGrowthScope heap;
            auto resources = loader.Load("cost");
            growth = heap.PeakGrowth();
            returned = Returned(resources);
            REQUIRE(resources.size() == seedtest::LargePakResourceCount);
        }
        const std::uint64_t read = PakBytesReadCount();
        std::cout << "pak cost: every resource: read " << read << " bytes, heap growth " << growth << " bytes ("
                  << (growth * 100 / returned) << "% of returned)" << std::endl;
        CHECK(returned == pak.total);
        CHECK(read == pak.total);
        CHECK(growth <= returned * 11 / 10);
    }
}

TEST_CASE("ResourcePak::Load(name) reads only that resource", "[PakCost]")
{
    Pak pak;
    ResourcePak resource_pak(pak.path.string());

    ResetPakBytesReadCount();
    ecs::Resource resource = resource_pak.Load(seedtest::LargePakResourceName(7));
    CHECK(PakBytesReadCount() == 256 * 1024);
    REQUIRE(resource.Data.size() == 256 * 1024);
    for(std::size_t i = 0; i < resource.Data.size(); i += 4099)
    {
        REQUIRE(resource.Data[i] == seedtest::LargePakByte(7, i));
    }

    ResetPakBytesReadCount();
    ecs::Resource small = resource_pak.Load("small");
    CHECK(PakBytesReadCount() == 1024);
    CHECK(small.Data.size() == 1024);
}
