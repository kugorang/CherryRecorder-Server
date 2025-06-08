#include <folly/init/Init.h>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <signal.h>
#include <memory>
#include <thread>
#include <iostream>
#include <boost/asio/io_context.hpp>
#include "ProxygenHttpServer.hpp"
#include "ChatServer.hpp"
#include "../include/WebSocketListener.hpp"

namespace net = boost::asio;

// 명령줄 플래그 정의
DEFINE_int32(http_port, 8080, "Port to listen on for HTTP");
DEFINE_int32(https_port, 58080, "Port to listen on for HTTPS");
DEFINE_string(cert_path, "", "Path to SSL certificate file");
DEFINE_string(key_path, "", "Path to SSL key file");
DEFINE_int32(threads, 0, "Number of threads to use (0 for hardware concurrency)");
DEFINE_int32(ws_port, 33334, "Port for WebSocket chat server");

// 전역 서버 인스턴스 (시그널 핸들러에서 접근)
std::unique_ptr<ProxygenHttpServer> g_proxygen_server;
std::shared_ptr<ChatServer> g_chat_server;

// 시그널 핸들러
void signalHandler(int signum) {
    LOG(INFO) << "Received signal " << signum << ", shutting down...";
    if (g_proxygen_server) {
        g_proxygen_server->stop();
    }
    if (g_chat_server) {
        g_chat_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // ECS 환경에서 epoll 문제 해결을 위한 설정
    // poll 백엔드 사용 강제
    setenv("FOLLY_EVENTBASE_BACKEND", "poll", 1);
    setenv("FOLLY_DISABLE_EPOLL", "1", 1);
    
    // Folly/glog/gflags 초기화
    folly::Init init(&argc, &argv, true);
    
    LOG(INFO) << "CherryRecorder Server v1.1 - WEBSOCKET_FIX_APPLIED (Proxygen)";
    LOG(INFO) << "EventBase backend: " << getenv("FOLLY_EVENTBASE_BACKEND");
    LOG(INFO) << "FOLLY_DISABLE_EPOLL: " << getenv("FOLLY_DISABLE_EPOLL");
    
    LOG(INFO) << "===========================================";
    LOG(INFO) << "CherryRecorder Server (Proxygen Edition)";
    LOG(INFO) << "===========================================";
    
    // 시그널 핸들러 설정
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // 환경 변수 확인
    const char* api_key = std::getenv("GOOGLE_MAPS_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        LOG(WARNING) << "GOOGLE_MAPS_API_KEY environment variable not set!";
        LOG(WARNING) << "Places API features will not work.";
    } else {
        LOG(INFO) << "Google Maps API key loaded successfully";
    }
    
    try {
        int threads = FLAGS_threads > 0 ? FLAGS_threads : std::thread::hardware_concurrency();
        
        // Proxygen HTTP/HTTPS 서버 시작
        LOG(INFO) << "Starting Proxygen HTTP/HTTPS server...";
        g_proxygen_server = std::make_unique<ProxygenHttpServer>(
            FLAGS_http_port, FLAGS_https_port, threads
        );
        g_proxygen_server->start(FLAGS_cert_path, FLAGS_key_path);
        LOG(INFO) << "HTTP server listening on port " << FLAGS_http_port;
        if (!FLAGS_cert_path.empty() && !FLAGS_key_path.empty()) {
            LOG(INFO) << "HTTPS server listening on port " << FLAGS_https_port;
        }
        
        // WebSocket 채팅 서버용 io_context 생성
        net::io_context ioc{threads};
        
        // 히스토리 디렉토리 경로 설정
        const char* history_path_env = std::getenv("HISTORY_DIR");
        std::string history_path = (history_path_env) ? std::string(history_path_env) : "history";
        LOG(INFO) << "Using history path: " << history_path;
        
        // WebSocket 채팅 서버 시작
        LOG(INFO) << "Starting WebSocket chat server...";
        g_chat_server = std::make_shared<ChatServer>(
            ioc, 
            FLAGS_ws_port,
            "chat_server.cfg", // config_file
            history_path       // history_dir
        );
        g_chat_server->run();
        LOG(INFO) << "WebSocket server listening on port " << FLAGS_ws_port;
        
        // WebSocket 리스너 생성 및 시작
        LOG(INFO) << "Creating WebSocket listener...";
        auto ws_listener = std::make_shared<WebSocketListener>(
            ioc,
            net::ip::tcp::endpoint{net::ip::make_address("0.0.0.0"), FLAGS_ws_port},
            g_chat_server
        );
        LOG(INFO) << "Starting WebSocket listener...";
        ws_listener->run();
        
        LOG(INFO) << "===========================================";
        LOG(INFO) << "All servers started successfully!";
        LOG(INFO) << "HTTP:      http://localhost:" << FLAGS_http_port;
        if (!FLAGS_cert_path.empty() && !FLAGS_key_path.empty()) {
            LOG(INFO) << "HTTPS:     https://localhost:" << FLAGS_https_port;
        }
        LOG(INFO) << "WebSocket: ws://localhost:" << FLAGS_ws_port;
        LOG(INFO) << "===========================================";
        LOG(INFO) << "Press Ctrl+C to stop the server";
        
        // io_context 실행을 위한 스레드 풀 생성
        std::vector<std::thread> v;
        v.reserve(threads - 1);
        for(auto i = threads - 1; i > 0; --i) {
            v.emplace_back([&ioc] { ioc.run(); });
        }
        
        // 메인 스레드에서 io_context 실행
        ioc.run();
        
        // 스레드 조인
        for(auto& t : v) {
            if (t.joinable()) {
                t.join();
            }
        }
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "Server error: " << e.what();
        return 1;
    }
    
    return 0;
} 