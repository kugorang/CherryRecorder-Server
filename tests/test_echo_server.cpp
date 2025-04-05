#include <gtest/gtest.h>
#include "../include/CherryRecorder-Server.hpp" // EchoServer 클래스 헤더
#include <boost/asio/error.hpp>
#include <thread>
#include <chrono>    // std::chrono 사용
#include <memory>    // std::unique_ptr 사용
#include <atomic>    // std::atomic_bool 사용 (서버 준비 상태 확인용)
#include <system_error> // std::error_code 사용

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// --- TCP Echo 서버 테스트를 위한 Fixture 클래스 ---
class EchoServerTest : public ::testing::Test {
protected:
    // 테스트용 포트 (0으로 설정 시 시스템이 사용 가능한 포트 자동 할당)
    // 참고: 포트 0 사용 시 실제 할당된 포트를 알아내야 클라이언트가 접속 가능
    // 여기서는 충돌 가능성 낮은 고정 포트 사용 예시
    const unsigned short test_port = 33334; // 기존 테스트와 다른 포트 사용
    const std::string test_ip = "127.0.0.1";

    // 서버 실행 관련 멤버
    std::unique_ptr<net::io_context> server_ioc; // 서버용 io_context
    std::unique_ptr<EchoServer> server;          // 서버 객체
    std::thread server_thread;                   // 서버 실행 스레드
    std::atomic<bool> server_ready{ false };       // 서버 준비 완료 플래그
    std::atomic<bool> server_stop_requested{ false }; // 서버 중지 요청 플래그

    // 각 테스트 케이스 시작 전 호출
    void SetUp() override {
        server_ready.store(false);
        server_stop_requested.store(false);
        server_ioc = std::make_unique<net::io_context>();

        try {
            // 서버 객체 생성
            server = std::make_unique<EchoServer>(*server_ioc, test_port);
            server->start(); // 비동기 accept 시작

            // 별도 스레드에서 서버 io_context 실행
            server_thread = std::thread([this]() {
                fprintf(stdout, "Test Echo Server thread started, running io_context on port %d...\n", test_port);
                // 서버가 리스닝 시작했음을 알릴 준비가 되면 플래그 설정 (예시)
                // 실제 EchoServer 클래스 수정이 필요할 수 있음 (예: start() 후 콜백 호출)
                // 여기서는 간단히 accept 시작 후 준비된 것으로 간주
                std::stringstream thread_id_ss;
                thread_id_ss << std::this_thread::get_id();
                std::string thread_id_str = thread_id_ss.str();
                try {
                    fprintf(stdout, "[Echo Server Thread ID: %s] Starting server_ioc->run() on port %d...\n", thread_id_str.c_str(), test_port);
                    server_ready.store(true); // 준비 완료 알림
                    server_ioc->run();
                    fprintf(stdout, "[Echo Server Thread ID: %s] server_ioc->run() finished cleanly.\n", thread_id_str.c_str());
                }
                catch (const std::exception& e) {
                    fprintf(stderr, "[Echo Server Thread ID: %s] Exception: %s\n", thread_id_str.c_str(), e.what());
                    FAIL() << "Echo Server thread crashed: " << e.what(); // GTest에 실패 보고
                }
                catch (...) {
                    fprintf(stderr, "[Echo Server Thread ID: %s] Unknown exception caught.\n", thread_id_str.c_str());
                    FAIL() << "Echo Server thread crashed with unknown exception."; // GTest에 실패 보고
                }
            });

            // 서버가 준비될 때까지 대기 (최대 1초)
            int wait_count = 0;
            while (!server_ready.load() && wait_count < 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                wait_count++;
            }
            ASSERT_TRUE(server_ready.load()) << "Server did not become ready in time.";
            fprintf(stdout, "Test Echo Server setup complete.\n");

        }
        catch (const std::exception& e) {
            // SetUp 중 예외 발생 시 정리
            if (server_ioc && !server_ioc->stopped()) {
                server_ioc->stop();
            }
            if (server_thread.joinable()) {
                server_thread.join();
            }
            FAIL() << "EchoServer SetUp failed: " << e.what();
        }
    }

    // 각 테스트 케이스 종료 후 호출
    void TearDown() override {
        fprintf(stdout, "Tearing down test Echo server...\n");
        // io_context 중지
        if (server_ioc && !server_ioc->stopped()) {
            server_ioc->stop();
        }
        // 스레드 join
        if (server_thread.joinable()) {
            server_thread.join();
        }
        // --- 서버 객체 명시적 리셋 추가 ---
        // 이것은 EchoServer 소멸자 호출을 보장하여 acceptor가 닫히도록 함
        server.reset();
        fprintf(stdout, "[EchoServerTest::TearDown] Server object reset.\n");
        // --- 추가 끝 ---

        fprintf(stdout, "Test Echo Server teardown complete.\n");
    }

