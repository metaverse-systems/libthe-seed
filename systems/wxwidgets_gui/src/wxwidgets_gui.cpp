#include <wxwidgets_gui.hpp>
#include <iostream>

wxwidgets_gui::wxwidgets_gui() 
{ 
    this->Handle = "wxwidgets_gui";
}

wxwidgets_gui::wxwidgets_gui(Json::Value config)
{
    this->Handle = "wxwidgets_gui";

    wxw *app = (wxw *)&this->App;
    app->System = this;

    wxApp::SetInstance(this->App);
    void *p = (void *)config["argv"].asUInt64();
    this->argv = (char **)p;
    this->argc = config["argc"].asInt();

    this->wxThread = std::thread(&wxwidgets_gui::ThreadFunc, this);
    this->wxThread.detach();
}

void wxwidgets_gui::ThreadFunc()
{
    wxEntryStart(this->argc, this->argv);
    wxTheApp->OnInit();
    wxTheApp->OnRun();

    // cleaning up...
    wxTheApp->OnExit();
    wxEntryCleanup();
    this->Container->ManagerGet()->Shutdown();
}

void wxwidgets_gui::Update(uint32_t dt)
{
    // It's been dt milliseconds since the last Update()
    // Do some work
}

void wxwidgets_gui::Shutdown()
{
    this->Container->ManagerGet()->Shutdown();
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new wxwidgets_gui();

        Json::Value *config = (Json::Value *)p;
        return new wxwidgets_gui(*config);
    }
}
