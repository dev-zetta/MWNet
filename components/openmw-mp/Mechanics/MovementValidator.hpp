#ifndef OPENMW_MP_MECHANICS_MOVEMENT_VALIDATOR_HPP
#define OPENMW_MP_MECHANICS_MOVEMENT_VALIDATOR_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mwmp::mechanics
{
    struct Position3
    {
        double x = 0;
        double y = 0;
        double z = 0;

        bool operator==(const Position3&) const = default;
    };

    struct MovementSample
    {
        Position3 position;
        std::string cell;
        std::uint64_t sequence = 0;
    };

    enum class MovementDecision : std::uint8_t
    {
        AcceptedInitial,
        Accepted,
        AcceptedTransition,
        InvalidConnection,
        InvalidSequence,
        StaleSequence,
        InvalidCoordinate,
        InvalidSpeed,
        SpeedExceeded,
        InvalidTransition,
        TransitionNotAuthorized,
        CapacityReached,
    };

    struct MovementValidationResult
    {
        MovementDecision decision = MovementDecision::InvalidConnection;
        double distance = 0;
        double allowedDistance = 0;

        bool accepted() const noexcept
        {
            return decision == MovementDecision::AcceptedInitial
                || decision == MovementDecision::Accepted
                || decision == MovementDecision::AcceptedTransition;
        }
    };

    class MovementValidator
    {
    public:
        using Clock = std::chrono::steady_clock;

        static constexpr double DistanceMultiplier = 1.5;
        static constexpr std::chrono::milliseconds LatencyAllowance{ 250 };
        static constexpr std::chrono::seconds TransitionLifetime{ 5 };
        static constexpr double MaximumCoordinateMagnitude = 1'000'000'000.0;
        static constexpr std::size_t MaximumCellBytes = 512;

        explicit MovementValidator(std::size_t maximumConnections = 4096);

        MovementValidationResult validate(std::uint64_t connection,
            const MovementSample& sample, double theoreticalMaximumSpeed,
            Clock::time_point now);
        MovementValidationResult previewCellTransition(std::uint64_t connection,
            std::string_view destinationCell, Position3 previousPosition,
            double tolerance, Clock::time_point now = Clock::time_point{},
            double theoreticalMaximumSpeed = 0) const;
        MovementValidationResult acceptCellTransition(std::uint64_t connection,
            std::string destinationCell, Position3 previousPosition,
            double tolerance, Clock::time_point now,
            std::chrono::milliseconds lifetime = TransitionLifetime,
            double theoreticalMaximumSpeed = 0);

        MovementValidationResult commitCellTransition(std::uint64_t connection,
            const MovementSample& destination, Position3 previousPosition,
            double tolerance, double theoreticalMaximumSpeed, Clock::time_point now);

        MovementValidationResult previewAuthorizedTransition(std::uint64_t connection,
            const MovementSample& destination, Clock::time_point now) const;

        bool authorizeTransition(std::uint64_t connection, std::string cell,
            Position3 position, double tolerance, Clock::time_point now,
            std::chrono::milliseconds lifetime = TransitionLifetime);
        bool cancelTransition(std::uint64_t connection) noexcept;
        bool erase(std::uint64_t connection) noexcept;
        void clear() noexcept;

        std::optional<MovementSample> current(std::uint64_t connection) const;
        std::size_t size() const noexcept;

    private:
        struct State
        {
            MovementSample sample;
            Clock::time_point observedAt;
        };

        struct Transition
        {
            std::string cell;
            std::optional<Position3> position;
            double tolerance = 0;
            Clock::time_point expiresAt;
        };

        static bool validPosition(const Position3& position) noexcept;
        static bool validCell(std::string_view cell) noexcept;
        static double distance(const Position3& left, const Position3& right) noexcept;

        std::size_t mMaximumConnections;
        std::unordered_map<std::uint64_t, State> mStates;
        std::unordered_map<std::uint64_t, Transition> mTransitions;
    };

    const char* describe(MovementDecision decision) noexcept;
}

#endif
