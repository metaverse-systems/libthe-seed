#include "NameParser.hpp"
#include <libthe-seed/LoadError.hpp>

namespace
{
    [[noreturn]] void Reject(const std::string &name, const std::string &kind, const std::string &rule)
    {
        throw LoadError(LoadError::Reason::InvalidName, name, "", {}, rule, kind);
    }

    void PartCheck(const std::string &part, const std::string &name, const std::string &kind)
    {
        if(part.empty())
        {
            Reject(name, kind, "empty part");
        }
        if(part == "." || part == "..")
        {
            Reject(name, kind, "\".\" or \"..\" as a part");
        }
    }
}

NameParser::NameParser(const std::string &name, const std::string &kind)
{
    for(char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if(u < 0x20 || u == 0x7f)
        {
            Reject(name, kind, "control character");
        }
    }
    if(name.find('\\') != std::string::npos)
    {
        Reject(name, kind, "backslash");
    }
    if(name.find(':') != std::string::npos)
    {
        Reject(name, kind, "\":\"");
    }

    // A leading slash followed by a name is an absolute path; a lone slash
    // or doubled slashes are empty parts.
    if(name.size() > 1 && name.front() == '/' && name[1] != '/')
    {
        Reject(name, kind, "leading \"/\"");
    }

    const size_t slash = name.find('/');
    if(slash == std::string::npos)
    {
        PartCheck(name, name, kind);
        this->library = name;
        return;
    }

    if(name.find('/', slash + 1) != std::string::npos)
    {
        // Doubled slashes leave an empty part; report that rule.
        if(name.find("//") != std::string::npos)
        {
            Reject(name, kind, "empty part");
        }
        Reject(name, kind, "more than one \"/\"");
    }

    this->org = name.substr(0, slash);
    this->library = name.substr(slash + 1);
    PartCheck(this->org, name, kind);
    PartCheck(this->library, name, kind);
}
