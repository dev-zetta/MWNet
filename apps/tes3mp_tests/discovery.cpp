#include <components/openmw-mp/Discovery/Client.hpp>
#include <components/openmw-mp/Discovery/Directory.hpp>
#include <components/openmw-mp/Discovery/Probe.hpp>
#include <components/openmw-mp/Transport/GameEndpoint.hpp>
#include <components/openmw-mp/Security/SodiumInit.hpp>
#include <components/openmw-mp/Metrics/ProcessMemory.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace mwmp;
using namespace mwmp::discovery;
namespace
{
    void check(bool ok, const char* message)
    { if (!ok) throw std::runtime_error(message); }
    template<class F> void rejects(F action)
    { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } check(rejected,"input was not rejected"); }
    const std::string origin = "https://directory.test";
    struct Fixture
    {
        std::filesystem::path root;
        std::string error;
        security::ServerIdentity identity;
        Listing listing;
        Fixture() : root(std::filesystem::temp_directory_path()/ ("tes3mp-discovery-"+randomNonce())),
            identity(std::move(*security::ServerIdentity::loadOrCreate(root/"identity.key",error)))
        { listing.host = "127.0.0.1"; listing.name = "Test server"; listing.version = "1.0-test";
            listing.content = {{"first.esm",{1,2}},{"second.esp",{3}}}; }
        ~Fixture() { std::filesystem::remove_all(root); }
    };
    Envelope announcement(Directory& directory, Fixture& f, std::string operation = "announce", std::string source = "test")
    {
        Json request; request.put("publicKey",hex(f.identity.publicKey())); request.put("operation",operation);
        auto reply = directory.handle("POST","/v1/challenges",writeJson(request),source);
        check(reply.status == 200,"challenge failed");
        return sign(f.identity,f.listing,origin,operation,reply.body.get<std::string>("nonce"));
    }
    Reply submit(Directory& directory, const Envelope& envelope, std::string source = "test")
    {
        return directory.handle(envelope.operation == "withdraw" ? "DELETE" : "PUT",
            "/v1/servers/"+fingerprint(envelope).substr(8),writeJson(toJson(envelope)),source);
    }
    std::size_t visible(Directory& directory)
    { return directory.handle("GET","/v1/servers",{},"reader").body.get_child("servers").size(); }
    void unit()
    {
        Fixture f;
        std::int64_t now = 1000;
        Directory directory((f.root/"directory.db").string(),origin,[&]{ return now; },true);
        auto e = announcement(directory,f);
        check(verify(e,origin) == f.listing,"signed content round trip");
        auto bad = e; bad.payload.back() = bad.payload.back() == '0' ? '1' : '0';
        rejects([&]{ verify(bad,origin); });
        rejects([&]{ verify(e,"https://another.test"); });
        rejects([&]{ decode(std::vector<unsigned char>{1,0,0,0}); });
        rejects([&]{ parseJson(std::string(40,'[')); });
        auto duplicate = toJson(e); duplicate.push_back({"origin",Json(origin)});
        rejects([&]{ envelopeFromJson(duplicate); });
        check(submit(directory,e).status == 202,"registration must be pending");
        check(visible(directory) == 0,"unverified listing leaked");
        check(submit(directory,e).status == 409,"replay accepted");
        auto job = directory.nextVerification(); check(job.has_value(),"missing verification");
        directory.complete(*job,true);
        check(visible(directory) == 1,"verified server not listed");
        // A challenge issued to another source must not consume the owner's quota.
        auto ownerChallenge = announcement(directory,f,"announce","owner");
        check(submit(directory,ownerChallenge,"other").status == 409,"cross-source challenge replay");
        check(submit(directory,ownerChallenge,"owner").status == 200,"owner challenge lost");
        f.listing.players = 2;
        check(submit(directory,announcement(directory,f)).status == 200,"heartbeat not accepted");
        now += 60;
        auto expired = announcement(directory,f); now += 31;
        check(submit(directory,expired).status == 409,"expired challenge accepted");
        f.listing.port++;
        check(submit(directory,announcement(directory,f)).status == 202,"endpoint change remained verified");
        directory.complete(*job,true);
        check(visible(directory) == 0,"stale verification applied to changed endpoint");
        job = directory.nextVerification(); check(job.has_value(),"new endpoint not checked");
        directory.complete(*job,false);
        check(visible(directory) == 0,"failed endpoint published");
        now += 31;
        job = directory.nextVerification(); check(job.has_value(),"retry not scheduled");
        directory.complete(*job,true);
        directory.backup((f.root/"backup.db").string());
        {
            Directory restored((f.root/"backup.db").string(),origin,[&]{return now;},true);
            check(visible(restored) == 0,"restart trusted stale verification");
            auto restoredJob = restored.nextVerification(); check(restoredJob.has_value(),"restart lost unexpired entry");
            restored.complete(*restoredJob,true); check(visible(restored) == 1,"restored entry not verified");
        }
        now += 181; directory.maintain();
        check(directory.metrics().get<unsigned>("listings") == 0,"expired rows retained");
        check(submit(directory,announcement(directory,f)).status == 202,"reregistration failed");
        check(submit(directory,announcement(directory,f,"withdraw")).status == 200,"withdrawal failed");
        check(directory.metrics().get<unsigned>("listings") == 0,"withdrawn entry retained");
        directory.block("identity",f.identity.fingerprint().substr(8));
        rejects([&]{ announcement(directory,f); });
        directory.block("identity",f.identity.fingerprint().substr(8),true);
        check(submit(directory,announcement(directory,f)).status == 202,"unblock failed");
        for (int i=0;i<8;++i)
        {
            now += 60;
            check(submit(directory,announcement(directory,f)).status == 200 || visible(directory) == 0,"heartbeat cycle failed");
            auto verification = directory.nextVerification();
            if (verification) directory.complete(*verification,true);
        }
        now += 301; directory.maintain();
        check(directory.metrics().get<unsigned>("challenges") == 0,"challenge retention");
        check(directory.metrics().get<unsigned>("rateBuckets") == 0,"rate bucket retention");
        check(!publicAddress("127.0.0.1") && !publicAddress("10.0.0.1") && !publicAddress("100.64.0.1")
            && !publicAddress("169.254.169.254") && !publicAddress("192.168.1.2")
            && !publicAddress("::1") && !publicAddress("::ffff:127.0.0.1") && !publicAddress("2001:db8::1")
            && !publicAddress("fc00::1") && !publicAddress("2002:7f00:1::")
            && publicAddress("8.8.8.8") && publicAddress("2606:4700:4700::1111"),"public address policy");
        check(directory.handle("PUT","/v1/servers/invalid","{}","test").status == 400,"bad id accepted");
        check(directory.handle("GET","/v1/servers?after=bad",{},"test").status == 400,"bad cursor accepted");
        check(directory.handle("POST","/v1/challenges",std::string(maximumBody+1,'x'),"test").status == 413,"body limit");
        for (int i=0;i<600;++i) directory.handle("GET","/v1/servers",{},"limited");
        check(directory.handle("GET","/v1/servers",{},"limited").status == 429,"rate limit");
        // Independently bound both entry count and aggregate payload bytes.
        now += 181; directory.maintain();
        std::vector<security::ServerIdentity> identities;
        auto bulkSubmit = [&](const security::ServerIdentity& key, const Listing& listing, unsigned index) {
            const std::string source = "bulk-"+std::to_string(index/100);
            Json query; query.put("publicKey",hex(key.publicKey())); query.put("operation","announce");
            auto nonce = directory.handle("POST","/v1/challenges",writeJson(query),source);
            check(nonce.status == 200,"bulk challenge");
            return submit(directory,sign(key,listing,origin,"announce",nonce.body.get<std::string>("nonce")),source);
        };
        for (unsigned i = 0; i <= maximumListings; ++i)
        {
            auto key = security::ServerIdentity::loadOrCreate(f.root/("bulk-"+std::to_string(i)+".key"),f.error);
            check(bool(key),"bulk key creation");
            identities.push_back(std::move(*key));
            check(bulkSubmit(identities.back(),f.listing,i).status == (i < maximumListings ? 202 : 429),"listing count budget");
        }
        check(directory.metrics().get<unsigned>("listings") == maximumListings,"count cap did not retain expected entries");
        auto first = directory.nextVerification(), second = directory.nextVerification();
        check(first && second && !directory.nextVerification(),"verification concurrency budget");
        directory.complete(*first,false); directory.complete(*second,false);
        auto next = directory.nextVerification();
        check(next && next->id != first->id && next->id != second->id,"failed endpoints starved new verification");
        // Pagination carries signed records without truncating their content.
        directory.complete(*next,true);
        check(visible(directory) == 1,"bulk verified list");
        now += 181; directory.maintain();
        check(directory.metrics().get<unsigned>("listings") == 0,"bulk expiration cleanup");
        Listing large = f.listing;
        large.content.clear();
        for (unsigned i=0;i<1000;++i) large.content.push_back({"plugin-"+std::to_string(i)+".esm",std::vector<std::uint32_t>(50,42)});
        bool byteCap = false;
        for (unsigned i=0;i<50;++i)
        {
            auto response = bulkSubmit(identities[i],large,i);
            if (response.status == 429) { byteCap = true; break; }
            check(response.status == 202,"large valid listing");
        }
        check(byteCap && directory.metrics().get<unsigned>("listingBytes") <= 16*1024*1024,"aggregate byte budget");
        now += 181; directory.maintain();
        for (unsigned i=0;i<identities.size();++i)
        {
            Json query; query.put("publicKey",hex(identities[i].publicKey())); query.put("operation","announce");
            const auto source = "challenges-"+std::to_string(i/100);
            check(directory.handle("POST","/v1/challenges",writeJson(query),source).status == 200,"first per-key challenge");
            check(directory.handle("POST","/v1/challenges",writeJson(query),source).status == 200,"second per-key challenge");
            check(directory.handle("POST","/v1/challenges",writeJson(query),source).status == 429,"per-source key challenge cap");
        }
        now += 31; directory.maintain();
        check(directory.metrics().get<unsigned>("challenges") == 0,"bulk challenge cleanup");
        auto invalidText = f.listing; invalidText.name = std::string(1,static_cast<char>(0xff));
        rejects([&]{ encode(invalidText); });
        Directory production((f.root/"public.db").string(),origin,[&]{return now;});
        check(submit(production,announcement(production,f)).status == 400,"private production registration");
        std::cout << "discovery protocol, leases, restart, restore, replay, limits and address policy passed\n";
    }
    struct Game
    {
        std::unique_ptr<transport::GameEndpoint> endpoint;
        std::atomic_bool stop{false};
        std::thread thread;
        Game(Fixture& fixture)
        {
            transport::TransportError error;
            bool listening = false;
            for (unsigned attempt=0; attempt<20 && !listening; ++attempt)
            {
                endpoint = transport::GameEndpoint::createServer(fixture.root/"identity.key",fixture.error);
                check(bool(endpoint),"game endpoint creation");
                transport::ListenOptions options;
                options.port = static_cast<std::uint16_t>(20000+randombytes_uniform(30000));
                listening = endpoint->listen(options,error);
                if (listening) fixture.listing.port = options.port;
            }
            check(listening,"game endpoint listen");
            thread = std::thread([this] { while (!stop) (void)endpoint->poll(std::chrono::milliseconds(10)); });
        }
        ~Game() { stop = true; thread.join(); endpoint->shutdown(std::chrono::milliseconds(100)); }
    };
    void transportTests()
    {
        Fixture fixture; Game game(fixture); std::atomic_bool cancel{false};
        check(probe(fixture.listing,fixture.identity.fingerprint(),true,cancel),"real encrypted verification failed");
        Fixture wrong;
        check(!probe(fixture.listing,wrong.identity.fingerprint(),true,cancel),"wrong identity accepted");
        check(!probe(fixture.listing,fixture.identity.fingerprint(),false,cancel),"private address accepted in production");
        auto client = transport::GameEndpoint::createClient(fixture.root/"trust.json",fixture.error);
        transport::ConnectOptions options; options.host = fixture.listing.host; options.port = fixture.listing.port;
        options.expectedFingerprint = fixture.identity.fingerprint();
        transport::TransportConnectionId connection; transport::TransportError error;
        check(client->connect(options,connection,error),"client discovery connect");
        bool confirmation = false;
        auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(4);
        while (std::chrono::steady_clock::now()<deadline)
        {
            auto event = client->poll(std::chrono::milliseconds(10));
            if (event && event->type == transport::TransportEventType::TrustRequired) { confirmation = true; break; }
        }
        check(confirmation,"discovery bypassed first-use confirmation");
        check(!std::filesystem::exists(fixture.root/"trust.json"),"discovery wrote a saved fingerprint");
        client->shutdown(std::chrono::milliseconds(100)); client.reset();
        auto store = security::TrustStore::load(fixture.root/"trust.json",fixture.error);
        check(store->trust(fixture.listing.host,fixture.listing.port,wrong.identity.fingerprint(),fixture.error),"seed trust");
        client = transport::GameEndpoint::createClient(fixture.root/"trust.json",fixture.error);
        check(client->connect(options,connection,error),"saved mismatch connect");
        bool disconnected = false;
        deadline = std::chrono::steady_clock::now()+std::chrono::seconds(4);
        while (std::chrono::steady_clock::now()<deadline)
        {
            auto event = client->poll(std::chrono::milliseconds(10));
            if (!event) continue;
            check(event->type != transport::TransportEventType::Connected && event->type != transport::TransportEventType::TrustRequired,
                "directory overrode saved trust");
            if (event->type == transport::TransportEventType::Disconnected) { disconnected = true; break; }
        }
        check(disconnected,"saved mismatch not rejected");
        std::cout << "encrypted reachability, identity pinning and first-use trust passed\n";
    }
    void https(const HttpOptions& options, int seconds)
    {
        Fixture fixture; Game game(fixture); std::atomic_bool cancel{false};
        const auto gamePort = fixture.listing.port;
        auto send = [&](std::string operation) {
            auto nonce = challenge(options,hex(fixture.identity.publicKey()),operation,cancel);
            return publish(options,sign(fixture.identity,fixture.listing,options.origin,operation,nonce),cancel);
        };
        check(send("announce") == 202,"new HTTP listing must wait for verification");
        bool listed = false;
        for (int i=0;i<30;++i)
        {
            auto page = fetch(options,"",cancel);
            for (const auto& server : page.servers) if (server.fingerprint == fixture.identity.fingerprint())
            { check(server.listing == fixture.listing,"HTTPS signed metadata differs"); listed = true; }
            if (listed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        check(listed,"directory did not verify real game endpoint");
        // Incompatible metadata stays discoverable; joining policy must filter it.
        fixture.listing.protocol = 11; fixture.listing.password = true; send("announce");
        auto incompatible = fetch(options,"",cancel);
        check(!incompatible.servers.empty() && incompatible.servers.front().listing.protocol == 11
            && incompatible.servers.front().listing.password,"compatibility metadata lost");
        fixture.listing.protocol = 12;
        send("withdraw"); check(fetch(options,"",cancel).servers.empty(),"HTTPS withdrawal failed");
        const auto started = std::chrono::steady_clock::now();
        const auto until = started+std::chrono::seconds(seconds);
        std::size_t cycles = 0, outages = 0;
        auto report = started;
        while (std::chrono::steady_clock::now() < until)
        {
            fixture.listing.players = static_cast<std::uint16_t>(cycles%65);
            fixture.listing.name = "Churn "+std::to_string(cycles);
            // Alternate reachable and closed endpoints; every cycle removes the old lease.
            fixture.listing.port = cycles%4 == 0 ? 9 : gamePort;
            try
            {
                send("announce");
                auto page = fetch(options,"",cancel); (void)page;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                send("withdraw");
                ++cycles;
            }
            catch (const std::exception& e)
            {
                ++outages;
                std::cout << "outage=" << outages << " error=" << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            if (std::chrono::steady_clock::now() >= report)
            {
                std::cout << "cycles=" << cycles << " elapsed_seconds="
                    << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started).count()
                    << " resident_bytes=" << metrics::residentMemoryBytes() << std::endl;
                report = std::chrono::steady_clock::now()+std::chrono::seconds(60);
            }
        }
        send("withdraw");
        check(fetch(options,"",cancel).servers.empty(),"HTTPS final listing cleanup");
        check(seconds == 0 || cycles >= static_cast<std::size_t>(seconds/10), "insufficient successful churn cycles");
        std::cout << "HTTPS verification and churn complete; cycles=" << cycles << " outages=" << outages << std::endl;
    }
}
int main(int argc, char** argv)
{
    try
    {
        std::string error; check(security::initializeSodium(&error),"sodium initialization");
        if (argc >= 4 && std::string_view(argv[1]) == "--https")
            https({argv[2],argv[3]},argc > 4 ? std::stoi(argv[4]) : 0);
        else { unit(); transportTests(); }
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "discovery test failure: " << e.what() << '\n'; return 1; }
}
