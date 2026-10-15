// The pak writer of the-seed is TypeScript; the reader and the test
// generator (PakTestSupport.hpp) are C++. This test runs the writer's own
// header code under node and checks that
//
//   - the bytes it writes equal the bytes PakTestSupport.hpp produces for the
//     same input, so the generator cannot drift from the writer, and
//   - every resource loads back through ResourcePak and PakLoader with the
//     same name and the same bytes.

#include "LoaderTestSupport.hpp"
#include "PakTestSupport.hpp"
#include "TestPaths.hpp"
#include "ToolSupport.hpp"

#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // The lines of ResourcePak.build() (the-seed/src/ResourcePak.ts) that
    // build the description and write the pak, copied as they are, with the
    // class members `this.packageDir` and `this.package` turned into the two
    // constants below.
    const char *writerScript = R"JS(
const fs = require("fs");
const packageDir = process.argv[2];
const pkg = JSON.parse(fs.readFileSync(packageDir + "/package.json").toString());

    const resources = pkg.resources.map((r) => {
      return {
        name: r.name,
        size: r.size,
        attributes: r.attributes
      };
    });

    const header = {
      name: pkg.name,
      headerSize: JSON.stringify(resources)
        .length.toString()
        .padStart(10, "0"),
      resources: resources,
    };
    header.headerSize = (JSON.stringify(header).length + 1).toString().padStart(10, "0");

    const [, name] = pkg.name.split("/");
    fs.writeFileSync(packageDir + "/" + name + ".pak", JSON.stringify(header) + "\n");
    pkg.resources.forEach((r) => {
      fs.appendFileSync(packageDir + "/" + name + ".pak", fs.readFileSync(packageDir + "/" + r.filename));
    });
)JS";

    std::vector<std::uint8_t> Bytes(std::size_t count, std::uint8_t seed)
    {
        std::vector<std::uint8_t> bytes(count);
        for(std::size_t i = 0; i < count; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(seed + i * 13 + (i >> 7));
        }
        return bytes;
    }

    std::vector<std::uint8_t> FileRead(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // Lays out the package folder the writer reads (package.json and one file
    // per resource) in `dir`, runs the writer and returns the pak it wrote.
    std::vector<std::uint8_t> NodeWrites(const std::filesystem::path &dir, const seedtest::PakSpec &spec,
                                         const std::string &file_stem)
    {
        std::filesystem::create_directories(dir);
        nlohmann::json package;
        package["name"] = spec.name;
        package["resources"] = nlohmann::json::array();
        for(std::size_t i = 0; i < spec.resources.size(); ++i)
        {
            const seedtest::PakResourceSpec &resource = spec.resources[i];
            const std::string filename = "data" + std::to_string(i) + ".bin";
            seedtest::WriteBytes(dir / filename, resource.bytes);

            nlohmann::json entry;
            entry["name"] = resource.name;
            entry["filename"] = filename;
            entry["size"] = resource.bytes.size();
            if(!resource.attributes.empty())
            {
                entry["attributes"] = nlohmann::json::parse(resource.attributes);
            }
            package["resources"].push_back(entry);
        }
        {
            std::ofstream out(dir / "package.json", std::ios::binary);
            out << package.dump(2);
        }
        {
            std::ofstream out(dir / "writer.js", std::ios::binary);
            out << writerScript;
        }

        const seedtest::ToolRun run = seedtest::RunCommand(
            "node " + seedtest::ShellQuote((dir / "writer.js").string()) + " " + seedtest::ShellQuote(dir.string()));
        INFO("node output: " << run.output);
        REQUIRE(run.status == 0);

        return FileRead(dir / (file_stem + ".pak"));
    }

    struct Case
    {
        std::string label;
        seedtest::PakSpec spec;
    };

    std::vector<Case> SoundCases()
    {
        std::vector<Case> cases;

        {
            seedtest::PakSpec spec;
            spec.name = "scope/several";
            spec.resources.push_back({"logo", Bytes(1000, 1), ""});
            spec.resources.push_back({"click", Bytes(88, 50), ""});
            spec.resources.push_back({"level/one.json", Bytes(4096, 9), ""});
            cases.push_back({"several resources", spec});
        }
        {
            seedtest::PakSpec spec;
            spec.name = "scope/zero";
            spec.resources.push_back({"before", Bytes(10, 3), ""});
            spec.resources.push_back({"nothing", {}, ""});
            spec.resources.push_back({"after", Bytes(10, 77), ""});
            cases.push_back({"a zero-size resource", spec});
        }
        {
            seedtest::PakSpec spec;
            spec.name = "scope/lonely";
            spec.resources.push_back({"nothing", {}, ""});
            cases.push_back({"only a zero-size resource", spec});
        }
        {
            seedtest::PakSpec spec;
            spec.name = "scope/none";
            cases.push_back({"no resources", spec});
        }
        {
            seedtest::PakSpec spec;
            spec.name = "scope/attributes";
            spec.resources.push_back({"with", Bytes(20, 5), "{\"kind\":\"sound\",\"loop\":true,\"volume\":0.5}"});
            spec.resources.push_back({"without", Bytes(21, 6), ""});
            spec.resources.push_back({"list", Bytes(22, 7), "[1,2,{\"a\":null}]"});
            spec.resources.push_back({"text", Bytes(23, 8), "\"plain\""});
            cases.push_back({"attributes present and absent", spec});
        }
        {
            seedtest::PakSpec spec;
            spec.name = "scope/escapes";
            spec.resources.push_back({"quote\"inside", Bytes(5, 1), ""});
            spec.resources.push_back({"back\\slash", Bytes(6, 2), ""});
            spec.resources.push_back({"line\nbreak", Bytes(7, 3), ""});
            spec.resources.push_back({"tab\tand\rreturn", Bytes(8, 4), ""});
            spec.resources.push_back({"bell\x07" "and\x1f" "unit", Bytes(9, 5), ""});
            spec.resources.push_back({"form\fback\bspace", Bytes(10, 6), ""});
            cases.push_back({"names with quotes, backslashes and control characters", spec});
        }
        return cases;
    }

    std::string StemOf(const seedtest::PakSpec &spec)
    {
        return spec.name.substr(spec.name.find('/') + 1);
    }
}

