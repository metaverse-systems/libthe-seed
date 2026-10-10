#include "PakTestSupport.hpp"
#include "TestPaths.hpp"
#include <libthe-seed/ResourcePak.hpp>
#include <libthe-seed/LoadError.hpp>
#include <filesystem>
#include <fstream>
#include <cstdint>

static std::string createTestPak(const std::string &dir, const std::string &name,
                                  const std::vector<uint8_t> &payload)
{
    // Build header JSON: headerSize includes the header line + newline
    nlohmann::json header;
    header["resources"] = nlohmann::json::array();
    nlohmann::json res;
    res["name"] = name;
    res["size"] = payload.size();
    header["resources"].push_back(res);

    // We need to know the header size before writing it, but header size is
    // part of the header. Serialize without headerSize first, then adjust.
    header["headerSize"] = 0;
    std::string raw = header.dump();
    // headerSize = length of final header line + 1 (newline)
    // Re-serialize with correct value — the digit count may change the length,
    // so iterate until stable.
    for (int i = 0; i < 5; ++i)
    {
        header["headerSize"] = std::to_string(raw.size() + 1); // +1 for '\n'
        raw = header.dump();
    }

    std::string path = std::string(dir) + "/test_resource.pak";
    std::ofstream out(path, std::ios::binary);
    out << raw << '\n';
    out.write(reinterpret_cast<const char *>(payload.data()), payload.size());
    out.close();
    return path;
}

TEST_CASE("ResourcePak round-trip loads Resource::Data correctly", "[ResourcePak]")
{
    // Known payload bytes
    std::vector<uint8_t> expected = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};

    seedtest::ScratchDir scratch;
    std::string pakPath = createTestPak(scratch.Path().string(), "test_res", expected);

    ResourcePak pak(pakPath);

    SECTION("Load returns Resource with matching Data vector")
    {
        auto resource = pak.Load("test_res");
        REQUIRE(resource.Data.size() == expected.size());
        REQUIRE(resource.Data == expected);
    }

    SECTION("ResourceNames lists the resource")
    {
        auto names = pak.ResourceNames();
        REQUIRE(names.size() == 1);
        REQUIRE(names[0] == "test_res");
    }
}

TEST_CASE("ResourcePak refuses a pak whose resource size exceeds the file", "[ResourcePak]")
{
    // Build a PAK where the JSON header declares a resource size larger than
    // the actual payload data appended after the header.
    nlohmann::json header;
    header["resources"] = nlohmann::json::array();
    nlohmann::json res;
    res["name"] = "oversized";
    res["size"] = 9999; // Way larger than actual payload
    header["resources"].push_back(res);
    header["headerSize"] = 0;

    std::string raw = header.dump();
    for(int i = 0; i < 5; ++i)
    {
        header["headerSize"] = std::to_string(raw.size() + 1);
        raw = header.dump();
    }

    seedtest::ScratchDir scratch;
    std::string pakPath = scratch.File("test_bounds.pak");
    {
        std::ofstream out(pakPath, std::ios::binary);
        out << raw << '\n';
        // Write only 4 bytes of actual payload (header claims 9999)
        std::vector<std::uint8_t> tiny = {0x01, 0x02, 0x03, 0x04};
        out.write(reinterpret_cast<const char *>(tiny.data()),
                  static_cast<std::streamsize>(tiny.size()));
    }

    // The description is checked against the file length when the pak is
    // opened, so the pak is refused before any resource is asked for.
    try
    {
        ResourcePak pak(pakPath);
        FAIL("the pak was accepted");
    }
    catch(const LoadError &error)
    {
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotLoadable);
        REQUIRE(error.DetailGet().rfind("damaged:", 0) == 0);
    }
}

