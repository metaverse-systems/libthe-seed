#pragma once

#include <wxw.hpp>
#include <libecs-cpp/ecs.hpp>
#include <jsoncpp/json/json.h>
#include <thread>

class wxwidgets_gui : public ecs::System
{
  public:
    wxwidgets_gui(); 
    wxwidgets_gui(Json::Value);
    void Update(uint32_t dt);
  private:
    std::thread wxThread;
    void ThreadFunc();
    wxApp *App = nullptr;
    char **argv = nullptr;
    int argc = 0;
};