TEST_CASE("The generator writes the same bytes as the-seed's pak writer", "[PakWriterCompat]")
{
    if(!seedtest::RequireTool("node"))
    {
        return;
    }

    for(const Case &test : SoundCases())
    {
        DYNAMIC_SECTION(test.label)
        {
            seedtest::ScratchDir scratch;
            const std::vector<std::uint8_t> written = NodeWrites(scratch.Path() / "node", test.spec, StemOf(test.spec));

            REQUIRE(written == seedtest::PakBytes(test.spec));
        }
    }
}

TEST_CASE("Paks from the-seed's pak writer load unchanged", "[PakWriterCompat]")
{
    if(!seedtest::RequireTool("node"))
    {
        return;
    }

    for(const Case &test : SoundCases())
    {
        DYNAMIC_SECTION(test.label)
        {
            seedtest::ScratchDir scratch;
            const std::filesystem::path package = scratch.Path() / "node";
            NodeWrites(package, test.spec, StemOf(test.spec));
            const std::filesystem::path pak = package / (StemOf(test.spec) + ".pak");

            SECTION("through ResourcePak")
            {
                ResourcePak loaded(pak.string());

                std::vector<std::string> expected_names;
                for(const seedtest::PakResourceSpec &resource : test.spec.resources)
                {
                    expected_names.push_back(resource.name);
                }
                REQUIRE(loaded.ResourceNames() == expected_names);

                for(const seedtest::PakResourceSpec &resource : test.spec.resources)
                {
                    INFO("resource " << resource.name);
                    REQUIRE(loaded.Load(resource.name).Data == resource.bytes);
                }
            }

            SECTION("through PakLoader")
            {
                PakLoader loader;
                loader.PathAdd(package.string());
                const auto resources = loader.Load(test.spec.name);

                REQUIRE(resources.size() == test.spec.resources.size());
                for(const seedtest::PakResourceSpec &resource : test.spec.resources)
                {
                    INFO("resource " << resource.name);
                    const auto found = resources.find(resource.name);
                    REQUIRE(found != resources.end());
                    REQUIRE(found->second->Data == resource.bytes);
                }
            }
        }
    }
}

TEST_CASE("The writer counts a non-ASCII name in UTF-16 units, not bytes", "[PakWriterCompat]")
{
    // Documents the known damaged output: the writer's headerSize is shorter
    // than the byte offset of the first resource. What the reader does with
    // such a pak is checked in the damaged-pak tests.
    if(!seedtest::RequireTool("node"))
    {
        return;
    }

    seedtest::PakSpec spec;
    spec.name = "scope/accents";
    spec.resources.push_back({"caf\xC3\xA9", Bytes(12, 1), ""});
    spec.resources.push_back({"emoji\xF0\x9F\x99\x82", Bytes(13, 2), ""});

    seedtest::ScratchDir scratch;
    const std::vector<std::uint8_t> written = NodeWrites(scratch.Path() / "node", spec, "accents");

    REQUIRE(written == seedtest::PakBytes(spec));

    std::size_t newline = 0;
    while(newline < written.size() && written[newline] != '\n')
    {
        ++newline;
    }
    REQUIRE(newline < written.size());
    const std::string line(written.begin(), written.begin() + static_cast<std::ptrdiff_t>(newline));
    const std::uint64_t stated = std::stoull(nlohmann::json::parse(line)["headerSize"].get<std::string>());

    // One unit less for the two byte character, two less for the four byte one.
    REQUIRE(stated != newline + 1);
    REQUIRE(stated + 3 == newline + 1);
}