TEST_CASE("A resource loaded from a pak is shared read-only by the world", "[ResourcePak]")
{
    std::vector<uint8_t> expected = {0xCA, 0xFE, 0xBA, 0xBE, 0x10, 0x20, 0x30, 0x40, 0x50};
    seedtest::ScratchDir scratch;
    std::string pakPath = createTestPak(scratch.Path().string(), "shared_res", expected);

    ResourcePak pak(pakPath);
    ecs::Manager manager;
    auto *world = manager.Container("world");
    pak.Load(world, "shared_res");

    SECTION("The retrieved resource has the bytes of the pak entry")
    {
        std::shared_ptr<const ecs::Resource> found = world->ResourceGet("shared_res");
        REQUIRE(found);
        REQUIRE(found->Data == expected);
    }

    SECTION("An unknown name gives an empty result")
    {
        std::shared_ptr<const ecs::Resource> found;
        REQUIRE_NOTHROW(found = world->ResourceGet("not_in_the_pak"));
        REQUIRE_FALSE(found);
    }

    SECTION("Two retrievals share one stored object")
    {
        // The vector that Load produces is moved into the world, so its buffer cannot be observed
        // from here; the bytes and the sharing are what this checks.
        auto first = world->ResourceGet("shared_res");
        auto second = world->ResourceGet("shared_res");
        REQUIRE(first);
        REQUIRE(first.get() == second.get());
        REQUIRE(first->Data.data() == second->Data.data());
    }

    SECTION("A retrieved resource outlives the world")
    {
        auto manager2 = std::make_unique<ecs::Manager>();
        auto *other = manager2->Container("other");
        pak.Load(other, "shared_res");
        auto held = other->ResourceGet("shared_res");
        manager2.reset();
        REQUIRE(held);
        REQUIRE(held->Data == expected);
    }
}

TEST_CASE("LoadAll puts every pak resource in the world", "[ResourcePak]")
{
    std::vector<uint8_t> expected = {0x01, 0x02, 0x03};
    seedtest::ScratchDir scratch;
    std::string pakPath = createTestPak(scratch.Path().string(), "only_res", expected);

    ResourcePak pak(pakPath);
    ecs::Manager manager;
    auto *world = manager.Container("world");
    pak.LoadAll(world);

    auto found = world->ResourceGet("only_res");
    REQUIRE(found);
    REQUIRE(found->Data == expected);
}

namespace
{
    std::vector<uint8_t> Pattern(std::size_t count, uint8_t first)
    {
        std::vector<uint8_t> bytes(count);
        for(std::size_t i = 0; i < count; ++i)
        {
            bytes[i] = static_cast<uint8_t>(first + i * 3);
        }
        return bytes;
    }

    // Several resources in an order that is not alphabetical, one of them empty.
    seedtest::PakSpec SeveralSpec()
    {
        seedtest::PakSpec spec;
        spec.name = "seed/several";
        spec.resources.push_back({"zeta", Pattern(300, 1), ""});
        spec.resources.push_back({"alpha", Pattern(5, 40), ""});
        spec.resources.push_back({"empty", {}, ""});
        spec.resources.push_back({"mid", Pattern(70000, 9), ""});
        return spec;
    }
}

TEST_CASE("ResourcePak reads a writer's pak exactly", "[ResourcePak]")
{
    seedtest::ScratchDir scratch;
    const seedtest::PakSpec spec = SeveralSpec();
    const std::string path = seedtest::WritePakFile(scratch.Path(), "several.pak", spec).string();

    SECTION("Every resource comes back with its exact bytes")
    {
        ResourcePak pak(path);
        for(const auto &resource : spec.resources)
        {
            INFO(resource.name);
            REQUIRE(pak.Load(resource.name).Data == resource.bytes);
        }
    }

    SECTION("A zero-size resource is an empty resource")
    {
        ResourcePak pak(path);
        ecs::Resource resource = pak.Load("empty");
        REQUIRE(resource.Data.empty());
    }

    SECTION("ResourceNames are in description order and callable on a const pak")
    {
        const ResourcePak pak(path);
        const std::vector<std::string> names = pak.ResourceNames();
        REQUIRE(names == std::vector<std::string>{"zeta", "alpha", "empty", "mid"});
    }

    SECTION("A copy loads the same bytes")
    {
        ResourcePak pak(path);
        ResourcePak copy(pak);
        REQUIRE(copy.Load("mid").Data == spec.resources[3].bytes);
        REQUIRE(pak.Load("mid").Data == spec.resources[3].bytes);
        REQUIRE(copy.ResourceNames() == pak.ResourceNames());
    }

    SECTION("LoadAll adds every resource to the container")
    {
        ResourcePak pak(path);
        ecs::Manager manager;
        auto *world = manager.Container("world");
        pak.LoadAll(world);
        for(const auto &resource : spec.resources)
        {
            INFO(resource.name);
            auto found = world->ResourceGet(resource.name);
            REQUIRE(found);
            REQUIRE(found->Data == resource.bytes);
        }
    }

    SECTION("Load into a container adds that resource")
    {
        ResourcePak pak(path);
        ecs::Manager manager;
        auto *world = manager.Container("world");
        pak.Load(world, "alpha");
        auto found = world->ResourceGet("alpha");
        REQUIRE(found);
        REQUIRE(found->Data == spec.resources[1].bytes);
        REQUIRE_FALSE(world->ResourceGet("zeta"));
    }
}

