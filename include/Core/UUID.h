#ifndef UUID_H
#define UUID_H

#include <cstdint>
#include <limits>
#include <random>

namespace Nova::Core {

    using UUID = std::uint32_t;

    /** Invalid / unset asset or entity ID. */
    constexpr UUID kInvalidUUID = 0;

    /**
     * Engine-reserved UUID range: [0, kEngineReservedUUIDEnd).
     * Used for built-in assets (e.g. mesh primitives) so GPU instancing can
     * batch by a stable AssetID. Runtime-generated IDs always fall outside.
     */
    constexpr UUID kEngineReservedUUIDEnd = 100;

    inline UUID GenerateUUID() {
        static std::random_device rd;
        static std::mt19937 engine(rd());
        static std::uniform_int_distribution<std::uint32_t> dis(
            kEngineReservedUUIDEnd,
            std::numeric_limits<std::uint32_t>::max()
        );

        return dis(engine);
    }

} // namespace Nova::Core

#endif // UUID_H