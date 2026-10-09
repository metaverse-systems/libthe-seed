#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

#include <atomic>
#include <cstdint>

#ifndef TEST_MODULE_VARIANT
#define TEST_MODULE_VARIANT 1
#endif

namespace
{
    /*! A component that a test loads from a shared module. Its configuration can set the value it
     *  carries and, for the tests of rejected components, the type it reports. The configuration key
     *  "counter" holds the address of a std::atomic<int> that the destructor increments. The exported
     *  configuration reports which build of the module the component came from. */
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
            this->Counter = reinterpret_cast<std::atomic<int> *>(
                static_cast<std::uintptr_t>(config.value("counter", static_cast<std::uint64_t>(0))));
        }

        ~ModuleComponent() override
        {
            if(this->Counter != nullptr)
            {
                ++*this->Counter;
            }
        }

        nlohmann::json Export() const override
        {
            nlohmann::json config;
            config["value"] = this->Value;
            config["variant"] = TEST_MODULE_VARIANT;
            return config;
        }

        int Value = 0;
        std::atomic<int> *Counter = nullptr;
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