TEST_CASE("ResourcePak reads a pak with no resources", "[ResourcePak]")
{
    seedtest::ScratchDir scratch;
    seedtest::PakSpec spec;
    spec.name = "seed/none";
    const std::string path = seedtest::WritePakFile(scratch.Path(), "none.pak", spec).string();

    ResourcePak pak(path);
    REQUIRE(pak.ResourceNames().empty());
    ecs::Manager manager;
    auto *world = manager.Container("world");
    REQUIRE_NOTHROW(pak.LoadAll(world));
    REQUIRE_FALSE(world->ResourceGet("anything"));
}

TEST_CASE("ResourcePak on a missing path is not found", "[ResourcePak]")
{
    seedtest::ScratchDir scratch;
    const std::string path = scratch.File("absent.pak");

    try
    {
        ResourcePak pak(path);
        FAIL("a missing pak was accepted");
    }
    catch(const LoadError &error)
    {
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(error.NameGet() == path);
        REQUIRE(error.FileGet() == path);
        REQUIRE(error.LocationsGet().empty());
        REQUIRE_FALSE(error.DetailGet().empty());
#ifndef _WIN32
        REQUIRE(error.DetailGet().find("No such file or directory") != std::string::npos);
#endif
        const std::string message = error.what();
        REQUIRE(message.find(path + ": " + error.DetailGet()) != std::string::npos);
    }
    catch(...)
    {
        FAIL("an exception other than LoadError escaped");
    }
}

TEST_CASE("ResourcePak reports a pak damaged after it was opened", "[ResourcePak]")
{
    seedtest::ScratchDir scratch;
    const seedtest::PakSpec spec = SeveralSpec();
    const std::vector<std::uint8_t> sound = seedtest::PakBytes(spec);
    const std::filesystem::path path = seedtest::WritePakFile(scratch.Path(), "several.pak", spec);

    ResourcePak pak(path.string());
    REQUIRE(pak.Load("alpha").Data == spec.resources[1].bytes);

    SECTION("A pak truncated after the constructor is checked again by the next Load")
    {
        seedtest::WriteBytes(path, seedtest::PakTruncate(sound, 10));

        try
        {
            pak.Load("zeta");
            FAIL("a truncated pak was read");
        }
        catch(const LoadError &error)
        {
            REQUIRE(error.ReasonGet() == LoadError::Reason::NotLoadable);
            REQUIRE(error.FileGet() == path.string());
            REQUIRE(error.DetailGet() == "damaged: the description claims " + std::to_string(sound.size()) +
                                             " bytes but the file holds " + std::to_string(sound.size() - 10));
        }
        catch(...)
        {
            FAIL("an exception other than LoadError escaped");
        }
    }

    SECTION("Data appended after the constructor is reported by LoadAll")
    {
        seedtest::WriteBytes(path, seedtest::PakAppend(sound, 7));

        ecs::Manager manager;
        auto *world = manager.Container("world");
        try
        {
            pak.LoadAll(world);
            FAIL("a pak with trailing data was read");
        }
        catch(const LoadError &error)
        {
            REQUIRE(error.ReasonGet() == LoadError::Reason::NotLoadable);
            REQUIRE(error.DetailGet() == "damaged: 7 bytes of unexplained data after the last resource");
        }
        catch(...)
        {
            FAIL("an exception other than LoadError escaped");
        }
        REQUIRE_FALSE(world->ResourceGet("zeta"));
    }
}
