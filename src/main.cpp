#include "CherryRecorder-Server.hpp" // 기존 Echo Server 헤더
#include "HttpServer.hpp"          // 신규 HTTP Server 헤더 (Beast)
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


/**
 * @file main.cpp
 * @brief CherryRecorder 서버 애플리케이션의 메인 진입점 파일.
 *
 * 환경 변수에서 설정을 읽어 TCP 에코 서버(`EchoServer`)와 HTTP 헬스 체크 서버(`HttpServer`)를
 * 각각 생성하고 실행한다.
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
// 서버 객체 포인터 (시그널 핸들러에서 접근 가능해야 함)
// 주의: 전역 변수는 설계상 단점이 있을 수 있으므로 신중히 사용.
//       대안: 싱글턴, 클래스 멤버 변수화 등.
std::unique_ptr<HttpServer> g_http_server_ptr = nullptr; ///< @brief HTTP 서버 객체 포인터 (전역).
boost::asio::io_context* g_echo_io_context_ptr = nullptr; ///< @brief Echo 서버의 io_context 포인터 (전역).

/**
 * @brief SIGINT, SIGTERM 시그널을 처리하는 핸들러 함수.
 * @param signum 수신된 시그널 번호.
 *
 * `shutdown_requested` 플래그를 true로 설정하고, 각 서버의 종료 메서드(`stop()`)를 호출한다.
 * Echo 서버는 io_context를 중지시켜 종료를 유도한다.
 * @note 시그널 핸들러 내에서는 안전하지 않은 함수(printf, malloc 등) 호출을 최소화해야 하나,
 * 여기서는 로깅 및 서버 종료 로직 단순화를 위해 fprintf 사용. 더 견고한 구현에서는 async-safe 함수 사용 고려.
 */
void signal_handler(int signum) {
    // 핸들러 재진입 방지 (선택적)
    static std::atomic<bool> handling_signal(false);
    if (handling_signal.exchange(true)) {
        return; // 이미 다른 시그널 처리 중이면 반환
    }

    fprintf(stdout, "\nShutdown signal (%d) received. Initiating graceful shutdown...\n", signum);
    shutdown_requested.store(true); // 종료 플래그 설정

    // 1. Echo 서버 io_context 중지 요청 (실행 중인 run() 반환 유도)
    // 포인터가 유효하고, io_context가 아직 중지되지 않았는지 확인
    if (g_echo_io_context_ptr && !g_echo_io_context_ptr->stopped()) {
        fprintf(stdout, "Requesting Echo server io_context stop...\n");
        g_echo_io_context_ptr->stop();
    }
    else {
        fprintf(stdout, "Echo server io_context already stopped or not initialized.\n");
    }

    // 2. HTTP 서버 중지 요청 (HttpServer::stop() 호출)
    // 포인터가 유효한지 확인
    if (g_http_server_ptr) {
        fprintf(stdout, "Requesting HTTP server stop...\n");
        // stop() 메서드는 내부적으로 io_context 중지 및 스레드 join 수행
        g_http_server_ptr->stop();
    }
    else {
        fprintf(stdout, "HTTP server not initialized.\n");
    }
    fprintf(stdout, "Signal handler finished processing.\n");
    // handling_signal.store(false); // 필요 시 플래그 리셋
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
        std::locale::global(std::locale(".UTF-8")); // C++ 스타일 (iostream에 영향)
        std::cout.imbue(std::locale());
        std::cerr.imbue(std::locale());
        std::wcout.imbue(std::locale()); // 와이드 문자용
        std::wcerr.imbue(std::locale());
        fprintf(stdout, "Windows console UTF-8 settings applied.\n");
    }
    catch (const std::runtime_error& e) {
        // 로케일 설정 실패 시 경고 출력 (프로그램 중단은 아님)
        std::cerr << "Warning: Failed to set C++ global locale to .UTF-8: " << e.what() << std::endl;
    }
