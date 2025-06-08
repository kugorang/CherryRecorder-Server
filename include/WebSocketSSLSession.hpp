/**
 * @file WebSocketSSLSession.hpp
 * @brief WSS(WebSocket Secure) 클라이언트와의 보안 채팅 세션을 관리하는 클래스를 정의합니다.
 * @details `WebSocketSession`을 기반으로 하며, Boost.Asio.Ssl을 사용하여
 *          TLS 핸드셰이크 및 암호화된 통신을 추가로 처리합니다.
 */
#pragma once

#include <memory>
#include <string>
#include <queue>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include "SessionInterface.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class ChatServer;

/**
 * @class WebSocketSSLSession
 * @brief 개별 WSS 클라이언트와의 보안 통신을 담당하는 클래스.
 * @details `SessionInterface`를 구현하며, TCP 스트림을 `ssl_stream`으로 감싸 TLS 암호화를 제공합니다.
 *          TLS 핸드셰이크, WebSocket 핸드셰이크, 암호화된 메시지 처리 등 보안 통신의 전체 생명주기를 관리합니다.
 * @see WebSocketSession
 * @see SessionInterface
 * @see ChatServer
 */
class WebSocketSSLSession : public SessionInterface, public std::enable_shared_from_this<WebSocketSSLSession>
{
private:
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_; ///< SSL 스트림으로 강화된 WebSocket 스트림
    beast::flat_buffer buffer_; ///< 메시지 읽기 버퍼
    std::shared_ptr<ChatServer> server_; ///< 세션이 속한 ChatServer에 대한 포인터
    net::strand<net::any_io_executor> strand_; ///< 비동기 핸들러의 직렬 실행을 보장하는 스트랜드
    std::queue<std::shared_ptr<const std::string>> write_msgs_; ///< 전송 대기 중인 메시지 큐
    bool is_writing_ = false; ///< 현재 쓰기 작업 진행 여부 플래그
    std::string nickname_;
    std::string remote_id_;
    std::string current_room_;
    bool authenticated_ = false;
    
    // 보안 설정
    static constexpr std::size_t max_message_size_ = 1024 * 1024; // 1MB 메시지 크기 제한
    static constexpr std::size_t max_queue_size_ = 100; // 최대 큐 크기
    
public:
    /**
     * @brief WebSocketSSLSession 생성자.
     * @param socket 클라이언트와 연결된 TCP 소켓.
     * @param ctx SSL 컨텍스트. 인증서, 비공개 키 등의 정보를 담고 있습니다.
     * @param server 세션이 속한 `ChatServer`의 `shared_ptr`.
     */
    explicit WebSocketSSLSession(tcp::socket&& socket, ssl::context& ctx, std::shared_ptr<ChatServer> server);
    
    // Destructor
    ~WebSocketSSLSession();
    
    /**
     * @brief 세션의 비동기 작업을 시작합니다.
     * @details 여기서는 `on_handshake`를 직접 호출하여 SSL 핸드셰이크부터 시작합니다.
     */
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
            std::enable_shared_from_this<WebSocketSSLSession>::shared_from_this());
    }
    
private:
    /**
     * @brief SSL 핸드셰이크를 시작하는 콜백.
     * @param ec 작업 결과 에러 코드.
     */
    void on_handshake(beast::error_code ec);
    
    /**
     * @brief SSL 핸드셰이크 완료 시 호출되는 콜백.
     * @param ec 작업 결과 에러 코드.
     */
    void on_ssl_handshake(beast::error_code ec);
    
    /**
     * @brief WebSocket 핸드셰이크 수락 완료 시 호출되는 콜백.
     * @param ec 작업 결과 에러 코드.
     */
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