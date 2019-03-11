#include "MainWindow.hpp"
#include "wxw.hpp"
#include "wxwidgets_gui.hpp"

DECLARE_APP(wxw)

wxBEGIN_EVENT_TABLE(MainWindow, wxFrame)
    EVT_MENU(wxID_EXIT,  MainWindow::OnExit)
wxEND_EVENT_TABLE()

void MainWindow::OnExit(wxCommandEvent& event)
{
//    wxw *app = (wxw *)wxTheApp;
//    app->System->Shutdown();
    Close(true);
}

MainWindow::MainWindow(const wxString& title, const wxPoint& pos, const wxSize& size)
        : wxFrame(NULL, wxID_ANY, title, pos, size)
{
    this->SetBackgroundColour(wxColour(*wxWHITE));

    wxMenu *menuFile = new wxMenu;
    menuFile->Append(wxID_EXIT);

    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    SetMenuBar(menuBar);

    this->Show(true);
}
