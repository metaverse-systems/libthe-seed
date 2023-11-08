#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <libthe-seed/LibraryLoader.hpp>

TEST_CASE("LibraryLoader adds and retrieves paths correctly", "[LibraryLoader]") {
    auto loader = new LibraryLoader("LIEF");

    SECTION("PathAdd adds a path") {
        loader->PathAdd("/usr/lib/x86_64-linux-gnu");
        auto paths = loader->PathsGet();
        REQUIRE(paths.size() == 1);
        REQUIRE(paths[0] == "/usr/lib/x86_64-linux-gnu/libLIEF.so");
    }

    SECTION("PathsGet returns only valid paths") {
        loader->PathAdd("/nonexistent/path");
        loader->PathAdd("/usr/lib/x86_64-linux-gnu");
        auto paths = loader->PathsGet();
        REQUIRE(paths.size() == 1);
        REQUIRE(paths[0] == "/usr/lib/x86_64-linux-gnu/libLIEF.so");
    }
}

TEST_CASE("LibraryLoader loads libraries and functions correctly", "[LibraryLoader]") {
    auto loader = new LibraryLoader("LIEF");

    SECTION("FunctionGet retrieves function pointers") {
        loader->PathAdd("/usr/lib/x86_64-linux-gnu");
        // The Load method will be called indirectly via FunctionGet
        void *func = nullptr;
        REQUIRE_NOTHROW(func = loader->FunctionGet("elf_parse"));
        REQUIRE(func != nullptr);
    }

    SECTION("FunctionGet throws when the library cannot be found") {
        loader->PathAdd("/nonexistent/path");
        // The Load method will be called indirectly and should throw because the library cannot be found
        REQUIRE_THROWS_AS(loader->FunctionGet("elf_parse"), std::runtime_error);
    }

    SECTION("FunctionGet throws when the function cannot be found") {
        loader->PathAdd("/usr/lib/x86_64-linux-gnu");
        // The Load method will be called indirectly and should not throw here
        REQUIRE_NOTHROW(loader->FunctionGet("elf_parse"));
        // However, this should throw because the function does not exist
        REQUIRE_THROWS_AS(loader->FunctionGet("dwarf_parse"), std::runtime_error);
    }
}

TEST_CASE("LibraryLoader destructor frees resources", "[LibraryLoader]") {
    LibraryLoader *loader = new LibraryLoader("LIEF");
    loader->PathAdd("/usr/lib/x86_64-linux-gnu");
    auto func = loader->FunctionGet("elf_parse");
    REQUIRE(func != nullptr);
    delete loader; // Destructor will be called here
}
