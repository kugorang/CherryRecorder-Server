#include "HttpsServer.hpp"
#include <spdlog/spdlog.h>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <iostream>
#include <string>
#include <cstring>

// HttpsSession 클래스 정의 (HttpSession과 유사하지만 SSL 추가)
class HttpsSession : public std::enable_shared_from_this<HttpsSession>
{
    beast::ssl_stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;
    std::shared_ptr<PlacesApiHandler> places_handler_;

public:
    explicit HttpsSession(tcp::socket&& socket, ssl::context& ctx, std::shared_ptr<PlacesApiHandler> handler)
        : stream_(std::move(socket), ctx)
        , places_handler_(handler)
    {
    }

    void run()
    {
        // SSL 핸드셰이크 수행
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(
                &HttpsSession::on_handshake,
                shared_from_this()));
    }

private:
    void on_handshake()
    {
        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(stream_.native_handle(), "localhost"))
        {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            spdlog::error("[HttpsSession] SNI hostname set failed: {}", ec.message());
            return;
        }

        stream_.async_handshake(
            ssl::stream_base::server,
            beast::bind_front_handler(
                &HttpsSession::on_handshake_complete,
                shared_from_this()));
    }

    void on_handshake_complete(beast::error_code ec)
    {
        if (ec)
        {
            spdlog::error("[HttpsSession] SSL handshake failed: {}", ec.message());
            return;
        }

        do_read();
    }

    void do_read()
    {
        req_ = {};
        
        // SSL stream은 expires_after를 지원하지 않으므로
        // tcp_stream에 대해 타임아웃 설정
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(
                &HttpsSession::on_read,
                shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec == http::error::end_of_stream)
            return do_close();

        if (ec)
        {
            spdlog::error("[HttpsSession] Read failed: {}", ec.message());
            return;
        }

        // 요청 처리
        handle_request();
    }

    void handle_request()
    {
        auto const bad_request = [&req = req_](beast::string_view why)
        {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::server, "CherryRecorder/1.0 HTTPS");
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
        };

        auto const not_found = [&req = req_](beast::string_view target)
        {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.set(http::field::server, "CherryRecorder/1.0 HTTPS");
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "The resource '" + std::string(target) + "' was not found.";
            res.prepare_payload();
            return res;
        };

        auto const server_error = [&req = req_](beast::string_view what)
        {
            http::response<http::string_body> res{http::status::internal_server_error, req.version()};
            res.set(http::field::server, "CherryRecorder/1.0 HTTPS");
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "An error occurred: '" + std::string(what) + "'";
            res.prepare_payload();
            return res;
        };

        // 메서드 확인
        if (req_.method() != http::verb::get &&
            req_.method() != http::verb::head &&
            req_.method() != http::verb::post)
        {
            res_ = bad_request("Unknown HTTP-method");
            return do_write();
        }

        // Request path 확인
        if (req_.target().empty() || req_.target()[0] != '/' || req_.target().find("..") != beast::string_view::npos)
        {
            res_ = bad_request("Illegal request-target");
            return do_write();
        }

        std::string target_str(req_.target());
        
        // Health check endpoint
        if (target_str == "/health" || target_str == "/health/")
        {
            res_ = http::response<http::string_body>{http::status::ok, req_.version()};
            res_.set(http::field::server, "CherryRecorder/1.0 HTTPS");
            res_.set(http::field::content_type, "text/plain");
            res_.keep_alive(req_.keep_alive());
            res_.body() = "OK";
            res_.prepare_payload();
            return do_write();
        }

        // Places API endpoint
        if (target_str.find("/api/places/nearby") == 0 && req_.method() == http::verb::post && places_handler_)
        {
            try
            {
                res_ = places_handler_->handleNearbySearch(req_);
            }
            catch (const std::exception& e)
            {
                spdlog::error("[HttpsSession] Exception in places handler: {}", e.what());
                res_ = server_error(e.what());
            }
            return do_write();
        }

        // 기본 응답
        res_ = not_found(req_.target());
        do_write();
    }

    void do_write()
    {
        http::async_write(stream_, res_,
            beast::bind_front_handler(
                &HttpsSession::on_write,
                shared_from_this(),
                res_.need_eof()));
    }

    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
        {
            spdlog::error("[HttpsSession] Write failed: {}", ec.message());
            return;
        }

        if (close)
        {
            return do_close();
        }

        res_ = {};
        do_read();
    }

    void do_close()
    {
        stream_.async_shutdown(
            beast::bind_front_handler(
                &HttpsSession::on_shutdown,
                shared_from_this()));
    }

    void on_shutdown(beast::error_code ec)
    {
        if (ec)
        {
            spdlog::debug("[HttpsSession] Shutdown: {}", ec.message());
        }
    }
};

