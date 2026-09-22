#include <components/openmw-mp/Session/AuthorityLease.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace
{
    using namespace mwmp::session;
    using namespace std::chrono_literals;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "authority.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testGrantRenewAndValidate()
    {
        AuthorityLeaseManager leases;
        const auto now = AuthorityLeaseManager::Clock::time_point{};
        const LeaseGrantResult granted = leases.grant("Balmora", 7, now);
        EXPECT(granted.decision == LeaseGrantDecision::Granted);
        EXPECT(granted.lease.has_value());
        EXPECT(granted.lease->owner == 7);
        EXPECT(granted.lease->leaseId != 0);
        EXPECT(granted.lease->expiresAt == now + 5s);
        EXPECT(leases.validate("Balmora", 7, granted.lease->leaseId, now + 4s)
            == LeaseValidation::Valid);
        EXPECT(leases.validate("Balmora", 8, granted.lease->leaseId, now + 4s)
            == LeaseValidation::WrongOwner);
        EXPECT(leases.validate("Balmora", 7, granted.lease->leaseId + 1, now + 4s)
            == LeaseValidation::WrongLease);

        EXPECT(leases.validateAndRenew("Balmora", 7, granted.lease->leaseId, now + 1s)
            == LeaseValidation::Valid);
        EXPECT(leases.find("Balmora")->expiresAt == now + 5s);
        EXPECT(leases.validateAndRenew("Balmora", 7, granted.lease->leaseId, now + 2s)
            == LeaseValidation::Valid);
        EXPECT(leases.find("Balmora")->expiresAt == now + 7s);
        EXPECT(leases.validate("Balmora", 7, granted.lease->leaseId, now + 6s)
            == LeaseValidation::Valid);
        EXPECT(leases.validate("Balmora", 7, granted.lease->leaseId, now + 7s)
            == LeaseValidation::Expired);
    }

    void testExistingLeaseCannotBeStolen()
    {
        AuthorityLeaseManager leases;
        const auto now = AuthorityLeaseManager::Clock::time_point{};
        const LeaseGrantResult original = leases.grant("Seyda Neen", 1, now);
        const LeaseGrantResult duplicate = leases.grant("Seyda Neen", 1, now + 2s);
        const LeaseGrantResult occupied = leases.grant("Seyda Neen", 2, now + 2s);
        EXPECT(duplicate.decision == LeaseGrantDecision::Existing);
        EXPECT(duplicate.lease->leaseId == original.lease->leaseId);
        EXPECT(duplicate.lease->expiresAt == original.lease->expiresAt);
        EXPECT(occupied.decision == LeaseGrantDecision::Occupied);
        EXPECT(occupied.lease->owner == 1);

        const LeaseGrantResult replacement = leases.grant("Seyda Neen", 2, now + 5s);
        EXPECT(replacement.decision == LeaseGrantDecision::Granted);
        EXPECT(replacement.lease->owner == 2);
        EXPECT(replacement.lease->leaseId != original.lease->leaseId);
    }

    void testReleaseAndExpiry()
    {
        AuthorityLeaseManager leases;
        const auto now = AuthorityLeaseManager::Clock::time_point{};
        const auto first = leases.grant("Ald-ruhn", 3, now).lease.value();
        const auto second = leases.grant("Vivec", 3, now + 1s).lease.value();
        const auto third = leases.grant("Gnisis", 4, now + 2s).lease.value();

        EXPECT(!leases.release("Ald-ruhn", 4, first.leaseId));
        EXPECT(!leases.release("Ald-ruhn", 3, first.leaseId + 1));
        EXPECT(leases.release("Ald-ruhn", 3, first.leaseId));
        EXPECT(leases.releaseOwner(3) == 1);
        EXPECT(!leases.find(second.cell).has_value());
        EXPECT(leases.find(third.cell).has_value());

        const auto expired = leases.expire(now + 7s);
        EXPECT(expired.size() == 1);
        EXPECT(expired.front().cell == "Gnisis");
        EXPECT(leases.size() == 0);
    }

    void testQuietCellRenewalAndLoadingPause()
    {
        AuthorityLeaseManager leases;
        const auto start = AuthorityLeaseManager::Clock::time_point{};
        const auto original = leases.grant("Seyda Neen", 7, start).lease.value();
        // A quiet cell produces no simulation, but explicit renewals keep it alive.
        for (auto elapsed = 2s; elapsed <= 60s; elapsed += 2s)
            EXPECT(leases.renew(original.cell, 7, original.leaseId, start + elapsed)
                == LeaseValidation::Valid);
        EXPECT(leases.validate(original.cell, 7, original.leaseId, start + 64s)
            == LeaseValidation::Valid);
        // A pause longer than the lease cannot revive the expired generation.
        EXPECT(leases.renew(original.cell, 7, original.leaseId, start + 66s)
            == LeaseValidation::Expired);
        const auto replacement = leases.grant(original.cell, 7, start + 66s).lease.value();
        EXPECT(replacement.leaseId != original.leaseId);
        EXPECT(leases.validate(original.cell, 7, original.leaseId, start + 66s)
            == LeaseValidation::WrongLease);
        EXPECT(leases.renew(original.cell, 8, replacement.leaseId, start + 67s)
            == LeaseValidation::WrongOwner);
        EXPECT(leases.find(original.cell)->expiresAt == start + 71s);
        EXPECT(leases.releaseOwner(7) == 1);
        EXPECT(leases.renew(original.cell, 7, replacement.leaseId, start + 68s)
            == LeaseValidation::NotFound);
    }

    void testLimits()
    {
        AuthorityLeaseManager leases(1);
        const auto now = AuthorityLeaseManager::Clock::time_point{};
        EXPECT(leases.grant("", 1, now).decision == LeaseGrantDecision::InvalidCell);
        EXPECT(leases.grant(std::string(AuthorityLeaseManager::MaximumCellNameBytes + 1, 'x'), 1,
                   now).decision
            == LeaseGrantDecision::InvalidCell);
        EXPECT(leases.grant("Caldera", 0, now).decision == LeaseGrantDecision::InvalidOwner);
        EXPECT(leases.grant("Caldera", 1, now).decision == LeaseGrantDecision::Granted);
        EXPECT(leases.grant("Pelagiad", 2, now).decision == LeaseGrantDecision::CapacityReached);
        EXPECT(leases.renew("missing", 1, 1, now) == LeaseValidation::NotFound);
        EXPECT(std::string(describe(LeaseValidation::WrongLease))
            == "the lease identifier does not match");
    }
}

int runAuthorityTests()
{
    testGrantRenewAndValidate();
    testExistingLeaseCannotBeStolen();
    testReleaseAndExpiry();
    testQuietCellRenewalAndLoadingPause();
    testLimits();
    return sFailures;
}
