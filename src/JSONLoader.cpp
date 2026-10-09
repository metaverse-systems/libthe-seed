#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>

#include <cerrno>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    const char *const textName = "scene text";

    std::string Quoted(const std::string &text)
    {
        return "\"" + text + "\"";
    }

    [[noreturn]] void Unopenable(const std::string &file, const std::string &reason)
    {
        throw LoadError(LoadError::Reason::SceneUnopenable, file, file, {}, reason);
    }

    [[noreturn]] void NotUnderstood(const std::string &file, const std::string &reason)
    {
        throw LoadError(LoadError::Reason::SceneNotUnderstood, file, file, {}, reason);
    }

    std::string EntityLabel(std::size_t index)
    {
        return "entity " + std::to_string(index);
    }

    /*! One component made from the scene, waiting to be attached. */
    struct Staged
    {
        std::string handle;
        std::unique_ptr<ecs::Component> component;
    };
}

JSONLoader::JSONLoader(ecs::Container *container, ComponentLoader &loader)
    : container(container), loader(loader)
{
}

void JSONLoader::StringParse(const std::string &data)
{
    this->Load(data, textName);
}

void JSONLoader::FileParse(const std::string &filename)
{
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status(filename, ec);
    if(std::filesystem::is_directory(status))
    {
        Unopenable(filename, "it is a directory");
    }
    if(status.type() == std::filesystem::file_type::not_found || (ec && !std::filesystem::exists(status)))
    {
        Unopenable(filename, std::generic_category().message(ec ? ec.value() : ENOENT));
    }
    if(!ec && !std::filesystem::is_regular_file(status))
    {
        Unopenable(filename, "it is not a regular file");
    }

    std::FILE *file = std::fopen(filename.c_str(), "rb");
    if(file == nullptr)
    {
        Unopenable(filename, std::generic_category().message(errno));
    }

    std::string data;
    char buffer[8192];
    std::size_t count = 0;
    while((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        data.append(buffer, count);
    }
    const bool failed = std::ferror(file) != 0;
    const int reason = errno;
    std::fclose(file);
    if(failed)
    {
        Unopenable(filename, std::generic_category().message(reason));
    }

    this->Load(data, filename);
}

void JSONLoader::Load(const std::string &data, const std::string &source)
{
    // Phase 1: parse.
    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(data);
    }
    catch(const nlohmann::json::exception &e)
    {
        NotUnderstood(source, e.what());
    }

    // Phase 2: check the structure of the whole document before anything is made.
    if(!document.is_object() || !document.contains("entities"))
    {
        NotUnderstood(source, "\"entities\" is missing");
    }
    const nlohmann::json &entities = document["entities"];
    if(!entities.is_array())
    {
        NotUnderstood(source, "\"entities\" must be an array");
    }
    for(std::size_t index = 0; index < entities.size(); index++)
    {
        const nlohmann::json &entity = entities[index];
        if(!entity.is_object())
        {
            NotUnderstood(source, EntityLabel(index) + " must be an object");
        }
        if(!entity.contains("Handle") || !entity["Handle"].is_string() || entity["Handle"].get<std::string>().empty())
        {
            NotUnderstood(source, EntityLabel(index) + ": \"Handle\" must be a non-empty string");
        }
        const std::string label = EntityLabel(index) + " (" + Quoted(entity["Handle"].get<std::string>()) + ")";
        if(!entity.contains("Components"))
        {
            NotUnderstood(source, label + ": \"Components\" is missing");
        }
        if(!entity["Components"].is_object())
        {
            NotUnderstood(source, label + ": \"Components\" must be an object");
        }
        for(const auto &item : entity["Components"].items())
        {
            if(item.key().empty())
            {
                NotUnderstood(source, label + ": a component name is empty");
            }
        }
    }

    // Phase 3: make every component. Nothing touches the container yet.
    std::vector<Staged> staged;
    for(const nlohmann::json &entity : entities)
    {
        const std::string handle = entity["Handle"].get<std::string>();
        for(auto &[type, config] : entity["Components"].items())
        {
            std::string problem;
            std::unique_ptr<ecs::Component> component;
            try
            {
                component = this->loader.Create(type, const_cast<nlohmann::json *>(&config));
                if(component != nullptr && component->Type.empty())
                {
                    problem = "component type is empty";
                }
            }
            catch(const std::exception &e)
            {
                problem = e.what();
            }
            if(problem.empty() && component == nullptr)
            {
                problem = "the plugin produced no object";
            }
            if(!problem.empty())
            {
                throw LoadError(LoadError::Reason::SceneComponentFailed, source, source, {},
                                "entity " + Quoted(handle) + ", component " + Quoted(type) + ": " + problem);
            }
            staged.push_back(Staged{handle, std::move(component)});
        }
    }

    // Phase 4: everything was made, so create the entities and attach in document order.
    for(const nlohmann::json &entity : entities)
    {
        this->container->Entity(entity["Handle"].get<std::string>());
    }
    for(Staged &item : staged)
    {
        this->container->Entity(item.handle)->Component(std::move(item.component));
    }
    this->scene = std::move(document);
}