// HttpsListener 구현
HttpsListener::HttpsListener(
    net::io_context& ioc,
    ssl::context& ctx,
    tcp::endpoint endpoint)
    : ioc_(ioc)
    , ctx_(ctx)
    , acceptor_(net::make_strand(ioc))
{
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
        spdlog::error("[HttpsListener] Error opening acceptor: {}", ec.message());
        throw beast::system_error{ec};
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
    {
        spdlog::error("[HttpsListener] Error setting SO_REUSEADDR: {}", ec.message());
        throw beast::system_error{ec};
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec)
    {
        spdlog::error("[HttpsListener] Error binding: {}", ec.message());
        throw beast::system_error{ec};
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        spdlog::error("[HttpsListener] Error listening: {}", ec.message());
        throw beast::system_error{ec};
    }

    // Create PlacesApiHandler
    const char* api_key = std::getenv("GOOGLE_MAPS_API_KEY");
    if (api_key && std::strlen(api_key) > 0)
    {
        places_handler_ = std::make_shared<PlacesApiHandler>(api_key);
        spdlog::info("[HttpsListener] PlacesApiHandler created with API key");
    }
    else
    {
        spdlog::warn("[HttpsListener] GOOGLE_MAPS_API_KEY not set. Places API will not be available.");
    }
}

void HttpsListener::run()
{
    do_accept();
}

void HttpsListener::do_accept()
{
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &HttpsListener::on_accept,
            shared_from_this()));
}

void HttpsListener::on_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec)
    {
        spdlog::error("[HttpsListener] Accept error: {}", ec.message());
    }
    else
    {
        // Create and run the session
        std::make_shared<HttpsSession>(std::move(socket), ctx_, places_handler_)->run();
    }

    // Accept another connection
    do_accept();
}

// HttpsServer 구현
HttpsServer::HttpsServer(const std::string& address, 
                        unsigned short port, 
                        int threads,
                        const std::string& cert_file,
                        const std::string& key_file,
                        const std::string& dh_file)
    : address_(address)
    , port_(port)
    , threads_(threads <= 0 ? std::thread::hardware_concurrency() : threads)
    , cert_file_(cert_file)
    , key_file_(key_file)
    , dh_file_(dh_file)
    , ioc_(threads_)
{
    setup_ssl_context();
}

void HttpsServer::setup_ssl_context()
{
    // SSL 옵션 설정
    ctx_.set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::no_sslv3 |
        ssl::context::no_tlsv1 |
        ssl::context::no_tlsv1_1 |
        ssl::context::single_dh_use);

    // 인증서와 키 파일 로드
    ctx_.use_certificate_chain_file(cert_file_);
    ctx_.use_private_key_file(key_file_, ssl::context::pem);

    // DH 파일이 있으면 로드
    if (!dh_file_.empty())
    {
        ctx_.use_tmp_dh_file(dh_file_);
    }

    spdlog::info("[HttpsServer] SSL context configured with cert: {}", cert_file_);
}

void HttpsServer::run()
{
    try
    {
        auto const address = net::ip::make_address(address_);
        listener_ = std::make_shared<HttpsListener>(ioc_, ctx_, tcp::endpoint{address, port_});
        listener_->run();

        io_threads_.reserve(threads_);
        for (int i = 0; i < threads_; ++i)
        {
            io_threads_.emplace_back([this, i]()
            {
                spdlog::info("[HttpsServer] IO thread {} started", i);
                ioc_.run();
                spdlog::info("[HttpsServer] IO thread {} stopped", i);
            });
        }

        spdlog::info("[HttpsServer] Started on {}:{} with {} threads", address_, port_, threads_);
    }
    catch (const std::exception& e)
    {
        spdlog::error("[HttpsServer] Failed to start: {}", e.what());
        throw;
    }
}

void HttpsServer::stop()
{
    spdlog::info("[HttpsServer] Stopping server...");
    
    ioc_.stop();
    
    for (auto& t : io_threads_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    
    listener_.reset();
    
    spdlog::info("[HttpsServer] Server stopped");
} 