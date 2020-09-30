#ifndef __WIN32
#include <cstring>
#include <LIEF/Abstract/Binary.hpp>
#include <LIEF/Abstract/Parser.hpp>

extern "C"
{
    int depCount(char *filename)
    {
        std::unique_ptr<const LIEF::Binary> binary { LIEF::Parser::parse(filename) };
        return binary->imported_libraries().size();
    }

    void depGet(char *filename, char *library, int index)
    {
        std::unique_ptr<const LIEF::Binary> binary { LIEF::Parser::parse(filename) };

        std::string name = binary->imported_libraries()[index];
        strcpy(library, name.c_str());
        return;
    }
}
#else
#include <stdexcept>

extern "C"
{
    int depCount(char *filename)
    {
        throw std::runtime_error("depCount() not available in windows binary");
        return -1;
    }

    void depGet(char *filename, char *library, int index)
    {
        throw std::runtime_error("depGet() not available in windows binary");
        return;
    }
}
#endif
