#include "TestPaths.hpp"
#include <libthe-seed/ResourcePak.hpp>
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

TEST_CASE("ResourcePak::Load throws on bounds-exceeding resource size", "[ResourcePak]")
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

    ResourcePak pak(pakPath);
    REQUIRE_THROWS_AS(pak.Load("oversized"), std::runtime_error);
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
