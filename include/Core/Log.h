#ifndef LOG_H
#define LOG_H

#include <string>
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

        static constexpr std::string COLOR_TRACE = "\033[37m";
        static constexpr std::string COLOR_DEBUG = "\033[36m";
        static constexpr std::string COLOR_INFO = "\033[32m";
        static constexpr std::string COLOR_WARN = "\033[33m";
        static constexpr std::string COLOR_ERROR = "\033[31m";
        static constexpr std::string COLOR_FATAL = "\033[1;31m";
        static constexpr std::string COLOR_RESET = "\033[0m";

        static Log& Get();

        void Print(Level level, const std::string& message);

        void Trace(const std::string& msg) { Print(Level::TRACE, msg); }
        void Debug(const std::string& msg) { Print(Level::DEBUG, msg); }
        void Info (const std::string& msg) { Print(Level::INFO,  msg); }
        void Warn (const std::string& msg) { Print(Level::WARN,  msg); }
        void Error(const std::string& msg) { Print(Level::ERROR, msg); }
        void Fatal(const std::string& msg) { Print(Level::FATAL, msg); }

        static const std::string GetColorCode(Level level);
        static const std::string GetLevelString(Level level);
        static const std::string ColorReset() { return COLOR_RESET; }

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

#define NV_VK_LOG_INFO(message)    NV_LOG_INFO(message, "Vulkan")
#define NV_VK_LOG_ERROR(message)   NV_LOG_ERROR(message, "Vulkan")
#define NV_VK_LOG_WARN(message)    NV_LOG_WARN(message, "Vulkan")
#define NV_VK_LOG_DEBUG(message)   NV_LOG_DEBUG(message, "Vulkan")

#define NV_GL_LOG_INFO(message)    NV_LOG_INFO(message, "OpenGL")
#define NV_GL_LOG_ERROR(message)   NV_LOG_ERROR(message, "OpenGL")
#define NV_GL_LOG_WARN(message)    NV_LOG_WARN(message, "OpenGL")
#define NV_GL_LOG_DEBUG(message)   NV_LOG_DEBUG(message, "OpenGL")

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