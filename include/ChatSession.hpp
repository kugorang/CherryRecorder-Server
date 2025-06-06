// include/ChatSession.hpp
#pragma once

#include <memory>
#include <string>
#include <deque>
#include <atomic>
#include <vector>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/error.hpp> // For beast::error_code
#include "SessionInterface.hpp"

// Forward declarations
class ChatServer;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;

class ChatSession : public SessionInterface, public std::enable_shared_from_this<ChatSession> {
    friend class ChatServer; // Allow ChatServer access if needed (e.g., socket)
private:
    tcp::socket socket_;
    std::shared_ptr<ChatServer> server_;
    net::strand<net::any_io_executor> strand_;
    boost::asio::streambuf read_buffer_;
    std::deque<std::string> write_msgs_;
    bool writing_flag_ = false;
    std::string remote_id_;
    std::string nickname_;
    std::string current_room_;
    std::atomic<bool> stopped_{false};
    bool authenticated_ = false; // SessionInterface requires this

public:
    explicit ChatSession(tcp::socket socket, std::shared_ptr<ChatServer> server);
    ~ChatSession();

    void start();
    
    // SessionInterface implementation
    void stop_session() override;
    void deliver(const std::string& msg) override;
    const std::string& nickname() const override;
    const std::string& remote_id() const override;
    net::strand<net::any_io_executor>& get_strand() override { return strand_; }
    bool is_authenticated() const override { return authenticated_; }
    void set_nickname(const std::string& nick) override { nickname_ = nick; }
    void set_authenticated(bool auth) override { authenticated_ = auth; }
    const std::string& current_room() const override;
    void set_current_room(const std::string& room_name) override;
    std::shared_ptr<SessionInterface> shared_from_this() override {
        return shared_from_this();
    }
    
    // Get shared_ptr to this ChatSession
    std::shared_ptr<ChatSession> shared_from_this_chat() {
        return std::static_pointer_cast<ChatSession>(shared_from_this());
    }

private:
    void process_command(const std::string& command_line);
    void do_read();
    void on_read(beast::error_code ec, std::size_t length);
    void do_write_strand();
    void on_write(std::error_code ec, std::size_t length);
};