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
        SceneComponentFailed
    };

    enum class LocationState
    {
        Searched,
        Missing,
        Found,
        NotReached
    };

    struct Location
    {
        std::string path;
        bool development;
        LocationState state;
    };

    /**
     * kind describes what was being loaded ("component plugin",
     * "system plugin", "library", "resource pak") and is used in the message
     * only; it may be empty.
     */
    LoadError(Reason reason, std::string name, std::string file,
              std::vector<Location> locations, std::string detail,
              std::string kind = "");

    Reason ReasonGet() const;
    const std::string &NameGet() const;
    const std::string &FileGet() const;
    const std::vector<Location> &LocationsGet() const;
    const std::string &DetailGet() const;
    const std::string &KindGet() const;

  private:
    Reason reason;
    std::string name;
    std::string file;
    std::vector<Location> locations;
    std::string detail;
    std::string kind;
};
