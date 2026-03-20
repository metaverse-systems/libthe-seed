#include <catch_amalgamated.hpp>
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

    std::string pakPath = createTestPak(FIXTURES_DIR, "test_res", expected);

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

    // Clean up
    std::remove(pakPath.c_str());
}
