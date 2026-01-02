#include "Core/Log.h"

namespace Nova::Core {

    Log::Log() {}

    Log& Log::Get() {
        static Log instance;
        return instance;
    }

    const char* Log::GetColorCode(Level level) {
        switch (level) {
            case Level::TRACE: return COLOR_TRACE;      // white
            case Level::DEBUG: return COLOR_DEBUG;      // cyan
            case Level::INFO:  return COLOR_INFO;       // green
            case Level::WARN:  return COLOR_WARN;       // yellow
            case Level::ERROR: return COLOR_ERROR;      // red
            case Level::FATAL: return COLOR_FATAL;      // bold red
            default:           return COLOR_RESET;      // reset
        }
    }

    const char* Log::GetLevelString(Level level) {
        switch (level) {
            case Level::TRACE: return "TRACE";
            case Level::DEBUG: return "DEBUG";
            case Level::INFO:  return "INFO";
            case Level::WARN:  return "WARN";
            case Level::ERROR: return "ERROR";
            case Level::FATAL: return "FATAL";
            default:           return "RESET";
        }
    }

    void Log::Print(Level level, const char* message) {
        // Errors and fatal go to std::cerr, others to std::cout
        std::ostream& out = (level >= Level::ERROR) ? std::cerr : std::cout;

        out
            << GetColorCode(level)
            << "[" << GetLevelString(level) << "] "
            << message
            << ColorReset()
            << std::endl;
    }

} // namespace Nova::Core