#pragma once

#include <stdexcept>
#include <string>
#include <vector>

/**
 * Every failure to load a plugin, a resource pak or a scene is reported as a
 * LoadError, which is a std::runtime_error. what() holds a summary line
 * followed by indented detail lines naming the plugin, the file and every
 * location that was searched. The accessors give the same facts in structured
 * form.
 */
class LoadError : public std::runtime_error
{
  public:
    enum class Reason
    {
        InvalidName,
        NotFound,
        NotLoadable,
        EntryPointMissing,
        NoObject,
        SceneUnopenable,
        SceneNotUnderstood,
        SceneComponentFailed,
        ResourceMissing
    };

    enum class LocationState
    {
        Searched,
        Missing,
        Found,
        NotReached,
        Unreadable
    };

    struct Location
    {
        std::string path;
        bool development;
        LocationState state;
        // The system's text for an Unreadable location; empty otherwise.
        std::string reason = "";
    };

    /**
     * kind describes what was being loaded ("component plugin",
     * "system plugin", "library", "resource pak") and is used in the message
     * only; it may be empty. missing lists the names a ResourceMissing error
     * could not find, in request order; it is empty for every other reason.
     */
    LoadError(Reason reason, std::string name, std::string file,
              std::vector<Location> locations, std::string detail,
              std::string kind = "", std::vector<std::string> missing = {});

    Reason ReasonGet() const;
    const std::string &NameGet() const;
    const std::string &FileGet() const;
    const std::vector<Location> &LocationsGet() const;
    const std::string &DetailGet() const;
    const std::string &KindGet() const;
    const std::vector<std::string> &MissingGet() const;

  private:
    Reason reason;
    std::string name;
    std::string file;
    std::vector<Location> locations;
    std::string detail;
    std::string kind;
    std::vector<std::string> missing;
};
