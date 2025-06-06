#include "WebSocketListener.hpp"
#include "WebSocketSession.hpp"
#include "WebSocketSSLSession.hpp"
#include "ChatServer.hpp"
#include <spdlog/spdlog.h>

// HTTP/WS용 생성자
WebSocketListener::WebSocketListener(net::io_context& ioc, 
                                     tcp::endpoint endpoint, 
                                     std::shared_ptr<ChatServer> server)
    : ioc_(ioc)
    , acceptor_(net::make_strand(ioc))
    , server_(server)
    , use_ssl_(false)
{
    init_acceptor(endpoint);
}

// HTTPS/WSS용 생성자
WebSocketListener::WebSocketListener(net::io_context& ioc, 
                                     tcp::endpoint endpoint, 
                                     std::shared_ptr<ChatServer> server,
                                     ssl::context&& ctx)
    : ioc_(ioc)
    , acceptor_(net::make_strand(ioc))
    , server_(server)
    , ssl_ctx_(std::move(ctx))
    , use_ssl_(true)
{
    init_acceptor(endpoint);
}

void WebSocketListener::init_acceptor(tcp::endpoint endpoint)
{
    beast::error_code ec;
    
    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("WebSocketListener: Error opening acceptor: {}", ec.message());
        throw beast::system_error{ec};
    }
    
    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        spdlog::error("WebSocketListener: Error setting reuse_address: {}", ec.message());
        throw beast::system_error{ec};
    }
    
    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
        spdlog::error("WebSocketListener: Error binding to endpoint: {}", ec.message());
        throw beast::system_error{ec};
    }
    
    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("WebSocketListener: Error listening: {}", ec.message());
        throw beast::system_error{ec};
    }
    
    spdlog::info("WebSocketListener: Listening on {}:{} ({})", 
                 endpoint.address().to_string(), endpoint.port(),
                 use_ssl_ ? "WSS" : "WS");
}

void WebSocketListener::run()
{
    do_accept();
}

void WebSocketListener::do_accept()
{
    // Accept a new connection
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &WebSocketListener::on_accept,
            shared_from_this()));
}

void WebSocketListener::on_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec) {
        spdlog::error("WebSocketListener: Accept failed: {}", ec.message());
    } else {
        // Create and run a WebSocket session (SSL or non-SSL)
        if (use_ssl_ && ssl_ctx_.has_value()) {
            auto session = std::make_shared<WebSocketSSLSession>(std::move(socket), *ssl_ctx_, server_);
            session->run();
            spdlog::info("WebSocketListener: New WebSocket SSL connection accepted");
        } else {
            auto session = std::make_shared<WebSocketSession>(std::move(socket), server_);
            session->run();
            spdlog::info("WebSocketListener: New WebSocket connection accepted");
        }
    }
    
    // Accept another connection
    do_accept();
} 