#ifndef UUID_H
#define UUID_H

#include <cstdint>
#include <random>
#include <limits>

namespace Nova::Core {

    using UUID = std::uint32_t;

    inline UUID GenerateUUID() {
        static std::random_device rd;
        static std::mt19937 engine(rd());
        static std::uniform_int_distribution<std::uint32_t> dis(
            std::numeric_limits<std::uint32_t>::min(),
            std::numeric_limits<std::uint32_t>::max()
        );

        return dis(engine);
    }

} // namespace Nova::Core

#endif // UUID_H