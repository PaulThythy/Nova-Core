#ifndef ASSERT_H
#define ASSERT_H

#include <cstdlib>
#include <string>
#include <string_view>

#include "Core/Log.h"

namespace Nova::Core {

    inline void HandleAssertFailure(
        const char* expression,
        std::string_view message,
        const char* file,
        int line)
    {
        std::string formattedMessage = "Assertion failed: ";
        formattedMessage += expression;

        if (!message.empty()) {
            formattedMessage += " | ";
            formattedMessage += message;
        }

        Log::Get().Fatal(formattedMessage, file, line);
        std::abort();
    }

} // namespace Nova::Core

#ifdef NV_ENABLE_ASSERTS
    #define NV_ASSERT(condition) \
        do { \
            if (!(condition)) { \
                Nova::Core::HandleAssertFailure(#condition, {}, __FILE__, __LINE__); \
            } \
        } while (0)

    #define NV_ASSERT_MSG(condition, message) \
        do { \
            if (!(condition)) { \
                Nova::Core::HandleAssertFailure(#condition, (message), __FILE__, __LINE__); \
            } \
        } while (0)
#else
    #define NV_ASSERT(condition) do { (void)sizeof(condition); } while (0)
    #define NV_ASSERT_MSG(condition, message) do { (void)sizeof(condition); (void)sizeof(message); } while (0)
#endif

#endif // ASSERT_H
