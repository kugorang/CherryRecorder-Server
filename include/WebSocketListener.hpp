#ifndef WEBSOCKET_LISTENER_HPP
#define WEBSOCKET_LISTENER_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <optional>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class ChatServer;

/// WebSocket 연결을 수신하고 WebSocketSession을 생성하는 클래스
class WebSocketListener : public std::enable_shared_from_this<WebSocketListener>
{
private:
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<ChatServer> server_;
    std::optional<ssl::context> ssl_ctx_; // SSL 컨텍스트 (WSS용)
    bool use_ssl_;
    
public:
    // HTTP/WS용 생성자
    WebSocketListener(net::io_context& ioc, 
                      tcp::endpoint endpoint, 
                      std::shared_ptr<ChatServer> server);
    
    // HTTPS/WSS용 생성자
    WebSocketListener(net::io_context& ioc, 
                      tcp::endpoint endpoint, 
                      std::shared_ptr<ChatServer> server,
                      ssl::context&& ctx);
    
    // Start accepting connections
    void run();
    
private:
    void init_acceptor(tcp::endpoint endpoint);
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);
};

#endif // WEBSOCKET_LISTENER_HPP 