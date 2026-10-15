#include <libthe-seed/LoadError.hpp>

#include <cstdio>
#include <utility>

namespace
{
    std::string Trim(std::string text)
    {
        while(!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        {
            text.pop_back();
        }
        return text;
    }

    // Control characters are shown as escapes so a name can never break the
    // message into extra lines or truncate it.
    std::string Escaped(const std::string &text)
    {
        std::string result;
        for(char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if(c == '\n')
            {
                result += "\\n";
            }
            else if(c == '\r')
            {
                result += "\\r";
            }
            else if(c == '\t')
            {
                result += "\\t";
            }
            else if(u < 0x20 || u == 0x7f)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\x%02X", u);
                result += buffer;
            }
            else
            {
                result.push_back(c);
            }
        }
        return result;
    }

    std::string Quoted(const std::string &text)
    {
        return "\"" + text + "\"";
    }

    std::string Subject(const std::string &kind, const std::string &name)
    {
        return (kind.empty() ? "" : kind + " ") + Quoted(Escaped(name));
    }

    const char *StateText(LoadError::LocationState state)
    {
        switch(state)
        {
            case LoadError::LocationState::Searched: return "searched";
            case LoadError::LocationState::Missing: return "does not exist";
            case LoadError::LocationState::Found: return "found";
            case LoadError::LocationState::NotReached: return "not reached";
            case LoadError::LocationState::Unreadable: return "could not be examined";
        }
        return "searched";
    }

    std::string Message(LoadError::Reason reason, const std::string &name, const std::string &file,
                        const std::vector<LoadError::Location> &locations,
                        const std::string &detail, const std::string &kind,
                        const std::vector<std::string> &missing)
    {
        const std::string reasonText = Trim(detail);
        std::string message;

        switch(reason)
        {
            case LoadError::Reason::InvalidName:
                message = "invalid " + (kind.empty() ? "" : kind + " ") + "name " + Quoted(Escaped(name)) + ": " + reasonText;
                break;

            case LoadError::Reason::NotFound:
            {
                message = Subject(kind, name) + " not found: ";
                if(locations.empty())
                {
                    if(reasonText.empty())
                    {
                        message += "no locations are configured";
                    }
                    else
                    {
                        message += file + ": " + reasonText;
                    }
                }
                else
                {
                    message += "no location holds " + file;
                }

                for(const LoadError::Location &location : locations)
                {
                    message += "\n  ";
                    if(location.development)
                    {
                        message += "development location, ";
                    }
                    message += std::string(StateText(location.state)) + ": " + location.path;
                    if(location.state == LoadError::LocationState::Unreadable && !location.reason.empty())
                    {
                        message += ": " + location.reason;
                    }
                }
                break;
            }

            case LoadError::Reason::NotLoadable:
            {
                const bool pak = kind == "resource pak";
                message = Subject(kind, name) + ": " + file + " is present but could not be " +
                          (pak ? "read" : "loaded") + ": " + reasonText;

                std::string before;
                for(const LoadError::Location &location : locations)
                {
                    if(location.state == LoadError::LocationState::Found)
                    {
                        break;
                    }
                    before += (before.empty() ? "" : ", ") + location.path;
                }
                if(!before.empty())
                {
                    message += "\n  searched before it: " + before;
                }
                break;
            }

            case LoadError::Reason::EntryPointMissing:
                message = Subject(kind, name) + ": " + file + " was loaded but has no entry point " + reasonText;
                break;

            case LoadError::Reason::NoObject:
                message = Subject(kind, name) + ": " + reasonText + " in " + file + " produced no object";
                break;

            case LoadError::Reason::SceneUnopenable:
                message = "scene file " + Quoted(file) + " could not be opened: " + reasonText;
                break;

            case LoadError::Reason::SceneNotUnderstood:
                message = "scene file " + Quoted(file) + " could not be understood: " + reasonText;
                break;

            case LoadError::Reason::SceneComponentFailed:
                message = "scene file " + Quoted(file) + ": " + reasonText;
                break;

            case LoadError::Reason::ResourceMissing:
            {
                message = Subject(kind, name) + ": " + file + " does not contain ";
                for(size_t i = 0; i < missing.size(); ++i)
                {
                    message += (i == 0 ? "" : ", ") + Quoted(Escaped(missing[i]));
                }
                break;
            }
        }

        return Trim(message);
    }
}

LoadError::LoadError(Reason reason, std::string name, std::string file,
                     std::vector<Location> locations, std::string detail, std::string kind,
                     std::vector<std::string> missing)
    : std::runtime_error(Message(reason, name, file, locations, detail, kind, missing)),
      reason(reason),
      name(std::move(name)),
      file(std::move(file)),
      locations(std::move(locations)),
      detail(std::move(detail)),
      kind(std::move(kind)),
      missing(std::move(missing))
{
}

LoadError::Reason LoadError::ReasonGet() const
{
    return this->reason;
}

const std::string &LoadError::NameGet() const
{
    return this->name;
}

const std::string &LoadError::FileGet() const
{
    return this->file;
}

const std::vector<LoadError::Location> &LoadError::LocationsGet() const
{
    return this->locations;
}

const std::string &LoadError::DetailGet() const
{
    return this->detail;
}

const std::string &LoadError::KindGet() const
{
    return this->kind;
}

const std::vector<std::string> &LoadError::MissingGet() const
{
    return this->missing;
}
