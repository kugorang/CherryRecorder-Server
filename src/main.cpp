#include "HttpServer.hpp"          // HTTP Server 헤더 (Beast)
#include <boost/asio/io_context.hpp> // Asio io_context
#include <cstdlib>                 // getenv, exit, stoi (C++11)
#include <iostream>
#include <stdexcept>               // runtime_error
#include <string>
#include <thread>                  // std::thread
#include <vector>
#include <csignal>                 // signal, SIGINT, SIGTERM
#include <atomic>                  // std::atomic_bool
#include <memory>                  // std::unique_ptr
#include <system_error>            // std::system_error (예외 처리)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // SetConsoleOutputCP, SetConsoleCP
#include <locale>    // std::locale::global
#endif

// --- 추가된 include ---
#include "../include/CherryRecorder-Server.hpp" // EchoServer 추가
#include "../include/ChatServer.hpp"         // ChatServer 추가


/**
 * @file main.cpp
 * @brief CherryRecorder 서버 애플리케이션의 메인 진입점 파일.
 *
 * 환경 변수에서 설정을 읽어 HTTP 서버, Echo 서버, Chat 서버를 생성하고 실행한다.
 * POSIX 시그널(SIGINT, SIGTERM)을 처리하여 서버의 정상 종료(graceful shutdown)를 지원한다.
 * Windows 환경에서는 콘솔 입출력 인코딩을 UTF-8로 설정하려고 시도한다.
 */

 // --- 환경 변수 처리 헬퍼 함수 ---

 /**
  * @brief 환경 변수에서 포트 번호를 읽어온다. 없으면 기본값을 사용한다.
  * @param var_name 읽어올 환경 변수 이름.
  * @param default_port 환경 변수가 없거나 유효하지 않을 경우 사용할 기본 포트 번호.
  * @return 읽어온 포트 번호 (unsigned short).
  * @throw std::runtime_error 환경 변수 값이 있으나 유효한 포트 범위(1-65535)가 아니거나 숫자로 변환 불가능한 경우.
  */
unsigned short get_required_port_env_var(const std::string& var_name, unsigned short default_port) {
    const char* value_str = std::getenv(var_name.c_str());
    if (value_str == nullptr) {
        fprintf(stdout, "Environment variable '%s' not set. Using default value: %hu\n", var_name.c_str(), default_port);
        return default_port;
    }
    try {
        int port_int = std::stoi(value_str); // C++11 stoi 사용
        if (port_int > 0 && port_int <= 65535) {
            fprintf(stdout, "Read environment variable '%s': %d\n", var_name.c_str(), port_int);
            return static_cast<unsigned short>(port_int);
        }
        else {
            // 오류 메시지 개선
            char msg[256];
            snprintf(msg, sizeof(msg), "Environment variable '%s' value '%s' is out of valid port range (1-65535).", var_name.c_str(), value_str);
            throw std::runtime_error(msg);
        }
    }
    catch (const std::invalid_argument& ia) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to convert environment variable '%s' value '%s' to integer: %s", var_name.c_str(), value_str, ia.what());
        throw std::runtime_error(msg);
    }
    catch (const std::out_of_range& oor) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Environment variable '%s' value '%s' is out of integer range: %s", var_name.c_str(), value_str, oor.what());
        throw std::runtime_error(msg);
    }
    // catch (const std::exception& e) 와 동일한 효과지만 더 명시적
}

/**
 * @brief 환경 변수에서 문자열 값을 읽어온다. 없으면 기본값을 사용한다.
 * @param var_name 읽어올 환경 변수 이름.
 * @param default_value 환경 변수가 없을 경우 사용할 기본 문자열 값.
 * @return 읽어온 문자열 값.
 */
std::string get_env_var(const std::string& var_name, const std::string& default_value) {
    const char* value = std::getenv(var_name.c_str());
    if (value == nullptr) {
        fprintf(stdout, "Environment variable '%s' not set. Using default value: '%s'\n", var_name.c_str(), default_value.c_str());
        return default_value;
    }
    fprintf(stdout, "Read environment variable '%s': '%s'\n", var_name.c_str(), value);
    return std::string(value);
}

/**
 * @brief 환경 변수에서 정수 값을 읽어온다. 없거나 유효하지 않으면 기본값을 사용한다.
 * @param var_name 읽어올 환경 변수 이름.
 * @param default_value 환경 변수가 없거나 정수로 변환 불가능할 경우 사용할 기본 정수 값.
 * @return 읽어온 정수 값.
 */
int get_int_env_var(const std::string& var_name, int default_value) {
    const char* value_str = std::getenv(var_name.c_str());
    if (value_str == nullptr) {
        fprintf(stdout, "Environment variable '%s' not set. Using default value: %d\n", var_name.c_str(), default_value);
        return default_value;
    }
    try {
        int value_int = std::stoi(value_str);
        fprintf(stdout, "Read environment variable '%s': %d\n", var_name.c_str(), value_int);
        return value_int;
    }
    catch (const std::exception& e) { // stoi 실패 (invalid_argument, out_of_range)
        fprintf(stderr, "Warning: Failed to convert environment variable '%s' value '%s' to integer (%s). Using default value: %d\n",
            var_name.c_str(), value_str, e.what(), default_value);
        return default_value;
    }
}

