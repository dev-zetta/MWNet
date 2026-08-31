#ifndef OPENMW_MP_MECHANICS_OBJECT_STATE_LEDGER_HPP
#define OPENMW_MP_MECHANICS_OBJECT_STATE_LEDGER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    struct ObjectIdentity
    {
        std::string cell;
        std::uint32_t refNum = 0;
        std::uint32_t mpNum = 0;

        bool operator==(const ObjectIdentity&) const = default;
    };

    struct ObjectIdentityHash
    {
        std::size_t operator()(const ObjectIdentity& identity) const noexcept;
    };

    struct ObjectState
    {
        ObjectIdentity identity;
        std::string refId;
        std::string soul;
        std::uint64_t creator = 0;
        std::int64_t count = 1;
        std::int32_t charge = -1;
        double enchantmentCharge = -1.0;
        std::int64_t goldValue = 0;
        std::array<double, 3> position{};
        std::array<double, 3> rotation{};
        double scale = 1.0;
        std::int32_t lockLevel = 0;
        bool enabled = true;
        bool hasContainer = false;
        bool deleted = false;

        bool operator==(const ObjectState&) const = default;
    };

    enum class ObjectMutationKind : std::uint8_t
    {
        Seed,
        Place,
        SetEnabled,
        Move,
        Rotate,
        Scale,
        SetLock,
        Delete,
    };

    struct ObjectMutation
    {
        ObjectMutationKind kind = ObjectMutationKind::Seed;
        ObjectState object;
    };

    enum class ObjectDecision : std::uint8_t
    {
        Applied,
        InvalidIdentity,
        InvalidMutation,
        InvalidObject,
        MissingObject,
        DeletedObject,
        DuplicateObject,
        ObjectLimitReached,
        PlayerLimitReached,
        MutationLimitReached,
    };

    struct ObjectResult
    {
        ObjectDecision decision = ObjectDecision::InvalidMutation;
        std::size_t objectCount = 0;

        bool applied() const noexcept
        {
            return decision == ObjectDecision::Applied;
        }
    };

    class ObjectStateLedger
    {
    public:
        static constexpr std::size_t MaximumCellBytes = 4096;
        static constexpr std::size_t MaximumStringBytes = 4096;
        static constexpr std::size_t MaximumMutations = 3000;
        static constexpr std::size_t DefaultMaximumObjects = 100'000;
        static constexpr std::size_t DefaultMaximumDynamicObjectsPerPlayer = 4096;
        static constexpr std::int64_t MaximumCount = 1'000'000'000;
        static constexpr double MaximumCharge = 1'000'000'000.0;
        static constexpr double MaximumCoordinate = 1'000'000'000.0;
        static constexpr double MaximumScale = 1000.0;
        static constexpr std::int32_t MaximumLockLevel = 1'000'000;

        explicit ObjectStateLedger(
            std::size_t maximumObjects = DefaultMaximumObjects,
            std::size_t maximumDynamicObjectsPerPlayer
                = DefaultMaximumDynamicObjectsPerPlayer);

        ObjectResult previewBatch(const std::vector<ObjectMutation>& mutations) const;
        ObjectResult applyBatch(const std::vector<ObjectMutation>& mutations);
        std::optional<ObjectState> find(const ObjectIdentity& identity) const;
        std::size_t dynamicObjectCount(std::uint64_t creator) const noexcept;
        std::size_t size() const noexcept;
        void clear() noexcept;

    private:
        struct PreparedBatch
        {
            ObjectResult result;
            std::unordered_map<ObjectIdentity, ObjectState, ObjectIdentityHash> candidates;
            std::unordered_map<std::uint64_t, std::size_t> creatorAdditions;
        };

        static bool validIdentity(const ObjectIdentity& identity) noexcept;
        static bool validObject(const ObjectState& object) noexcept;
        static bool validVector(const std::array<double, 3>& values) noexcept;
        static bool validText(const std::string& value, std::size_t limit) noexcept;
        PreparedBatch prepare(const std::vector<ObjectMutation>& mutations) const;

        std::size_t mMaximumObjects;
        std::size_t mMaximumDynamicObjectsPerPlayer;
        std::unordered_map<ObjectIdentity, ObjectState, ObjectIdentityHash> mObjects;
        std::unordered_map<std::uint64_t, std::size_t> mDynamicObjectsByCreator;
    };

    const char* describe(ObjectDecision decision) noexcept;
}

#endif
