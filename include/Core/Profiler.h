#ifndef PROFILER_H
#define PROFILER_H

// Tracy client macros. When TRACY_ENABLE is not defined (Dist / NOVA_ENABLE_TRACY=OFF),
// every macro below compiles to a no-op.
#include <tracy/Tracy.hpp>

#define NV_PROFILE_FRAME_MARK              FrameMark
#define NV_PROFILE_FRAME_MARK_NAMED(name)  FrameMarkNamed(name)

#define NV_PROFILE_SCOPE                   ZoneScoped
#define NV_PROFILE_SCOPE_N(name)           ZoneScopedN(name)
#define NV_PROFILE_SCOPE_C(color)          ZoneScopedC(color)
#define NV_PROFILE_SCOPE_NC(name, color)   ZoneScopedNC(name, color)

#define NV_PROFILE_PLOT(name, value)       TracyPlot(name, value)
#define NV_PROFILE_MESSAGE(text)           TracyMessageL(text)
#define NV_PROFILE_SET_THREAD_NAME(name)   tracy::SetThreadName(name)

#endif // PROFILER_H
