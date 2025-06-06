#ifndef WEBSOCKET_SESSION_HPP
#define WEBSOCKET_SESSION_HPP

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#include <memory>
#include <string>
#include <queue>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "SessionInterface.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class ChatServer;

/// WebSocket 세션을 처리하는 클래스
class WebSocketSession : public SessionInterface, public std::enable_shared_from_this<WebSocketSession>
{
private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::shared_ptr<ChatServer> server_;
    net::strand<net::any_io_executor> strand_;
    std::queue<std::shared_ptr<const std::string>> write_msgs_;
    bool is_writing_ = false;
    std::string nickname_;
    std::string remote_id_;
    std::string current_room_;
    bool authenticated_ = false;
    
    // 보안 설정
    static constexpr std::size_t max_message_size_ = 1024 * 1024; // 1MB 메시지 크기 제한
    static constexpr std::size_t max_queue_size_ = 100; // 최대 큐 크기
    
public:
    // Constructor
    explicit WebSocketSession(tcp::socket&& socket, std::shared_ptr<ChatServer> server);
    
    // Destructor
    ~WebSocketSession();
    
    // Start the asynchronous operation
    void run();
    
    // SessionInterface implementation
    void deliver(const std::string& msg) override;
    void stop_session() override;
    const std::string& nickname() const override { return nickname_; }
    const std::string& remote_id() const override { return remote_id_; }
    net::strand<net::any_io_executor>& get_strand() override { return strand_; }
    bool is_authenticated() const override { return authenticated_; }
    void set_nickname(const std::string& nick) override { nickname_ = nick; }
    void set_authenticated(bool auth) override { authenticated_ = auth; }
    const std::string& current_room() const override { return current_room_; }
    void set_current_room(const std::string& room_name) override { current_room_ = room_name; }
    std::shared_ptr<SessionInterface> shared_from_this() override {
        return std::static_pointer_cast<SessionInterface>(
            std::enable_shared_from_this<WebSocketSession>::shared_from_this());
    }
    
private:
    // Accept the websocket handshake
    void on_accept(beast::error_code ec);
    
    // Read a message from the client
    void do_read();
    
    // Called when a message is received
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    
    // Write the next message in the queue
    void do_write();
    
    // Called when a message is sent
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    
    // Process a message from the client
    void process_message(const std::string& message);
    
    // Handle authentication
    void handle_auth(const std::string& username, const std::string& password);
};

#endif // WEBSOCKET_SESSION_HPP 