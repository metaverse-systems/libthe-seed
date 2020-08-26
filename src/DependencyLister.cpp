#include <cstring>
#include <LIEF/PE.hpp>

extern "C"
{
    int depCount(char *filename)
    {
        std::unique_ptr<const LIEF::PE::Binary> binary { LIEF::PE::Parser::parse(filename) };
        return binary->imports().size();
    }

    void depGet(char *filename, char *dll, int index)
    {
        std::unique_ptr<const LIEF::PE::Binary> binary { LIEF::PE::Parser::parse(filename) };

        std::string name = binary->imports()[index].name();
        strcpy(dll, name.c_str());
        return;
    }
}
