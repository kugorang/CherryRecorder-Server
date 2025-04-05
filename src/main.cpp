#include "CherryRecorder-Server.hpp" // 기존 Echo Server 헤더
#include "HttpServer.hpp"          // 신규 HTTP Server 헤더 (Beast)
#include <boost/asio/io_context.hpp> // Asio io_context
#include <cstdlib>                 // getenv, exit
#include <iostream>
#include <stdexcept>               // runtime_error
#include <string>
#include <thread>                  // std::thread
#include <vector>
#include <csignal>                 // signal handling
#include <atomic>                  // std::atomic_bool

// --- 환경 변수 처리 함수 ---
unsigned short get_required_port_env_var(const std::string& var_name, unsigned short default_port) {
    const char* value_str = std::getenv(var_name.c_str());
    if (value_str == nullptr) {
        fprintf(stdout, "환경 변수 %s가 설정되지 않았습니다. 기본값 %d를 사용합니다.\n", var_name.c_str(), default_port);
        return default_port;
    }
    try {
        int port_int = std::stoi(value_str);
        if (port_int > 0 && port_int <= 65535) {
            return static_cast<unsigned short>(port_int);
        }
        else {
            throw std::runtime_error("환경 변수 " + var_name + " 값이 유효한 포트 범위(1-65535)가 아닙니다: " + std::string(value_str));
        }
    }
    catch (const std::exception& e) {
        throw std::runtime_error("환경 변수 " + var_name + " 값을 포트로 변환할 수 없습니다: " + std::string(value_str) + " (" + e.what() + ")");
    }
}

std::string get_env_var(const std::string& var_name, const std::string& default_value) {
    const char* value = std::getenv(var_name.c_str());
    if (value == nullptr) {
        fprintf(stdout, "환경 변수 %s가 설정되지 않았습니다. 기본값 %s를 사용합니다.\n", var_name.c_str(), default_value.c_str());
        return default_value;
    }
    return std::string(value);
}

int get_int_env_var(const std::string& var_name, int default_value) {
    const char* value_str = std::getenv(var_name.c_str());
    if (value_str == nullptr) {
        fprintf(stdout, "환경 변수 %s가 설정되지 않았습니다. 기본값 %d를 사용합니다.\n", var_name.c_str(), default_value);
        return default_value;
    }
    try {
        return std::stoi(value_str);
    }
    catch (const std::exception& e) {
        fprintf(stderr, "환경 변수 %s 값을 정수로 변환할 수 없습니다(%s): %s. 기본값 %d 사용.\n", var_name.c_str(), e.what(), value_str, default_value);
        return default_value;
    }
}

// --- 시그널 처리 ---
std::atomic<bool> shutdown_requested(false);
// 서버 객체 포인터 (시그널 핸들러에서 접근 가능하도록 전역 또는 다른 방식으로 관리)
// 주의: 전역 변수 사용 시 스레드 안전성 및 설계 고려 필요
std::unique_ptr<HttpServer> http_server_ptr = nullptr;
boost::asio::io_context* echo_io_context_ptr = nullptr; // Echo 서버 io_context 포인터

void signal_handler(int signum) {
    fprintf(stdout, "\n종료 시그널 (%d) 수신, 서버 종료를 시작합니다...\n", signum);
    shutdown_requested.store(true);

    // 서버 객체에 종료 요청 전달
    if (echo_io_context_ptr && !echo_io_context_ptr->stopped()) {
        echo_io_context_ptr->stop(); // Echo 서버 io_context 중지
    }
    if (http_server_ptr) {
        http_server_ptr->stop(); // HTTP 서버 중지
    }
}

