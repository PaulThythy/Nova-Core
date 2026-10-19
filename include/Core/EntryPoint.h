#ifndef ENTRY_POINT_H
#define ENTRY_POINT_H

#include "Core/Application.h"
#include "Core/Platform.h"

#if defined(NOVA_WINDOWS) || defined(NOVA_LINUX)

namespace Nova::Core::Detail {

    inline int Main(int argc, char** argv)
    {
        Application* app = CreateApplication({ argc, argv });
        app->Run();
        delete app;
        return 0;
    }

} // namespace Nova::Core::Detail

// Dist on Windows: GUI subsystem entry (no console window).
// Debug / Release / … and Linux: standard console-capable main.
#if defined(NOVA_WINDOWS) && defined(NOVA_DIST)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // MSVC / clang-cl CRT exposes the process command line as __argc / __argv.
    return Nova::Core::Detail::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
    return Nova::Core::Detail::Main(argc, argv);
}

#endif // NOVA_WINDOWS && NOVA_DIST

#endif // NOVA_WINDOWS || NOVA_LINUX

#endif // ENTRY_POINT_H