#ifndef LOG_H
#define LOG_H

#include <chrono>
#include <iostream>
#include <string>

#include "Api.h"

namespace Nova::Core {

    struct NV_API Log {
        
        enum class Level {
            TRACE = 0,
            DEBUG = 1,
            INFO  = 2,
            WARN  = 3,
            ERROR = 4,
            FATAL = 5
        };

        inline static constexpr const char* COLOR_TRACE = "\x1b[37m";
        inline static constexpr const char* COLOR_DEBUG = "\x1b[36m";
        inline static constexpr const char* COLOR_INFO = "\x1b[32m";
        inline static constexpr const char* COLOR_WARN = "\x1b[33m";
        inline static constexpr const char* COLOR_ERROR = "\x1b[31m";
        inline static constexpr const char* COLOR_FATAL = "\x1b[1;31m";
        inline static constexpr const char* COLOR_RESET = "\x1b[0m";

        static Log& Get();

        void Print(Level level, const char* message,
                   const char* file = nullptr, int line = 0);

        void Trace(const char* msg, const char* file = nullptr, int line = 0)
            { Print(Level::TRACE, msg, file, line); }
        void Trace(const std::string& msg, const char* file = nullptr, int line = 0)
            { Print(Level::TRACE, msg.c_str(), file, line); }
        void Debug(const char* msg, const char* file = nullptr, int line = 0)
            { Print(Level::DEBUG, msg, file, line); }
        void Debug(const std::string& msg, const char* file = nullptr, int line = 0)
            { Print(Level::DEBUG, msg.c_str(), file, line); }
        void Info (const char* msg, const char* file = nullptr, int line = 0)
            { Print(Level::INFO,  msg, file, line); }
        void Info (const std::string& msg, const char* file = nullptr, int line = 0)
            { Print(Level::INFO,  msg.c_str(), file, line); }
        void Warn (const char* msg, const char* file = nullptr, int line = 0)
            { Print(Level::WARN,  msg, file, line); }
        void Warn (const std::string& msg, const char* file = nullptr, int line = 0)
            { Print(Level::WARN,  msg.c_str(), file, line); }
        void Error(const char* msg, const char* file = nullptr, int line = 0)
            { Print(Level::ERROR, msg, file, line); }
        void Error(const std::string& msg, const char* file = nullptr, int line = 0)
            { Print(Level::ERROR, msg.c_str(), file, line); }
        void Fatal(const char* msg, const char* file = nullptr, int line = 0)
            { Print(Level::FATAL, msg, file, line); }
        void Fatal(const std::string& msg, const char* file = nullptr, int line = 0)
            { Print(Level::FATAL, msg.c_str(), file, line); }

        static const char* GetColorCode(Level level);
        static const char* GetLevelString(Level level);
        static const char* ColorReset() { return COLOR_RESET; }

        Log();
        ~Log() = default;
        Log(const Log&) = delete;
        Log& operator=(const Log&) = delete;
    };

#define NV_LOG_TRACE(message, ...) Nova::Core::Log::Get().Trace(message, ##__VA_ARGS__)
#define NV_LOG_DEBUG(message, ...) Nova::Core::Log::Get().Debug(message, ##__VA_ARGS__)
#define NV_LOG_INFO(message, ...)  Nova::Core::Log::Get().Info(message, ##__VA_ARGS__)
#define NV_LOG_WARN(message, ...)  Nova::Core::Log::Get().Warn(message, ##__VA_ARGS__)
#define NV_LOG_ERROR(message) Nova::Core::Log::Get().Error(message, __FILE__, __LINE__)
#define NV_LOG_FATAL(message) Nova::Core::Log::Get().Fatal(message, __FILE__, __LINE__)

#define NV_LOG_OBJECT_CREATED(type, name)    NV_LOG_DEBUG(std::string("Created ") + (type) + ": " + (name))
#define NV_LOG_OBJECT_DESTROYED(type, name)  NV_LOG_DEBUG(std::string("Destroyed ") + (type) + ": " + (name))

#define LOG_PERF_START(operation) auto _perf_start_##operation = std::chrono::high_resolution_clock::now()
#define LOG_PERF_END(operation) do { \
    auto _perf_end = std::chrono::high_resolution_clock::now(); \
    auto _perf_duration = std::chrono::duration<float, std::milli>(_perf_end - _perf_start_##operation); \
    NV_LOG_DEBUG(#operation " took " + std::to_string(_perf_duration.count()) + "ms", "Performance"); \
} while(0)

} // namespace Nova::Core

#endif // LOG_H