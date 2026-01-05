#ifndef LOG_H
#define LOG_H

#include <chrono>
#include <iostream>

namespace Nova::Core {

    struct Log {
        
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

        void Print(Level level, const char* message);

        void Trace(const char* msg) { Print(Level::TRACE, msg); }
        void Debug(const char* msg) { Print(Level::DEBUG, msg); }
        void Info (const char* msg) { Print(Level::INFO,  msg); }
        void Warn (const char* msg) { Print(Level::WARN,  msg); }
        void Error(const char* msg) { Print(Level::ERROR, msg); }
        void Fatal(const char* msg) { Print(Level::FATAL, msg); }

        static const char* GetColorCode(Level level);
        static const char* GetLevelString(Level level);
        static const char* ColorReset() { return COLOR_RESET; }

        Log();
        ~Log() = default;
        Log(const Log&) = delete;
        Log& operator=(const Log&) = delete;
    };

#define NV_LOG_TRACE(message, ...) Log::Get().Trace(message, ##__VA_ARGS__)
#define NV_LOG_DEBUG(message, ...) Log::Get().Debug(message, ##__VA_ARGS__)
#define NV_LOG_INFO(message, ...)  Log::Get().Info(message, ##__VA_ARGS__)
#define NV_LOG_WARN(message, ...)  Log::Get().Warn(message, ##__VA_ARGS__)
#define NV_LOG_ERROR(message, ...) Log::Get().Error(message, ##__VA_ARGS__)
#define NV_LOG_FATAL(message, ...) Log::Get().Fatal(message, ##__VA_ARGS__)

#define NV_LOG_OBJECT_CREATED(type, name)    NV_LOG_DEBUG("Created " + std::string(type) + ": " + std::string(name), "Object")
#define NV_LOG_OBJECT_DESTROYED(type, name)  NV_LOG_DEBUG("Destroyed " + std::string(type) + ": " + std::string(name), "Object")

#define LOG_PERF_START(operation) auto _perf_start_##operation = std::chrono::high_resolution_clock::now()
#define LOG_PERF_END(operation) do { \
    auto _perf_end = std::chrono::high_resolution_clock::now(); \
    auto _perf_duration = std::chrono::duration<float, std::milli>(_perf_end - _perf_start_##operation); \
    NV_LOG_DEBUG(#operation " took " + std::to_string(_perf_duration.count()) + "ms", "Performance"); \
} while(0)

} // namespace Nova::Core

#endif // LOG_H