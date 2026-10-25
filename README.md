# Nova-Core

Core module of Nova game engine.

## Tracy (frame profiler)

Nova embeds the **Tracy client** (`vendors/tracy`). Profiling is enabled for all configs **except Dist**. Disable entirely with `-DNOVA_ENABLE_TRACY=OFF`.

The **Tracy Profiler** GUI is a separate app (not built with Nova). Build and run it once, then connect to a running Nova process.

### 1. Dependencies (Linux)

```bash
sudo apt install libdbus-1-dev libglfw3-dev libfreetype-dev libgtk-3-dev pkg-config
```

### 2. Build the Tracy Profiler GUI

CMake presets in Tracy may require a newer CMake than 3.28. Configure manually. On many distros, Wayland headers are too old for Tracy — use the X11 backend with `-DLEGACY=ON` :

```bash
cd vendors/tracy/profiler
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEGACY=ON
cmake --build build-release -j$(nproc)
```

Binary: `vendors/tracy/profiler/build-release/tracy-profiler`

### 3. Profile Nova

1. Launch `tracy-profiler`
2. Launch Nova (Debug / Release / RelWithDebInfo — **not Dist**)
3. In Tracy, select the Nova client and **Connect**

Capture is **on-demand** (`TRACY_ON_DEMAND`) : Nova runs normally without the GUI ; profiling starts when you connect.

### 4. Instrument code

```cpp
#include "Core/Profiler.h"

void Foo() {
    NV_PROFILE_SCOPE_N("Foo");
    // ...
}
```

Useful macros : `NV_PROFILE_SCOPE`, `NV_PROFILE_SCOPE_N(name)`, `NV_PROFILE_FRAME_MARK`, `NV_PROFILE_PLOT`, `NV_PROFILE_SET_THREAD_NAME`.

The engine already marks frames in `Application::Run` and zones around `OnUpdate` / `OnRender` / Vulkan `BeginFrame`–`EndFrame`.