// --- 시그널 처리 관련 전역 변수 및 함수 ---
std::atomic<bool> shutdown_requested(false); ///< @brief 서버 종료 요청 플래그 (원자적 접근 보장).
// 서버 객체 포인터 (시그널 핸들러에서 접근)
// TODO: 전역 변수 대신 더 나은 의존성 주입 또는 관리 방식 고려
std::unique_ptr<HttpServer> g_http_server_ptr = nullptr; ///< @brief HTTP 서버 객체 포인터 (전역).
std::unique_ptr<EchoServer> g_echo_server_ptr = nullptr; ///< @brief Echo 서버 객체 포인터 (전역).
std::unique_ptr<ChatServer> g_chat_server_ptr = nullptr; ///< @brief Chat 서버 객체 포인터 (전역).

/**
 * @brief SIGINT, SIGTERM 시그널을 처리하는 핸들러 함수.
 * @param signum 수신된 시그널 번호.
 *
 * `shutdown_requested` 플래그를 true로 설정하고, 각 서버의 종료 메서드(`stop()`)를 호출한다.
 * @note 시그널 핸들러 내에서는 안전하지 않은 함수(printf, malloc 등) 호출을 최소화해야 함.
 */
void signal_handler(int signum) {
    static std::atomic<bool> handling_signal(false);
    if (handling_signal.exchange(true)) {
        return;
    }

    fprintf(stdout, "\nShutdown signal (%d) received. Initiating graceful shutdown...\n", signum);
    shutdown_requested.store(true); // 종료 플래그 설정

    // 각 서버의 stop() 메서드 호출 (null 체크 포함)
    if (g_http_server_ptr) {
        fprintf(stdout, "Requesting HTTP server stop...\n");
        g_http_server_ptr->stop(); // 자체 io_context/스레드 관리 가정
    }
    if (g_echo_server_ptr) {
        fprintf(stdout, "Requesting Echo server stop...\n");
        g_echo_server_ptr->stop(); // acceptor만 닫음 (io_context는 공유 가정)
    }
    if (g_chat_server_ptr) {
        fprintf(stdout, "Requesting Chat server stop...\n");
        g_chat_server_ptr->stop(); // 자체 io_context/스레드 관리 가정
    }

    fprintf(stdout, "Signal handler finished processing. Main thread should exit soon.\n");
    // handling_signal.store(false); // 일반적으로 필요 없음
}

/**
 * @brief 애플리케이션 메인 함수.
 * @param argc 커맨드 라인 인자 개수.
 * @param argv 커맨드 라인 인자 배열.
 * @return 성공 시 0, 오류 시 1.
 */
