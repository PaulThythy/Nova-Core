#ifndef API_H
#define API_H

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(NV_BUILD_SHARED)
        #if defined(NV_CORE_EXPORTS)
            #define NV_API __declspec(dllexport)
        #else
            #define NV_API __declspec(dllimport)
        #endif
    #else
        #define NV_API
    #endif
#else
    #if defined(NV_BUILD_SHARED)
        #define NV_API __attribute__((visibility("default")))
    #else
        #define NV_API
    #endif
#endif

#endif // API_H