    // 테스트용 간단한 동기 TCP 클라이언트 함수
    std::string send_and_receive(const std::string& message) {
        std::string received_message;
        boost::system::error_code ec; // 오류 코드

        try {
            net::io_context client_ioc;
            tcp::socket socket(client_ioc);
            tcp::resolver resolver(client_ioc);

            // 서버 주소 확인 및 연결
            auto endpoints = resolver.resolve(test_ip, std::to_string(test_port), ec);
            if (ec) { throw std::system_error(ec, "Resolver failed"); }

            net::connect(socket, endpoints, ec);
            if (ec) { throw std::system_error(ec, "Connect failed"); }

            // 메시지 전송
            size_t sent_len = net::write(socket, net::buffer(message), ec);
            if (ec) { throw std::system_error(ec, "Send failed"); }
            if (sent_len != message.size()) { throw std::runtime_error("Partial send occurred"); }

            // 응답 수신
            std::vector<char> recv_buf(message.size() + 1); // 수신 버퍼 (충분한 크기)
            size_t len = socket.read_some(net::buffer(recv_buf), ec);
            if (ec == net::error::eof) {
                fprintf(stdout, "Connection closed by peer after send.\n");
                // EOF는 정상적인 종료일 수 있음, 읽은 데이터가 있다면 처리
                if (len > 0) {
                    received_message.assign(recv_buf.data(), len);
                }
            }
            else if (ec) {
                throw std::system_error(ec, "Receive failed");
            }
            else {
                received_message.assign(recv_buf.data(), len);
            }

            // 소켓 닫기
            socket.shutdown(tcp::socket::shutdown_both, ec);

            // 무시할 수 있는 오류 확인 (Asio 오류 코드만 비교)
            if (ec && ec != net::error::not_connected) {
                // 여기에 해당하는 다른 무시 가능한 Asio 오류가 있다면 추가 가능
                // 예: && ec != net::error::connection_reset 등 (상황에 따라)
                fprintf(stderr, "Socket shutdown warning: %s (%d)\n", ec.message().c_str(), ec.value());
            }
            socket.close(ec);
            // close 후 오류 처리도 유사하게 진행
            if (ec && ec != net::error::not_connected) { // 필요 시 close 후 오류도 확인
                fprintf(stderr, "Socket close warning: %s (%d)\n", ec.message().c_str(), ec.value());
            }
        }
        catch (const std::exception& e) {
            received_message = "[ERROR] " + std::string(e.what());
            ADD_FAILURE() << "Client operation failed: " << e.what();
        }
        return received_message;
    }

