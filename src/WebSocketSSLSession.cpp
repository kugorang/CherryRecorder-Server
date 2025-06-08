#include "WebSocketSSLSession.hpp"
#include "ChatServer.hpp"
#include <spdlog/spdlog.h>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

WebSocketSSLSession::WebSocketSSLSession(tcp::socket&& socket, ssl::context& ctx, std::shared_ptr<ChatServer> server)
    : ws_(std::move(socket), ctx)
    , server_(server)
    , strand_(net::make_strand(beast::get_lowest_layer(ws_).get_executor()))
{
    // 원격 엔드포인트 정보 저장
    try {
        auto remote_endpoint = beast::get_lowest_layer(ws_).socket().remote_endpoint();
        remote_id_ = remote_endpoint.address().to_string() + ":" + 
                     std::to_string(remote_endpoint.port());
        nickname_ = remote_id_; // 초기 닉네임은 remote_id로 설정
    } catch (const std::exception& e) {
        spdlog::error("[WebSocketSSLSession] Failed to get remote endpoint: {}", e.what());
        remote_id_ = "unknown";
        nickname_ = "unknown"; // 초기 닉네임도 설정
    }
}

WebSocketSSLSession::~WebSocketSSLSession()
{
    spdlog::info("[WebSocketSSLSession {}] Session destroyed", remote_id_);
}

void WebSocketSSLSession::run()
{
    // SSL 핸드셰이크를 수행
    auto self = std::enable_shared_from_this<WebSocketSSLSession>::shared_from_this();
    on_handshake(beast::error_code{});
}

void WebSocketSSLSession::on_handshake(beast::error_code ec)
{
    if (ec) {
        spdlog::error("[WebSocketSSLSession {}] SSL handshake failed: {}", remote_id_, ec.message());
        return;
    }
    
    // SSL 핸드셰이크 시작
    beast::get_lowest_layer(ws_).expires_never();
    auto self = std::enable_shared_from_this<WebSocketSSLSession>::shared_from_this();
    ws_.next_layer().async_handshake(
        ssl::stream_base::server,
        beast::bind_front_handler(
            &WebSocketSSLSession::on_ssl_handshake,
            self));
}

void WebSocketSSLSession::on_ssl_handshake(beast::error_code ec)
{
    if (ec) {
        spdlog::error("[WebSocketSSLSession {}] SSL handshake failed: {}", remote_id_, ec.message());
        return;
    }
    
    spdlog::info("[WebSocketSSLSession {}] SSL handshake completed", remote_id_);
    
    // WebSocket 핸드셰이크 수락
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(beast::http::field::server, "CherryRecorder/1.0 (WSS)");
        }));
    ws_.read_message_max(max_message_size_);
    
    auto self = std::enable_shared_from_this<WebSocketSSLSession>::shared_from_this();
    ws_.async_accept(
        beast::bind_front_handler(
            &WebSocketSSLSession::on_accept,
            self));
}

void WebSocketSSLSession::on_accept(beast::error_code ec)
{
    if (ec) {
        spdlog::error("[WebSocketSSLSession {}] Accept failed: {}", remote_id_, ec.message());
        return;
    }
    
    spdlog::info("[WebSocketSSLSession {}] WebSocket connection accepted (SSL)", remote_id_);
    
    // 서버에 세션 등록
    if (server_) {
        server_->join(shared_from_this());
    }
    
    // 환영 메시지 전송
    deliver("* CherryRecorder 채팅 서버에 연결되었습니다. (보안 연결)\r\n");
    deliver("* /nick <닉네임> - 닉네임 변경\r\n");
    deliver("* /pm <닉네임> <메시지> - 개인 메시지\r\n");
    deliver("* /list - 접속자 목록\r\n");
    
    // 메시지 읽기 시작
    do_read();
}

void WebSocketSSLSession::do_read()
{
    auto self = std::enable_shared_from_this<WebSocketSSLSession>::shared_from_this();
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &WebSocketSSLSession::on_read,
            self));
}

void WebSocketSSLSession::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);
    
    if (ec == websocket::error::closed) {
        // 정상적인 연결 종료
        spdlog::info("[WebSocketSSLSession {}] Connection closed", remote_id_);
        stop_session();
        return;
    }
    
    if (ec) {
        spdlog::error("[WebSocketSSLSession {}] Read failed: {}", remote_id_, ec.message());
        stop_session();
        return;
    }
    
    // 메시지 처리
    std::string message = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    
    // 메시지 길이 확인
    if (message.size() > max_message_size_) {
        deliver("* 오류: 메시지가 너무 깁니다.\r\n");
        spdlog::warn("[WebSocketSSLSession {}] Message too long: {} bytes", remote_id_, message.size());
        do_read();
        return;
    }
    
    // 메시지 처리
    process_message(message);
    
    // 다음 메시지 읽기
    do_read();
}