int main(int argc, char* argv[]) {
    // --- Windows 콘솔 UTF-8 설정 ---
#ifdef _WIN32
   // (기존 코드 유지, 필요 시 SetConsoleCP 등 추가)
   // Windows에서 UTF-8 출력을 위한 로케일 설정 등
    try {
        // 콘솔 출력/입력 코드 페이지 설정
        if (!SetConsoleOutputCP(CP_UTF8)) {
            std::cerr << "오류: 콘솔 출력 인코딩 UTF-8 변경 실패!" << std::endl;
        }
        if (!SetConsoleCP(CP_UTF8)) {
            std::cerr << "오류: 콘솔 입력 인코딩 UTF-8 변경 실패!" << std::endl;
        }
        // C++ 표준 라이브러리 로케일 설정
        std::locale::global(std::locale(".UTF-8"));
        std::cout.imbue(std::locale());
        std::cerr.imbue(std::locale());
        std::wcout.imbue(std::locale()); // 와이드 문자용
        std::wcerr.imbue(std::locale());
    }
    catch (const std::runtime_error& e) {
        std::cerr << "경고: C++ 로케일 UTF-8 설정 실패: " << e.what() << std::endl;
    }
#endif
    // --- 콘솔 설정 끝 ---

    // --- 시그널 핸들러 등록 ---
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // --- 설정 값 읽기 (환경 변수 사용) ---
        unsigned short echo_server_port = get_required_port_env_var("ECHO_SERVER_PORT", 33333);
        unsigned short health_check_port = get_required_port_env_var("HEALTH_CHECK_PORT", 8080);
        std::string health_check_ip = get_env_var("HEALTH_CHECK_IP", "0.0.0.0");
        int http_threads = get_int_env_var("HTTP_THREADS", 4); // HTTP 서버 스레드 수
        std::string doc_root = get_env_var("DOC_ROOT", "."); // 정적 파일 루트 (예시)

        // --- TCP Echo 서버 생성 및 시작 (별도 스레드) ---
        boost::asio::io_context echo_io_context;
        echo_io_context_ptr = &echo_io_context; // 시그널 핸들러용 포인터 설정
        EchoServer echo_server(echo_io_context, echo_server_port);
        echo_server.start(); // 비동기 accept 시작

        std::thread echo_thread([&echo_io_context, echo_server_port]() {
            fprintf(stdout, "TCP Echo Server starting on port %d...\n", echo_server_port);
            try {
                echo_io_context.run(); // 이벤트 루프 시작 (블로킹)
            }
            catch (const std::exception& e) {
                fprintf(stderr, "[Error] TCP Echo Server exception: %s\n", e.what());
            }
            fprintf(stdout, "TCP Echo Server IO context stopped.\n");
            });

        // --- HTTP Health Check 서버 생성 및 시작 (HttpServer 내부 스레드 활용) ---
        http_server_ptr = std::make_unique<HttpServer>(health_check_ip, health_check_port, http_threads, doc_root);
        http_server_ptr->run(); // 서버 시작 (내부 스레드에서 io_context.run() 호출)


        // --- 메인 스레드는 종료 대기 또는 다른 작업 수행 ---
        fprintf(stdout, "All servers are running. Press Ctrl+C to exit.\n");
        // 메인 스레드가 바로 종료되지 않도록 대기 로직 추가
        // 예를 들어, echo_thread가 끝날 때까지 기다리거나,
        // 또는 시그널 처리를 기다리는 루프를 유지한다.
        if (echo_thread.joinable()) {
            echo_thread.join(); // Echo 서버 스레드 종료 대기 (run()이 반환될 때까지)
        }
        // HTTP 서버 스레드는 HttpServer::stop()에서 join 처리

        fprintf(stdout, "Main thread finished waiting.\n");
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[오류] 메인 함수 실행 중 문제 발생: %s\n", e.what());
        // 시그널 핸들러에서 이미 종료 요청했을 수 있으므로 추가 종료 로직 필요 시 구현
        if (echo_io_context_ptr && !echo_io_context_ptr->stopped()) echo_io_context_ptr->stop();
        if (http_server_ptr) http_server_ptr->stop();
        return 1; // 오류 시 비정상 종료
    }

    fprintf(stdout, "Server application finished gracefully.\n");
    return 0; // 정상 종료
}