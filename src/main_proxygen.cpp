#include <folly/init/Init.h>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <signal.h>
#include <memory>
#include <thread>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <boost/asio/io_context.hpp>
#include <event2/event.h>
#include <event2/thread.h>
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
    // libevent 스레드 지원 초기화 (Proxygen 초기화 전에 필수)
    if (evthread_use_pthreads() != 0) {
        LOG(ERROR) << "Failed to initialize libevent threading support";
        return 1;
    }
    
    // libevent 버전 및 메서드 확인
    LOG(INFO) << "libevent version: " << event_get_version();
    
    // 시스템 정보 출력 (ECS 디버깅용)
    LOG(INFO) << "=== System Information ===";
    
    // 커널 버전
    struct utsname unameData;
    if (uname(&unameData) == 0) {
        LOG(INFO) << "Kernel: " << unameData.sysname << " " << unameData.release 
                  << " " << unameData.version;
        LOG(INFO) << "Architecture: " << unameData.machine;
    }
    
    // CPU 정보
    int num_cpus = std::thread::hardware_concurrency();
    LOG(INFO) << "CPU cores: " << num_cpus;
    
    // 메모리 정보
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    if (meminfo.is_open()) {
        while (std::getline(meminfo, line) && 
               (line.find("MemTotal:") == 0 || line.find("MemAvailable:") == 0)) {
            LOG(INFO) << line;
        }
        meminfo.close();
    }
    
    // 파일 디스크립터 제한
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        LOG(INFO) << "File descriptor limits - soft: " << rlim.rlim_cur 
                  << ", hard: " << rlim.rlim_max;
    }
    
    // /dev 디렉토리 확인 (event 메커니즘 관련)
    LOG(INFO) << "Checking /dev for event mechanisms:";
    system("ls -la /dev/ | grep -E '(epoll|poll|select|null|zero|random)' | head -10");
    
    LOG(INFO) << "=========================";
    
    // 사용 가능한 이벤트 메서드 확인
    const char** methods = event_get_supported_methods();
    LOG(INFO) << "Supported event methods:";
    for (int i = 0; methods[i] != nullptr; i++) {
        LOG(INFO) << "  - " << methods[i];
    }
    
    // ECS 환경에서 libevent 초기화 문제 해결을 위한 설정
    // 환경 변수가 이미 설정되어 있으면 그것을 사용
    const char* backend = std::getenv("FOLLY_EVENTBASE_BACKEND");
    if (!backend) {
        // poll을 우선 시도 (ECS EC2에서 더 잘 작동)
        setenv("FOLLY_EVENTBASE_BACKEND", "poll", 1);
    }
    
    // libevent 백엔드 설정 - poll과 select만 활성화
    setenv("EVENT_NOEPOLL", "1", 1);     // epoll 비활성화
    setenv("EVENT_NOKQUEUE", "1", 1);    // kqueue 비활성화
    setenv("EVENT_NODEVPOLL", "1", 1);   // devpoll 비활성화
    setenv("EVENT_NOEVPORT", "1", 1);    // evport 비활성화
    setenv("EVENT_NOPOLL", "0", 1);      // poll 활성화
    setenv("EVENT_NOSELECT", "0", 1);    // select 활성화
    
    // libevent 디버깅 활성화
    setenv("EVENT_DEBUG_MODE", "1", 1);
    setenv("EVENT_SHOW_METHOD", "1", 1);
    
    // Folly 추가 설정
    setenv("FOLLY_DISABLE_EPOLL", "1", 1);
    setenv("FOLLY_USE_EPOLL", "0", 1);
    
    // 테스트용 event_base 생성 (명시적 설정 사용)
    struct event_config* cfg = event_config_new();
    if (cfg) {
        // epoll 메서드 명시적 비활성화
        event_config_avoid_method(cfg, "epoll");
        event_config_avoid_method(cfg, "epollsig");
        
        // poll과 select만 사용하도록 설정
        event_config_require_features(cfg, 0); // 특별한 기능 요구사항 없음
        
        struct event_base* test_base = event_base_new_with_config(cfg);
        if (test_base) {
            const char* method = event_base_get_method(test_base);
            LOG(INFO) << "Test event_base using method: " << (method ? method : "unknown");
            event_base_free(test_base);
        } else {
            LOG(ERROR) << "Failed to create test event_base with config!";
            // 설정 없이 재시도
            test_base = event_base_new();
            if (test_base) {
                const char* method = event_base_get_method(test_base);
                LOG(INFO) << "Default event_base using method: " << (method ? method : "unknown");
                event_base_free(test_base);
            } else {
                LOG(FATAL) << "Cannot create any event_base! Check system configuration.";
                return 1;
            }
        }
        event_config_free(cfg);
    }
    
    // Folly/glog/gflags 초기화
    folly::Init init(&argc, &argv, true);
    
    LOG(INFO) << "CherryRecorder Server v1.4 - ECS_EC2_LIBEVENT_CUSTOM (Proxygen)";
    LOG(INFO) << "EventBase backend: " << (getenv("FOLLY_EVENTBASE_BACKEND") ? getenv("FOLLY_EVENTBASE_BACKEND") : "default");
    LOG(INFO) << "FOLLY_DISABLE_EPOLL: " << (getenv("FOLLY_DISABLE_EPOLL") ? getenv("FOLLY_DISABLE_EPOLL") : "not set");
    LOG(INFO) << "EVENT_NOEPOLL: " << (getenv("EVENT_NOEPOLL") ? getenv("EVENT_NOEPOLL") : "not set");
    LOG(INFO) << "EVENT_NOPOLL: " << (getenv("EVENT_NOPOLL") ? getenv("EVENT_NOPOLL") : "not set");
    LOG(INFO) << "EVENT_NOSELECT: " << (getenv("EVENT_NOSELECT") ? getenv("EVENT_NOSELECT") : "not set");
    
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