#include <components/openmw-mp/Discovery/Directory.hpp>
#include <components/openmw-mp/Discovery/Probe.hpp>
#include <components/openmw-mp/Security/SodiumInit.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <atomic>
#include <charconv>
#include <iostream>
#include <memory>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using namespace mwmp::discovery;

namespace
{
    struct Service
    {
        asio::io_context io;
        asio::thread_pool workers{2};
        asio::steady_timer timer{io};
        Directory directory;
        std::atomic_bool stop{false};
        bool localTest, proxy, healthy = true;
        unsigned jobs = 0, connections = 0;
        Service(std::string database, std::string origin, bool local, bool trustProxy)
            : directory(std::move(database), std::move(origin), {}, local), localTest(local), proxy(trustProxy) {}
        ~Service() { stop = true; workers.join(); }
        void tick()
        {
            try
            {
                directory.maintain();
                healthy = true;
                while (jobs < 2)
                {
                    auto job = directory.nextVerification();
                    if (!job) break;
                    ++jobs;
                    asio::post(workers, [this, task=*job] {
                        bool reachable = false;
                        try { reachable = probe(task.listing, "ed25519/"+task.id, localTest,stop); }
                        catch (const std::exception&) {}
                        asio::post(io,[this,task,reachable] { --jobs; directory.complete(task,reachable); });
                    });
                }
            }
            catch (const std::exception& e) { healthy = false; std::cerr << "maintenance: " << e.what() << '\n'; }
            timer.expires_after(std::chrono::seconds(1));
            timer.async_wait([this](beast::error_code error) { if (!error && !stop) tick(); });
        }
    };
    class Connection : public std::enable_shared_from_this<Connection>
    {
        Service& mService;
        beast::tcp_stream mStream;
        beast::flat_buffer mBuffer{maximumBody+8192};
        http::request_parser<http::string_body> mParser;
        http::response<http::string_body> mResponse;
    public:
        Connection(Service& service, tcp::socket socket) : mService(service), mStream(std::move(socket))
        { ++mService.connections; mParser.body_limit(maximumBody); mParser.header_limit(8192); }
        ~Connection() { --mService.connections; }
        void start()
        {
            mStream.expires_after(std::chrono::seconds(5));
            http::async_read(mStream,mBuffer,mParser,[self=shared_from_this()](beast::error_code error,std::size_t) {
                if (!error) self->respond();
            });
        }
        void respond()
        {
            auto request = mParser.release();
            Reply reply{503,{}};
            try
            {
                const std::string target(request.target());
                if (target == "/healthz") { reply.status = mService.healthy ? 200 : 503;
                    reply.body.put("status",mService.healthy ? "ok" : "storage unavailable"); }
                else if (target == "/metrics") { reply.status = 200; reply.body = mService.directory.metrics(); }
                else
                {
                    std::string source = mStream.socket().remote_endpoint().address().to_string();
                    if (mService.proxy)
                    {
                        const auto header = request.find("X-Real-IP");
                        if (header != request.end())
                        {
                            beast::error_code error;
                            auto address = asio::ip::make_address(std::string(header->value()),error);
                            if (!error) source = address.to_string();
                        }
                    }
                    reply = mService.directory.handle(std::string(request.method_string()),target,request.body(),source);
                }
            }
            catch (const std::invalid_argument&) { reply.status = 400; reply.body.put("error","invalid public endpoint or request"); }
            catch (const boost::property_tree::ptree_error&) { reply.status = 400; reply.body.put("error","invalid request"); }
            catch (const std::exception& e) { std::cerr << "request failed: " << e.what() << '\n'; reply.body.put("error","directory unavailable"); }
            mResponse.version(11);
            mResponse.result(static_cast<http::status>(reply.status));
            mResponse.set(http::field::content_type,"application/json");
            mResponse.set(http::field::cache_control,"no-store");
            mResponse.keep_alive(false);
            mResponse.body() = writeJson(reply.body);
            mResponse.prepare_payload();
            mStream.expires_after(std::chrono::seconds(5));
            http::async_write(mStream,mResponse,[self=shared_from_this()](beast::error_code,std::size_t) {
                beast::error_code error;
                self->mStream.socket().shutdown(tcp::socket::shutdown_both,error);
            });
        }
    };
    void accept(Service& service, tcp::acceptor& acceptor)
    {
        acceptor.async_accept([&](beast::error_code error,tcp::socket socket) {
            if (!error && service.connections < 32) std::make_shared<Connection>(service,std::move(socket))->start();
            if (!service.stop) accept(service,acceptor);
        });
    }
}
int main(int argc, char** argv)
{
    try
    {
        std::string origin, database = "directory.sqlite3", bind = "127.0.0.1", action, value;
        unsigned port = 8080;
        bool local = false, proxy = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg(argv[i]);
            if (arg == "--local-test") { local = true; continue; }
            if (arg == "--trust-proxy") { proxy = true; continue; }
            if (++i == argc) throw std::invalid_argument("missing option value");
            const std::string setting(argv[i]);
            if (arg == "--origin") origin = setting;
            else if (arg == "--database") database = setting;
            else if (arg == "--bind") bind = setting;
            else if (arg == "--port")
            {
                const auto parsed = std::from_chars(setting.data(),setting.data()+setting.size(),port);
                if (parsed.ec != std::errc{} || parsed.ptr != setting.data()+setting.size() || port == 0 || port > 65535)
                    throw std::invalid_argument("invalid HTTP port");
            }
            else if (arg == "--block-identity" || arg == "--block-endpoint" || arg == "--unblock-identity"
                || arg == "--unblock-endpoint" || arg == "--backup") { action = arg; value = setting; }
            else throw std::invalid_argument("unknown option");
        }
        std::string error;
        if (!mwmp::security::initializeSodium(&error)) throw std::runtime_error(error);
        Service service(database,origin,local,proxy);
        if (!action.empty())
        {
            if (action == "--backup") service.directory.backup(value);
            else service.directory.block(action.ends_with("identity") ? "identity" : "endpoint",value,
                action.starts_with("--unblock"));
            return 0;
        }
        tcp::acceptor acceptor(service.io,{asio::ip::make_address(bind),static_cast<unsigned short>(port)});
        asio::signal_set signals(service.io,SIGINT,SIGTERM);
        signals.async_wait([&](beast::error_code,int) { service.stop = true; service.io.stop(); });
        accept(service,acceptor);
        service.tick();
        std::cout << "Directory API v1 listening on " << bind << ':' << port
            << (local ? " (PRIVATE TEST ENDPOINTS ENABLED)" : "") << std::endl;
        service.io.run();
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
