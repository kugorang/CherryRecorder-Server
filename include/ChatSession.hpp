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

// Forward declarations
class ChatServer;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;

class ChatSession : public std::enable_shared_from_this<ChatSession> {
    friend class ChatServer; // Allow ChatServer access if needed (e.g., socket)
private:
    tcp::socket socket_;
    std::shared_ptr<ChatServer> server_;
    using socket_executor_type = decltype(std::declval<tcp::socket&>().get_executor());
    net::strand<socket_executor_type> strand_;
    boost::asio::streambuf read_buffer_;
    std::deque<std::string> write_msgs_;
    bool writing_flag_ = false;
    std::string remote_id_;
    std::string nickname_;
    std::string current_room_;
    std::atomic<bool> stopped_{false};

public:
    explicit ChatSession(tcp::socket socket, std::shared_ptr<ChatServer> server);
    ~ChatSession();

    void start();
    void stop_session();
    void deliver(const std::string& msg);

    const std::string& nickname() const;
    const std::string& remote_id() const;
    const std::string& current_room() const;
    void set_current_room(const std::string& room_name);

    // Allow ChatServer to get the strand for dispatching stop_session
    using strand_type = net::strand<socket_executor_type>;
    strand_type& get_strand() { return strand_; }

private:
    void process_command(const std::string& command_line);
    void do_read();
    void on_read(beast::error_code ec, std::size_t length);
    void do_write_strand();
    void on_write(std::error_code ec, std::size_t length);
};