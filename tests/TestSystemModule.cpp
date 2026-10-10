#include <libecs-cpp/ecs.hpp>

#include <atomic>

namespace
{
    /*! A system that a test loads from a shared module. Its destructor increments the counter whose
     *  address the creator was given. */
    class ModuleSystem : public ecs::System
    {
      public:
        explicit ModuleSystem(std::atomic<int> *counter)
            : ecs::System("TestSystem"), Counter(counter)
        {
        }

        ~ModuleSystem() override
        {
            if(this->Counter != nullptr)
            {
                ++*this->Counter;
            }
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

      private:
        std::atomic<int> *Counter;
    };
}

extern "C"
{
    /*! Returns a system. The argument is the address of a std::atomic<int> that the system's destructor
     *  increments, or null for no counting. */
    ecs::System *create_system(void *data)
    {
        return new ModuleSystem(static_cast<std::atomic<int> *>(data));
    }
}
