#include <FL/Fl.H>
#include "editor.h"

int main(int argc, char **argv)
{
    Fl::lock();
    g_wnd = new editor_window();
    g_wnd->show(argc, argv);
    int rc = Fl::run();
    delete g_wnd;
    g_wnd = 0;
    return rc;
}
