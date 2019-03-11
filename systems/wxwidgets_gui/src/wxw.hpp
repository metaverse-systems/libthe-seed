#pragma once

#include <libecs-cpp/ecs.hpp>
#include <wx/wx.h>
#include "MainWindow.hpp"

class wxwidgets_gui;

class wxw : public wxApp
{
  public:
    virtual bool OnInit();
    MainWindow *window = nullptr;
    wxwidgets_gui *System = nullptr;
};

DECLARE_APP(wxw)