void WebSocketSSLSession::process_message(const std::string& message)
{
    // 빈 메시지 무시
    if (message.empty()) {
        return;
    }
    
    // 개행 문자 제거
    std::string msg = message;
    msg.erase(std::remove(msg.begin(), msg.end(), '\r'), msg.end());
    msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
    
    spdlog::debug("[WebSocketSSLSession {}] Received: {}", remote_id_, msg);
    
    // 명령어 처리
    if (msg.find("/nick ") == 0) {
        std::string new_nick = msg.substr(6);
        if (!new_nick.empty()) {
            // change_nickname이 없으므로 try_register_nickname_async 사용
            server_->try_register_nickname_async(new_nick, shared_from_this(), 
                [this, new_nick](bool success) {
                    if (success) {
                        std::string old_nick = nickname_;
                        nickname_ = new_nick;
                        deliver("* 닉네임이 '" + new_nick + "'으로 변경되었습니다.\r\n");
                        
                        // Check if this is the first nickname change (from IP:PORT to actual nickname)
                        bool is_first_nickname = (old_nick == remote_id_);
                        
                        // 다른 사용자들에게 알림
                        if (server_) {
                            if (is_first_nickname) {
                                // Broadcast join message for first-time nickname setting
                                std::string join_msg = "* 사용자 '" + new_nick + "'님이 입장했습니다.\r\n";
                                server_->broadcast(join_msg, shared_from_this());
                            } else {
                                // Broadcast nickname change message
                                std::string notify_msg = "* '" + old_nick + "'님이 '" + new_nick + "'으로 닉네임을 변경했습니다.\r\n";
                                server_->broadcast(notify_msg, shared_from_this());
                            }
                        }
                    } else {
                        deliver("* 닉네임 변경 실패: 이미 사용 중이거나 유효하지 않습니다.\r\n");
                    }
                });
        }
    }
    else if (msg.find("/pm ") == 0) {
        size_t first_space = msg.find(' ', 4);
        if (first_space != std::string::npos) {
            std::string target_nick = msg.substr(4, first_space - 4);
            std::string pm_msg = msg.substr(first_space + 1);
            server_->send_private_message(pm_msg, shared_from_this(), target_nick);
        }
    }
    else if (msg == "/list") {
        // list_users가 없으므로 get_user_list_async 사용
        server_->get_user_list_async([this](std::vector<std::string> users) {
            std::string user_list = "* 현재 접속자 목록:\r\n";
            for (const auto& user : users) {
                user_list += "  - " + user + "\r\n";
            }
            deliver(user_list);
        });
    }
    else if (msg.find("/auth ") == 0) {
        // 간단한 인증 처리 (실제 구현 시에는 보안 강화 필요)
        size_t space_pos = msg.find(' ', 6);
        if (space_pos != std::string::npos) {
            std::string username = msg.substr(6, space_pos - 6);
            std::string password = msg.substr(space_pos + 1);
            handle_auth(username, password);
        }
    }
    else {
        // 일반 메시지
        if (server_) {
            // 현재 방에 있으면 방으로, 아니면 전체 브로드캐스트
            if (!current_room_.empty()) {
                server_->broadcast_to_room(current_room_, "[" + nickname_ + "] " + msg + "\r\n", shared_from_this());
            } else {
                server_->broadcast("[" + nickname_ + "] " + msg + "\r\n", shared_from_this());
            }
        }
    }
}

void WebSocketSSLSession::handle_auth(const std::string& username, const std::string& password)
{
    // 실제 구현에서는 데이터베이스 조회 등이 필요
    if (username == "admin" && password == "password") {
        authenticated_ = true;
        deliver("* 인증 성공\r\n");
        spdlog::info("[WebSocketSSLSession {}] Authentication successful: {}", remote_id_, username);
    } else {
        deliver("* 인증 실패\r\n");
        spdlog::warn("[WebSocketSSLSession {}] Authentication failed: {}", remote_id_, username);
    }
}

void WebSocketSSLSession::deliver(const std::string& msg)
{
    auto self = shared_from_this();
    net::post(strand_,
        [this, self, msg]() {
            // 큐 크기 제한 확인
            if (write_msgs_.size() >= max_queue_size_) {
                spdlog::warn("[WebSocketSSLSession {}] Message queue full, dropping message", remote_id_);
                return;
            }
            
            bool write_in_progress = !write_msgs_.empty();
            write_msgs_.push(std::make_shared<const std::string>(msg));
            if (!write_in_progress && !is_writing_) {
                do_write();
            }
        });
}

void WebSocketSSLSession::do_write()
{
    is_writing_ = true;
    auto self = std::enable_shared_from_this<WebSocketSSLSession>::shared_from_this();
    ws_.async_write(
        net::buffer(*write_msgs_.front()),
        beast::bind_front_handler(
            &WebSocketSSLSession::on_write,
            self));
}

void WebSocketSSLSession::on_write(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);
    
    if (ec) {
        spdlog::error("[WebSocketSSLSession {}] Write failed: {}", remote_id_, ec.message());
        is_writing_ = false;
        stop_session();
        return;
    }
    
    write_msgs_.pop();
    is_writing_ = false;
    
    if (!write_msgs_.empty()) {
        do_write();
    }
}

void WebSocketSSLSession::stop_session()
{
    if (server_) {
        server_->leave(shared_from_this());
    }
    
    // WebSocket 연결 종료
    beast::error_code ec;
    ws_.close(websocket::close_code::normal, ec);
    if (ec) {
        spdlog::error("[WebSocketSSLSession {}] Close failed: {}", remote_id_, ec.message());
    }
} 