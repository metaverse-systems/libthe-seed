#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

namespace
{
    /*! A component that a test loads from a shared module. Its configuration can set the value it
     *  carries and, for the tests of rejected components, the type it reports. */
    class ModuleComponent : public ecs::Component
    {
      public:
        ModuleComponent()
        {
            this->Type = "TestModule";
        }

        explicit ModuleComponent(const nlohmann::json &config)
        {
            this->Type = config.value("type", std::string("TestModule"));
            this->Value = config.value("value", 0);
        }

        nlohmann::json Export() const override
        {
            nlohmann::json config;
            config["value"] = this->Value;
            return config;
        }

        int Value = 0;
    };
}

extern "C"
{
    /*! Returns a component built from the configuration, or nothing when the configuration asks for a
     *  factory that fails to produce one. */
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr)
        {
            return new ModuleComponent();
        }

        auto *config = static_cast<nlohmann::json *>(p);
        if(config->value("null", false))
        {
            return nullptr;
        }
        return new ModuleComponent(*config);
    }
}
