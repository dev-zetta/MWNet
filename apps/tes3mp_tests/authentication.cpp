#include <components/openmw-mp/Security/AccountAuthentication.hpp>
#include <components/openmw-mp/Security/AuthenticationRateLimiter.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

namespace
{
    using namespace mwmp::security;
    using namespace std::chrono_literals;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "authentication.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testArgon2idAndMigration()
    {
        std::string error;
        auto password = PasswordBuffer::copyFrom("correct horse battery staple", error);
        auto wrongPassword = PasswordBuffer::copyFrom("incorrect password", error);
        EXPECT(password.has_value());
        EXPECT(wrongPassword.has_value());
        if (!password || !wrongPassword)
            return;

        AccountCredentials created;
        EXPECT(createAccountCredentials(*password, created, error));
        EXPECT(created.schemaVersion == accountLoginSchemaVersion);
        EXPECT(created.passwordScheme == argon2idPasswordScheme);
        EXPECT(created.passwordSalt.empty());
        EXPECT(created.passwordHash.starts_with("$argon2id$"));
        EXPECT(PasswordHash::verifyArgon2id(*password, created.passwordHash));
        EXPECT(!PasswordHash::verifyArgon2id(*wrongPassword, created.passwordHash));
        EXPECT(!PasswordHash::needsRehash(created.passwordHash));

        AccountCredentials legacy;
        legacy.passwordSalt = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-";
        legacy.passwordHash = "0774be374bdab4fb47ad1b85baddc3b9cbaee98d9a7abb34de6a8467afbfd231";
        const AccountCredentials original = legacy;
        EXPECT(PasswordHash::verifyLegacySha256(
            *password, legacy.passwordSalt, legacy.passwordHash));
        EXPECT(!PasswordHash::verifyLegacySha256(
            *wrongPassword, legacy.passwordSalt, legacy.passwordHash));

        AccountCredentials attemptedUpgrade;
        const auto deferred = authenticateAndUpgrade(*password, legacy,
            [&](const AccountCredentials& value, std::string& persistenceError) {
                attemptedUpgrade = value;
                persistenceError = "injected write failure";
                return false;
            }, error);
        EXPECT(deferred == AuthenticationResult::AuthenticatedMigrationDeferred);
        EXPECT(isAuthenticated(deferred));
        EXPECT(error == "injected write failure");
        EXPECT(legacy == original);
        EXPECT(attemptedUpgrade.passwordScheme == argon2idPasswordScheme);
        EXPECT(attemptedUpgrade.passwordSalt.empty());

        bool persisted = false;
        const auto migrated = authenticateAndUpgrade(*password, legacy,
            [&](const AccountCredentials& value, std::string&) {
                persisted = value.passwordSalt.empty();
                return true;
            }, error);
        EXPECT(migrated == AuthenticationResult::AuthenticatedMigrated);
        EXPECT(isAuthenticated(migrated));
        EXPECT(persisted);
        EXPECT(legacy.schemaVersion == accountLoginSchemaVersion);
        EXPECT(legacy.passwordScheme == argon2idPasswordScheme);
        EXPECT(legacy.passwordSalt.empty());
        EXPECT(PasswordHash::verifyArgon2id(*password, legacy.passwordHash));

        const auto authenticated = authenticateAndUpgrade(*password, legacy, {}, error);
        EXPECT(authenticated == AuthenticationResult::Authenticated);
        EXPECT(authenticateAndUpgrade(*wrongPassword, legacy, {}, error)
            == AuthenticationResult::InvalidPassword);

        std::string oversized(129, 'x');
        EXPECT(!PasswordBuffer::copyFrom(oversized, error).has_value());
        EXPECT(!PasswordBuffer::copyFrom({}, error).has_value());
    }

    void testRateLimits()
    {
        AuthenticationLimits limits;
        limits.preKdfAttemptsPerMinute = 60.0;
        limits.preKdfBurst = 10.0;
        AuthenticationRateLimiter limiter(limits);
        const auto start = AuthenticationRateLimiter::Clock::time_point{};

        for (std::size_t attempt = 0; attempt < 5; ++attempt)
        {
            EXPECT(limiter.begin("Player", "127.0.0.1", start + attempt * 1s)
                == AuthenticationAttemptDecision::Allowed);
            limiter.recordFailure("Player", "127.0.0.1", start + attempt * 1s);
        }
        EXPECT(limiter.begin("player", "127.0.0.1", start + 5s)
            == AuthenticationAttemptDecision::AccountAndAddressLocked);
        EXPECT(limiter.begin("Player", "127.0.0.2", start + 5s)
            == AuthenticationAttemptDecision::Allowed);
        EXPECT(limiter.begin("Other", "127.0.0.1", start + 5s)
            == AuthenticationAttemptDecision::Allowed);
        EXPECT(limiter.begin("Player", "127.0.0.1", start + 15min + 5s)
            == AuthenticationAttemptDecision::Allowed);
        limiter.recordSuccess("Player", "127.0.0.1");

        AuthenticationLimits preKdfLimits;
        preKdfLimits.preKdfAttemptsPerMinute = 1.0;
        preKdfLimits.preKdfBurst = 2.0;
        AuthenticationRateLimiter preKdf(preKdfLimits);
        EXPECT(preKdf.begin("one", "10.0.0.1", start)
            == AuthenticationAttemptDecision::Allowed);
        EXPECT(preKdf.begin("two", "10.0.0.1", start)
            == AuthenticationAttemptDecision::Allowed);
        EXPECT(preKdf.begin("three", "10.0.0.1", start)
            == AuthenticationAttemptDecision::PreKdfRateLimited);
        EXPECT(preKdf.begin("three", "10.0.0.1", start + 1min)
            == AuthenticationAttemptDecision::Allowed);
        EXPECT(preKdf.begin("", "10.0.0.1", start)
            == AuthenticationAttemptDecision::InvalidIdentity);

        AuthenticationLimits capacityLimits;
        capacityLimits.maximumTrackedEntries = 1;
        AuthenticationRateLimiter capacity(capacityLimits);
        EXPECT(capacity.begin("one", "10.0.0.1", start)
            == AuthenticationAttemptDecision::Allowed);
        EXPECT(capacity.begin("two", "10.0.0.1", start)
            == AuthenticationAttemptDecision::CapacityReached);
        EXPECT(capacity.trackedAccountAddresses() == 1);
        EXPECT(capacity.trackedAddresses() == 1);
    }
}

int runAuthenticationTests()
{
    testArgon2idAndMigration();
    testRateLimits();
    return sFailures;
}
