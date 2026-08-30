#include <components/openmw-mp/Security/AccountAuthentication.hpp>
#include <components/openmw-mp/Security/AccountStore.hpp>
#include <components/openmw-mp/Security/AuthenticationMessages.hpp>
#include <components/openmw-mp/Security/AuthenticationRateLimiter.hpp>
#include <components/openmw-mp/Security/ServerAuthenticationService.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    void testAccountStore()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto directory = std::filesystem::temp_directory_path()
            / ("tes3mp-account-store-" + unique);
        const auto credentials = directory / "accounts";
        const auto players = directory / "player";
        std::filesystem::create_directories(players);

        std::string error;
        auto password = PasswordBuffer::copyFrom("correct horse battery staple", error);
        auto wrongPassword = PasswordBuffer::copyFrom("incorrect password", error);
        EXPECT(password.has_value());
        EXPECT(wrongPassword.has_value());
        if (!password || !wrongPassword)
            return;

        AccountStore store(credentials, players);
        auto result = store.authenticate("New Player", *password, true);
        EXPECT(result.status == AccountStoreStatus::Registered);
        EXPECT(result.authenticated());
        EXPECT(result.isNewAccount);
        EXPECT(store.authenticate("new player", *wrongPassword, false).status
            == AccountStoreStatus::InvalidCredentials);
        EXPECT(store.authenticate("NEW PLAYER", *password, false).authenticated());
        EXPECT(store.authenticate("New Player", *password, true).status
            == AccountStoreStatus::AccountAlreadyExists);

        const auto legacyPath = players / "Manio.json";
        {
            std::ofstream legacy(legacyPath, std::ios::binary);
            legacy << "{\n"
                "  \"login\":{\"name\":\"Manio\","
                "\"passwordHash\":\"0774be374bdab4fb47ad1b85baddc3b9cbaee98d9a7abb34de6a8467afbfd231\","
                "\"passwordSalt\":\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-\"},\n"
                "  \"stats\":{\"level\":1,\"alive\":true}\n"
                "}\n";
        }
        result = store.authenticate("manio", *password, false);
        EXPECT(result.status == AccountStoreStatus::AuthenticatedMigrated);
        EXPECT(result.authenticated());
        EXPECT(result.legacyMaterialRemoved);
        const std::string migrated = readFile(legacyPath);
        EXPECT(migrated.find("passwordHash") == std::string::npos);
        EXPECT(migrated.find("passwordSalt") == std::string::npos);
        EXPECT(migrated.find("\"schemaVersion\":1") != std::string::npos);
        EXPECT(migrated.find("\"level\":1") != std::string::npos);
        EXPECT(migrated.find("\"alive\":true") != std::string::npos);
        EXPECT(store.authenticate("MANIO", *password, false).authenticated());

        mwmp::persistence::AtomicWriteOptions injected;
        injected.injectFailure = [](mwmp::persistence::AtomicWriteStage stage) {
            return stage == mwmp::persistence::AtomicWriteStage::BeforeReplace;
        };
        AccountStore failing(directory / "failing-accounts", players, injected);
        result = failing.authenticate("Another Player", *password, true);
        EXPECT(result.status == AccountStoreStatus::PersistenceFailed);
        EXPECT(!result.authenticated());

        EXPECT(store.authenticate("bad\nname", *password, true).status
            == AccountStoreStatus::InvalidAccountName);

        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
    }

    std::string passwordText(const PasswordBuffer& password)
    {
        const auto characters = password.characters();
        return { characters.data(), characters.size() };
    }

    void testAuthenticationMessages()
    {
        std::string errorText;
        auto password = PasswordBuffer::copyFrom("account password", errorText);
        auto accessPassword = PasswordBuffer::copyFrom("server password", errorText);
        EXPECT(password.has_value());
        EXPECT(accessPassword.has_value());
        if (!password || !accessPassword)
            return;

        std::vector<std::byte> encoded;
        mwmp::protocol::CodecError error = mwmp::protocol::CodecError::InvalidValue;
        EXPECT(encodeAuthenticationRequest(AuthenticationOperation::Register,
            "Nerevar", *password, &*accessPassword, encoded, error));
        EXPECT(error == mwmp::protocol::CodecError::None);

        AuthenticationRequest request;
        EXPECT(static_cast<bool>(decodeAuthenticationRequest(encoded, request)));
        EXPECT(request.operation == AuthenticationOperation::Register);
        EXPECT(request.accountName == "Nerevar");
        EXPECT(request.password.has_value());
        EXPECT(request.password && passwordText(*request.password) == "account password");
        EXPECT(request.serverAccessPassword.has_value());
        EXPECT(request.serverAccessPassword
            && passwordText(*request.serverAccessPassword) == "server password");

        for (std::size_t size = 0; size < encoded.size(); ++size)
        {
            AuthenticationRequest truncated;
            truncated.accountName = "unchanged";
            EXPECT(!decodeAuthenticationRequest(
                std::span(encoded).first(size), truncated));
            EXPECT(truncated.accountName == "unchanged");
        }
        auto trailing = encoded;
        trailing.push_back(std::byte{ 0 });
        EXPECT(!decodeAuthenticationRequest(trailing, request));

        AuthenticationResponse response;
        response.status = AuthenticationResponseStatus::Registered;
        response.message = "account created";
        EXPECT(encodeAuthenticationResponse(response, encoded, error));
        AuthenticationResponse decoded;
        EXPECT(static_cast<bool>(decodeAuthenticationResponse(encoded, decoded)));
        EXPECT(decoded.status == AuthenticationResponseStatus::Registered);
        EXPECT(decoded.authenticated());
        EXPECT(decoded.message == "account created");
        for (std::size_t size = 0; size < encoded.size(); ++size)
        {
            AuthenticationResponse truncated;
            truncated.message = "unchanged";
            EXPECT(!decodeAuthenticationResponse(
                std::span(encoded).first(size), truncated));
            EXPECT(truncated.message == "unchanged");
        }
    }

    AuthenticationRequest makeRequest(std::string_view account,
        std::string_view password, std::string_view access,
        AuthenticationOperation operation = AuthenticationOperation::Login)
    {
        std::string error;
        AuthenticationRequest request;
        request.operation = operation;
        request.accountName = account;
        request.password = PasswordBuffer::copyFrom(password, error);
        if (!access.empty())
            request.serverAccessPassword = PasswordBuffer::copyFrom(access, error);
        return request;
    }

    void testServerAuthenticationService()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto directory = std::filesystem::temp_directory_path()
            / ("tes3mp-auth-service-" + unique);
        std::string error;
        auto access = PasswordBuffer::copyFrom("private server", error);
        EXPECT(access.has_value());
        if (!access)
            return;
        std::string accessHash;
        EXPECT(PasswordHash::createArgon2id(*access, accessHash, error));

        ServerAuthenticationService service(
            directory / "accounts", directory / "player");
        EXPECT(service.setAccessPasswordHash(accessHash, error));
        EXPECT(service.requiresAccessPassword());

        auto result = service.authenticate(makeRequest("Vivec", "secret", "wrong",
            AuthenticationOperation::Register), "127.0.0.1");
        EXPECT(result.response.status == AuthenticationResponseStatus::ServerAccessDenied);
        EXPECT(!result.response.authenticated());

        result = service.authenticate(makeRequest("Vivec", "secret", "private server",
            AuthenticationOperation::Register), "127.0.0.1");
        EXPECT(result.response.status == AuthenticationResponseStatus::Registered);
        EXPECT(result.response.authenticated());
        EXPECT(result.accountName == "Vivec");
        EXPECT(result.isNewAccount);

        result = service.authenticate(makeRequest("vivec", "secret", "private server"),
            "127.0.0.1");
        EXPECT(result.response.status == AuthenticationResponseStatus::Authenticated);
        EXPECT(!result.isNewAccount);

        const auto start = AuthenticationRateLimiter::Clock::time_point{};
        for (std::size_t attempt = 0; attempt < 5; ++attempt)
        {
            result = service.authenticate(makeRequest("Vivec", "bad", "private server"),
                "10.0.0.2", start + attempt * 1s);
            EXPECT(result.response.status == AuthenticationResponseStatus::InvalidCredentials);
        }
        result = service.authenticate(makeRequest("Vivec", "secret", "private server"),
            "10.0.0.2", start + 5s);
        EXPECT(result.response.status == AuthenticationResponseStatus::RateLimited);

        EXPECT(!service.setAccessPasswordHash("plaintext", error));
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
    }
}

int runAuthenticationTests()
{
    testArgon2idAndMigration();
    testRateLimits();
    testAccountStore();
    testAuthenticationMessages();
    testServerAuthenticationService();
    return sFailures;
}