#endif
    // --- 콘솔 설정 끝 ---

    // --- 시그널 핸들러 등록 ---
    // SIGINT (Ctrl+C) 와 SIGTERM (kill 등) 처리
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    fprintf(stdout, "Signal handlers for SIGINT and SIGTERM registered.\n");

    std::thread echo_thread; // Echo 서버 실행 스레드

    try {
        // --- 설정 값 읽기 (환경 변수 사용) ---
        unsigned short echo_server_port = get_required_port_env_var("ECHO_SERVER_PORT", 33333);
        unsigned short health_check_port = get_required_port_env_var("HEALTH_CHECK_PORT", 8080);
        std::string health_check_ip = get_env_var("HEALTH_CHECK_IP", "0.0.0.0"); // 모든 인터페이스에서 수신
        int http_threads = get_int_env_var("HTTP_THREADS", 4); // HTTP 서버 IO 스레드 수
        std::string doc_root = get_env_var("DOC_ROOT", "."); // 정적 파일 루트 (현재 HTTP 서버에서는 사용 안 함)

        // --- TCP Echo 서버 생성 및 시작 (별도 스레드) ---
        boost::asio::io_context echo_io_context; // Echo 서버 전용 io_context
        g_echo_io_context_ptr = &echo_io_context; // 시그널 핸들러에서 접근 가능하도록 전역 포인터 설정

        // EchoServer 객체 생성 (스택 또는 힙)
        // 여기서는 스택에 생성하고, io_context.run()이 반환된 후 소멸되도록 함
        EchoServer echo_server(echo_io_context, echo_server_port);
        echo_server.start(); // 비동기 accept 시작 (io_context.run() 필요)

        // Echo 서버의 io_context를 실행할 별도 스레드 생성
        echo_thread = std::thread([&echo_io_context, echo_server_port]() {
            fprintf(stdout, "TCP Echo Server thread started. Running io_context on port %d...\n", echo_server_port);
            try {
                // 이 스레드는 echo_io_context 이벤트 루프를 실행 (블로킹)
                // echo_io_context.stop()이 호출되거나 모든 작업이 완료되면 반환됨
                echo_io_context.run();
            }
            catch (const std::exception& e) {
                // echo_io_context.run() 내부에서 발생한 예외 처리
                fprintf(stderr, "[Error] TCP Echo Server thread exception: %s\n", e.what());
                // 필요 시 전역 에러 플래그 설정 또는 메인 스레드에 알림
                // 메인 스레드 종료 유도
                // raise(SIGTERM); // 또는 다른 메커니즘 사용
            }
            fprintf(stdout, "TCP Echo Server thread finished (io_context stopped).\n");
            });


        // --- HTTP Health Check 서버 생성 및 시작 (HttpServer 내부 스레드 활용) ---
        // unique_ptr 사용으로 자동 메모리 관리 및 시그널 핸들러 접근 제공
        g_http_server_ptr = std::make_unique<HttpServer>(health_check_ip, health_check_port, http_threads, doc_root);
        g_http_server_ptr->run(); // 서버 시작 (내부적으로 스레드 풀 생성 및 io_context.run() 호출)


        // --- 메인 스레드는 종료 시그널 대기 ---
        fprintf(stdout, "Both Echo server and HTTP server are running. Press Ctrl+C or send SIGTERM to exit.\n");

        // 메인 스레드는 Echo 서버 스레드가 종료될 때까지 대기.
        // HTTP 서버 스레드는 HttpServer::stop() 내부에서 join 처리됨.
        // 시그널 핸들러가 호출되면 echo_io_context.stop()이 실행되고,
        // echo_thread의 io_context.run()이 반환되어 아래 join()이 완료됨.
        if (echo_thread.joinable()) {
            fprintf(stdout, "Main thread is waiting for Echo server thread to join...\n");
            echo_thread.join(); // Echo 서버 스레드 종료 대기
            fprintf(stdout, "Echo server thread joined.\n");
        }
        else {
            fprintf(stderr, "Warning: Echo server thread was not joinable.\n");
        }

        // HTTP 서버의 stop()은 시그널 핸들러에서 호출되었으므로, 여기서 별도 호출 불필요.
        // 단, 시그널 수신 없이 정상 종료하는 경로가 있다면 여기서 g_http_server_ptr->stop() 호출 필요.

        fprintf(stdout, "Main thread finished waiting for server threads.\n");

    }
    catch (const std::system_error& e) { // Asio 관련 시스템 오류 등
        fprintf(stderr, "[Error] System error during server setup or execution: %s (code: %d)\n", e.what(), e.code().value());
        // 시그널 핸들러와 유사하게 종료 처리 시도
        if (!shutdown_requested.load()) { // 시그널 핸들러가 이미 호출되지 않았다면
            signal_handler(0); // 가상 시그널 번호로 핸들러 호출하여 정리 시도
        }
        if (echo_thread.joinable()) echo_thread.join(); // 스레드 정리 시도
        return 1;
    }
    catch (const std::runtime_error& e) { // 환경 변수 파싱 오류 등
        fprintf(stderr, "[Error] Runtime error during server setup: %s\n", e.what());
        // 위와 유사하게 종료 처리 시도
        if (!shutdown_requested.load()) {
            signal_handler(0);
        }
        if (echo_thread.joinable()) echo_thread.join();
        return 1;
    }
    catch (const std::exception& e) { // 그 외 표준 라이브러리 예외
        fprintf(stderr, "[Error] Unhandled standard exception in main: %s\n", e.what());
        if (!shutdown_requested.load()) {
            signal_handler(0);
        }
        if (echo_thread.joinable()) echo_thread.join();
        return 1;
    }
    catch (...) { // 알 수 없는 예외
        fprintf(stderr, "[Error] Unknown exception caught in main!\n");
        if (!shutdown_requested.load()) {
            signal_handler(0);
        }
        if (echo_thread.joinable()) echo_thread.join();
        return 1;
    }

    // 전역 포인터 정리 (선택적, 프로그램 종료 시 자동 해제되나 명시적 정리)
    g_http_server_ptr.reset();
    g_echo_io_context_ptr = nullptr;

    fprintf(stdout, "Server application finished gracefully.\n");
    return 0; // 정상 종료
}