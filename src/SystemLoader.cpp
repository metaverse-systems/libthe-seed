#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LibraryLoader.hpp>
#include "NameParser.hpp"
#include <shared_mutex>
#include <stdexcept>

SystemLoader::SystemLoader() = default;

SystemLoader::~SystemLoader() = default;

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name)
{
    return Create(name, nullptr);
}

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name, void *data)
{
    auto creator = Get(name);
    return std::unique_ptr<ecs::System>(creator(data));
}

SystemLoader::SystemCreator SystemLoader::Get(const std::string &name)
{
    {
        std::shared_lock lock(mutex_);
        auto it = creators_.find(name);
        if (it != creators_.end())
            return it->second;
    }

    std::unique_lock lock(mutex_);
    auto it = creators_.find(name);
    if (it != creators_.end())
        return it->second;

    auto parsed = NameParser(name);
    auto lib = std::make_unique<LibraryLoader>(parsed.library);

    lib->PathAdd("./");
    lib->PathAdd("../../" + parsed.library + "/src/.libs/");
    if (!parsed.org.empty())
    {
        auto path = "../node_modules/" + parsed.org + "/" + parsed.library + "/src/.libs";
        lib->PathAdd(path);
    }

    for (const auto &path : paths_)
        lib->PathAdd(path);

    void *ptr = lib->FunctionGet("create_system");
    auto creator = reinterpret_cast<SystemCreator>(ptr);

    cache_[name] = std::move(lib);
    creators_[name] = creator;

    return creator;
}

void SystemLoader::PathAdd(const std::string &path)
{
    std::unique_lock lock(mutex_);
    paths_.push_back(path);
}

std::vector<std::string> SystemLoader::PathsGet() const
{
    std::shared_lock lock(mutex_);
    return paths_;
}