int main(int argc, char* argv[]) {
    // --- Windows 콘솔 UTF-8 설정 (프로그램 시작 시) ---
#ifdef _WIN32
    try {
        // 콘솔 출력 코드 페이지를 UTF-8로 설정
        if (!SetConsoleOutputCP(CP_UTF8)) {
            std::cerr << "Warning: Failed to set console output codepage to UTF-8. Error code: " << GetLastError() << std::endl;
        }
        // 콘솔 입력 코드 페이지를 UTF-8로 설정
        if (!SetConsoleCP(CP_UTF8)) {
            std::cerr << "Warning: Failed to set console input codepage to UTF-8. Error code: " << GetLastError() << std::endl;
        }
        // C/C++ 표준 라이브러리 로케일을 UTF-8로 설정 (printf 등에도 영향)
        // 시스템에 ".UTF-8" 로케일이 설치되어 있어야 함
        // setlocale(LC_ALL, ".UTF-8"); // C 스타일
        std::locale::global(std::locale(".UTF-8")); ///< C++ 스타일 (iostream에 영향)
        std::cout.imbue(std::locale());
        std::cerr.imbue(std::locale());
        std::wcout.imbue(std::locale()); ///< 와이드 문자용
        std::wcerr.imbue(std::locale());
        fprintf(stdout, "Windows console UTF-8 settings applied.\n");
    }
    catch (const std::runtime_error& e) {
        // 로케일 설정 실패 시 경고 출력 (프로그램 중단은 아님)
        std::cerr << "Warning: Failed to set C++ global locale to .UTF-8: " << e.what() << std::endl;
    }
#endif
    // --- 콘솔 설정 끝 ---

    fprintf(stdout, "Running in RELEASE mode\n");

    // --- 시그널 핸들러 등록 ---
    // SIGINT (Ctrl+C) 와 SIGTERM (kill 등) 처리
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    fprintf(stdout, "Signal handlers for SIGINT and SIGTERM registered.\n");

    try {
        // --- 설정 값 읽기 (환경 변수 사용) ---
        unsigned short http_port = get_required_port_env_var("HTTP_PORT", 8080);
        std::string http_bind_ip = get_env_var("HTTP_BIND_IP", "0.0.0.0");
        int http_threads = get_int_env_var("HTTP_THREADS", 1); // HTTP 서버 자체 스레드는 1개로 충분할 수 있음
        unsigned short chat_port = get_required_port_env_var("CHAT_SERVER_PORT", 33334);
        int chat_threads = get_int_env_var("CHAT_THREADS", 1); // Chat 서버 자체 스레드 (io_context 공유 시 불필요)
        unsigned short echo_port = get_required_port_env_var("ECHO_SERVER_PORT", 33333);
        // int echo_threads = get_int_env_var("ECHO_THREADS", 1); // Echo 서버는 보통 스레드 설정 불필요

        // --- io_context 생성 --- 
        // 모든 서버가 io_context를 공유하도록 변경 (더 효율적일 수 있음)
        const int num_total_threads = 4; // 예시: 전체 서버용 스레드 수
        net::io_context ioc{num_total_threads};
        std::vector<std::thread> io_threads;
        io_threads.reserve(num_total_threads);

        // --- 서버 객체 생성 (공유 io_context 사용) ---
        g_http_server_ptr = std::make_unique<HttpServer>(http_bind_ip, http_port, http_threads); // HttpServer는 자체 스레드 관리
        g_echo_server_ptr = std::make_unique<EchoServer>(ioc, echo_port); // 공유 io_context 전달
        g_chat_server_ptr = std::make_unique<ChatServer>(ioc, chat_port); // 수정: io_context와 포트만 전달
        
        // --- 서버 시작 ---
        g_http_server_ptr->run(); // 자체 스레드 시작
        g_echo_server_ptr->start(); // 공유 io_context에 accept 작업 게시
        g_chat_server_ptr->run(); // 자체 스레드 시작

        fprintf(stdout, "HTTP server starting on %s:%hu (%d threads)\n", http_bind_ip.c_str(), http_port, http_threads);
        fprintf(stdout, "Echo server starting on port %hu (using shared io_context)\n", echo_port);
        fprintf(stdout, "Chat server starting on port %hu (%d threads)\n", chat_port, chat_threads);

        // --- 공유 io_context 실행 스레드 시작 ---
        fprintf(stdout, "Starting %d shared IO threads for Echo Server...\n", num_total_threads);
        for (int i = 0; i < num_total_threads; ++i) {
            io_threads.emplace_back([&ioc, i]() { // ioc 참조 캡처
                fprintf(stdout, "[Shared IO Thread %d] Starting io_context::run()...\n", i);
                try {
                    ioc.run(); // 이벤트 루프 실행
                    fprintf(stdout, "[Shared IO Thread %d] io_context::run() finished.\n", i);
                } catch (const std::exception& e) {
                    fprintf(stderr, "[Shared IO Thread %d] Exception: %s\n", i, e.what());
                }
            });
        }

        fprintf(stdout, "All servers running. Press Ctrl+C or send SIGTERM to exit.\n");

        // --- 메인 스레드는 종료 시그널 대기 또는 다른 작업 수행 --- 
        // 시그널 핸들러가 stop()을 호출하고 io_context를 중지시킬 것임
        // 모든 스레드가 종료될 때까지 대기
        for(auto& t : io_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        // HttpServer, ChatServer의 자체 스레드 join 로직은 각 stop() 메서드 내부에 있어야 함

        fprintf(stdout, "Main thread exiting after IO threads finished.\n");

    }
    catch (const std::system_error& e) { ///< Asio 관련 시스템 오류 등
        fprintf(stderr, "[Error] System error during server setup or execution: %s (code: %d)\n", e.what(), e.code().value());
        // 시그널 핸들러와 유사하게 종료 처리 시도
        if (!shutdown_requested.load()) { ///< 시그널 핸들러가 이미 호출되지 않았다면
            signal_handler(0); ///< 가상 시그널 번호로 핸들러 호출하여 정리 시도
        }
        return 1;
    }
    catch (const std::runtime_error& e) { ///< 환경 변수 파싱 오류 등
        fprintf(stderr, "[Error] Runtime error during server setup: %s\n", e.what());
        // 위와 유사하게 종료 처리 시도
        if (!shutdown_requested.load()) {
            signal_handler(0);
        }
        return 1;
    }
    catch (const std::exception& e) { ///< 그 외 표준 라이브러리 예외
        fprintf(stderr, "[Error] Unhandled standard exception in main: %s\n", e.what());
        if (!shutdown_requested.load()) {
            signal_handler(0);
        }
        return 1;
    }
    catch (...) { ///< 알 수 없는 예외
        fprintf(stderr, "[Error] Unknown exception caught in main!\n");
        if (!shutdown_requested.load()) {
            signal_handler(0);
        }
        return 1;
    }

    // 전역 포인터 정리 (선택적, 프로그램 종료 시 자동 해제되나 명시적 정리)
    g_http_server_ptr.reset();
    g_echo_server_ptr.reset();
    g_chat_server_ptr.reset();

    fprintf(stdout, "Server application finished gracefully.\n");
    return 0; ///< 정상 종료
}