#include <wxw.hpp>

IMPLEMENT_APP_NO_MAIN(wxw)

bool wxw::OnInit()
{
    uint16_t screen_width = 1285, screen_height = 810;
    this->window = new MainWindow("Editor", wxPoint(20, 20), wxSize(screen_width, screen_height));
    std::cout << "wxw::OnInit()" << std::endl;
    return true;
}