    std::string send_empty_and_expect_eof() {
        std::string received_message = ""; // 최종적으로 빈 문자열이어야 함
        boost::system::error_code ec;
        bool first_read = true;          // shutdown 후 첫 번째 읽기인지 여부 플래그
        bool ignored_null_byte = false; // 널 바이트를 무시했는지 여부 플래그

        try {
            net::io_context client_ioc;
            tcp::socket socket(client_ioc);
            tcp::resolver resolver(client_ioc);

            auto endpoints = resolver.resolve(test_ip, std::to_string(test_port), ec);
            if (ec) { throw std::system_error(ec, "Resolver failed"); }

            net::connect(socket, endpoints, ec);
            if (ec) { throw std::system_error(ec, "Connect failed"); }

            // 1. 빈 버퍼 전송 (0 바이트 쓰기 시도)
            size_t sent_len = net::write(socket, net::buffer(""), ec);
            if (ec) { throw std::system_error(ec, "Send (empty) failed"); }
            fprintf(stdout, "[Test Client Empty] Sent 0 bytes.\n");

            // 2. 클라이언트가 더 이상 보낼 데이터 없음을 알림 (핵심)
            socket.shutdown(tcp::socket::shutdown_send, ec);
            if (ec && ec != net::error::not_connected) { // shutdown 시 not_connected 오류는 무시 가능
                fprintf(stderr, "[Test Client Empty] Shutdown(send) warning: %s\n", ec.message().c_str());
            }
            else if (!ec) {
                fprintf(stdout, "[Test Client Empty] Shutdown send direction.\n");
            }
            else {
                fprintf(stdout, "[Test Client Empty] Shutdown(send) returned 'not connected'.\n");
            }

            // 3. EOF를 받을 때까지 읽기 (첫 0x00 바이트는 무시)
            std::vector<char> recv_buf(512); // 적당한 크기의 버퍼
            for (;;) {
                size_t len = socket.read_some(net::buffer(recv_buf), ec);

                if (ec == net::error::eof) {
                    // 정상 EOF 수신
                    fprintf(stdout, "[Test Client Empty] Received EOF as expected%s.\n",
                        ignored_null_byte ? " (after ignoring 1 null byte)" : "");
                    break; // 루프 종료
                }
                else if (ec) {
                    // 다른 오류 발생 시 실패 처리
                    throw std::system_error(ec, "Receive failed after empty send");
                }

                // 데이터를 수신한 경우
                if (len > 0) {
                    // --- 수정된 로직 ---
                    // 첫 번째 읽기이고, 1 바이트만 왔고, 그 값이 0x00 이면 무시
                    if (first_read && len == 1 && static_cast<unsigned char>(recv_buf[0]) == 0x00) {
                        fprintf(stdout, "[Test Client Empty] Ignoring single null byte received after shutdown(send).\n");
                        ignored_null_byte = true;
                        // received_message에 추가하지 않고 다음 읽기 시도
                    }
                    else {
                        // 그 외의 경우 (두 번째 이후 읽기, 1바이트 초과, 0x00 아님)는 예상치 못한 데이터로 간주
                        fprintf(stderr, "[Test Client Empty] Unexpectedly received %zu bytes (first_read=%d, ignored_null=%d). Hex:",
                            len, first_read, ignored_null_byte);
                        for (size_t i = 0; i < len; ++i) fprintf(stderr, " %02X", static_cast<unsigned char>(recv_buf[i]));
                        fprintf(stderr, "\n");

                        received_message.assign(recv_buf.data(), len); // 오류 보고 위해 임시 저장
                        throw std::runtime_error("Server sent data unexpectedly for empty input");
                    }
                    // --- 수정 끝 ---
                }
                else {
                    // 오류 없이 0 바이트 읽기 (일반적이지 않음)
                    fprintf(stdout, "[Test Client Empty] read_some returned 0 bytes without error.\n");
                }
                first_read = false; // 다음부터는 첫 번째 읽기가 아님
            }

            // 소켓 완전 종료 (read_some이 EOF로 종료되었으므로 오류는 무시 가능)
            socket.close(ec);
        }
        catch (const std::exception& e) {
            // 예외 발생 시 오류 메시지 생성
            std::string error_prefix = "[ERROR] ";
            std::string error_what = e.what();
            // 이미 오류 메시지가 있다면 합치기 (예: 예상치 못한 데이터 수신 후 예외 발생 시)
            if (received_message.find(error_prefix) != 0 && !received_message.empty()) {
                received_message = error_prefix + error_what + " (after receiving unexpected data: " + received_message + ")";
            }
            else {
                received_message = error_prefix + error_what;
            }
            ADD_FAILURE() << "Client (empty send) operation failed: " << e.what();
        }
        // 정상적인 경우 (EOF 받고 종료) received_message는 여전히 "" 상태
        return received_message;
    }

};

// --- 테스트 케이스 (Fixture 사용) ---

// 가장 기본적인 Echo 동작 검증
TEST_F(EchoServerTest, BasicEcho) {
    // Arrange
    std::string message = "Hello, Echo Server!";

    // Act
    std::string received = send_and_receive(message);

    // Assert
    ASSERT_NE(received.find("[ERROR]"), 0) << "Client function returned an error."; // 오류 발생 여부 확인
    EXPECT_EQ(received, message); // 받은 메시지가 보낸 메시지와 같은지 확인
}

// 다른 메시지로 추가 테스트
TEST_F(EchoServerTest, DifferentMessage) {
    // Arrange
    std::string message = "Another test message 123!@#";

    // Act
    std::string received = send_and_receive(message);

    // Assert
    ASSERT_NE(received.find("[ERROR]"), 0) << "Client function returned an error.";
    EXPECT_EQ(received, message);
}

// --- 수정된 테스트 케이스 ---
TEST_F(EchoServerTest, EmptyMessageGetsEmptyResponseOrEOF) {
    // Act
    std::string received = send_empty_and_expect_eof(); // 새 헬퍼 함수 사용

    // Assert
    // 헬퍼 함수 자체에서 오류가 발생했는지 먼저 확인 (더 안정적인 방법)
    ASSERT_EQ(received.find("[ERROR]"), std::string::npos)
        << "Client function itself reported an error: " << received;

    // 서버가 아무것도 보내지 않고 연결을 정상 종료(EOF)했으므로, 받은 메시지는 빈 문자열이어야 함
    EXPECT_EQ(received, "");
}

// TODO: 여기에 추가적인 테스트 케이스 구현
// - 여러 클라이언트 동시 접속
// - 대용량 데이터 전송
// - 비정상적인 연결 종료 등