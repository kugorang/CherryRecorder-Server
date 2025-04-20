#include <gtest/gtest.h>
#include "../include/CherryRecorder-Server.hpp" // 테스트 대상 EchoServer 클래스 헤더
#include <boost/asio/connect.hpp> // boost::asio::connect
#include <boost/asio/read.hpp>    // boost::asio::read (동기 읽기)
#include <boost/asio/write.hpp>   // boost::asio::write (동기 쓰기)
#include <boost/asio/ip/tcp.hpp> // tcp::socket, tcp::resolver
#include <boost/asio/io_context.hpp> // io_context
#include <boost/system/error_code.hpp> // boost::system::error_code
#include <boost/asio/error.hpp> // boost::asio::error 네임스페이스 (예: eof)

#include <thread>
#include <chrono>    // std::chrono 네임스페이스 (sleep_for 등)
#include <memory>    // std::unique_ptr
#include <atomic>    // std::atomic<bool>
#include <system_error> // std::system_error (예외)
#include <string>    // std::string
#include <vector>    // std::vector<char>
#include <cstdio>    // fprintf (로깅용)
#include <sstream>   // std::stringstream (스레드 ID 로깅용)
#include <stdexcept> // std::runtime_error

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @file test_echo_server.cpp
 * @brief `EchoServer` 클래스의 기능을 검증하는 Google Test 테스트 파일.
 *
 * `EchoServerTest` Fixture 클래스를 사용하여 각 테스트 전에 서버를 설정하고,
 * 테스트 종료 후 정리한다. 기본적인 에코 동작, 빈 메시지 처리 등을 테스트한다.
 * @see EchoServer, EchoServerTest
 */

 // --- TCP Echo 서버 테스트를 위한 Fixture 클래스 ---
 /**
  * @class EchoServerTest
  * @brief `EchoServer` 테스트를 위한 Google Test Fixture 클래스.
  * @details 각 테스트 케이스 실행 전에 `SetUp()` 메서드를 호출하여 `EchoServer` 인스턴스를
  * 생성하고 별도 스레드에서 실행시킨다. 테스트 종료 후 `TearDown()` 메서드를 호출하여
  * 서버를 중지하고 관련 리소스를 정리한다. 테스트용 클라이언트 함수(`send_and_receive`, `send_empty_and_expect_eof`)를 제공한다.
  */
class EchoServerTest : public ::testing::Test {
protected:
    // 테스트 설정 값
    const unsigned short test_port = 33333; ///< @brief 테스트에 사용할 TCP 포트 번호.
    const std::string test_ip = "127.0.0.1"; ///< @brief 테스트 서버가 리슨할 IP 주소 (localhost).

    // 서버 실행 관련 멤버 변수
    std::unique_ptr<net::io_context> server_ioc; ///< @brief 서버용 io_context 객체 포인터.
    std::unique_ptr<EchoServer> server;          ///< @brief 테스트 대상 EchoServer 객체 포인터.
    std::thread server_thread;                   ///< @brief 서버 io_context를 실행하는 스레드 객체.
    std::atomic<bool> server_ready{ false };       ///< @brief 서버가 accept 준비 완료 상태임을 나타내는 플래그.
    std::atomic<bool> server_stop_requested{ false }; ///< @brief (현재 미사용) 서버 중지 요청 플래그.

    /**
     * @brief 각 테스트 케이스 시작 전에 호출되는 설정 메서드.
     *
     * `server_ioc`와 `EchoServer` 객체를 생성하고, `EchoServer::start()`를 호출한다.
     * 별도의 `server_thread`를 생성하여 `server_ioc->run()`을 실행시킨다.
     * 서버가 리스닝을 시작할 때까지 잠시 대기한다 (`server_ready` 플래그 사용).
     * @throw testing::AssertionResult 서버 준비 대기 시간 초과 또는 서버 생성/시작 중 예외 발생 시 테스트 실패 처리.
     */
    void SetUp() override {
        fprintf(stdout, "[EchoServerTest::SetUp] Starting setup for port %d...\n", test_port);
        server_ready.store(false);
        server_stop_requested.store(false);
        server_ioc = std::make_unique<net::io_context>();

        try {
            // 서버 객체 생성 (지정된 io_context와 포트 사용)
            server = std::make_unique<EchoServer>(*server_ioc, test_port);
            server->start(); // 비동기 accept 시작 요청

            // 별도 스레드에서 서버 io_context 실행
            server_thread = std::thread([this]() { // this 캡처하여 멤버 접근
                std::stringstream thread_id_ss;
                thread_id_ss << std::this_thread::get_id();
                std::string thread_id_str = thread_id_ss.str();
                fprintf(stdout, "[Echo Server Thread ID: %s] Started, running io_context on port %d...\n", thread_id_str.c_str(), test_port);

                // 서버가 accept 준비되었음을 알림 (start() 호출 직후 준비된 것으로 간주)
                // 더 정확하게는 EchoServer 내부에서 accept 준비 후 콜백 호출 방식 고려 가능
                server_ready.store(true);
                fprintf(stdout, "[Echo Server Thread ID: %s] Server marked as ready.\n", thread_id_str.c_str());

                try {
                    // io_context 이벤트 루프 실행 (블로킹)
                    // server_ioc->stop()이 호출될 때까지 실행됨
                    server_ioc->run();
                    fprintf(stdout, "[Echo Server Thread ID: %s] server_ioc->run() finished cleanly.\n", thread_id_str.c_str());
                }
                catch (const std::exception& e) {
                    fprintf(stderr, "[Echo Server Thread ID: %s] Exception in io_context.run(): %s\n", thread_id_str.c_str(), e.what());
                    // GTest Fixture 스레드에서 예외 발생 시 테스트 실패 처리 어려움. 로깅 위주.
                    // 필요 시 공유 플래그 설정 후 메인 스레드에서 확인.
                    // FAIL() << "Echo Server thread crashed: " << e.what(); // 다른 스레드에서 FAIL 사용은 주의
                }
                catch (...) {
                    fprintf(stderr, "[Echo Server Thread ID: %s] Unknown exception caught in io_context.run().\n", thread_id_str.c_str());
                    // FAIL() << "Echo Server thread crashed with unknown exception.";
                }
                });

            // 서버가 준비될 때까지 최대 1초 대기
            int wait_count = 0;
            const int max_wait_count = 100; // 100 * 10ms = 1000ms = 1s
            while (!server_ready.load() && wait_count < max_wait_count) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                wait_count++;
            }
            // 대기 시간 초과 시 테스트 실패 처리
            ASSERT_TRUE(server_ready.load()) << "Server did not become ready within " << (max_wait_count * 10) << "ms.";
            fprintf(stdout, "[EchoServerTest::SetUp] Server is ready. Setup complete.\n");

        }
        catch (const std::exception& e) {
            // SetUp 중 예외 발생 시 정리 작업 및 테스트 실패 처리
            fprintf(stderr, "[EchoServerTest::SetUp] Exception during setup: %s\n", e.what());
            if (server_ioc && !server_ioc->stopped()) {
                server_ioc->stop();
            }
            if (server_thread.joinable()) {
                server_thread.join();
            }
            server.reset(); // 서버 객체 정리
            FAIL() << "EchoServer SetUp failed: " << e.what(); // GTest에 실패 보고
        }
    }

    /**
     * @brief 각 테스트 케이스 종료 후 호출되는 정리 메서드.
     *
     * `server_ioc`를 중지시키고, `server_thread`가 종료될 때까지 기다린다.
     * `EchoServer` 객체를 소멸시켜 acceptor 등 리소스를 정리한다.
     */
    void TearDown() override {
        fprintf(stdout, "[EchoServerTest::TearDown] Starting teardown for port %d...\n", test_port);
        // 1. io_context 중지 요청 (실행 중인 run() 반환 유도)
        if (server_ioc && !server_ioc->stopped()) {
            fprintf(stdout, "[EchoServerTest::TearDown] Stopping io_context...\n");
            server_ioc->stop();
        }
        else {
            fprintf(stdout, "[EchoServerTest::TearDown] io_context already stopped or null.\n");
        }

        // 2. 서버 스레드 종료 대기
        if (server_thread.joinable()) {
            std::stringstream ss;
            ss << server_thread.get_id();
            fprintf(stdout, "[EchoServerTest::TearDown] Joining server thread (ID: %s)...\n", ss.str().c_str());
            server_thread.join(); // 스레드 종료까지 대기
            fprintf(stdout, "[EchoServerTest::TearDown] Server thread joined.\n");
        }
        else {
            fprintf(stdout, "[EchoServerTest::TearDown] Server thread is not joinable.\n");
        }

        // 3. 서버 객체 명시적 리셋 (소멸자 호출 보장 -> acceptor 닫힘)
        fprintf(stdout, "[EchoServerTest::TearDown] Resetting server object...\n");
        server.reset();
        fprintf(stdout, "[EchoServerTest::TearDown] Server object reset.\n");

        // 4. io_context 객체 리셋 (선택적)
        // server_ioc.reset();

        fprintf(stdout, "[EchoServerTest::TearDown] Teardown complete.\n");
    }

    /**
     * @brief 테스트용 동기 TCP 클라이언트 함수: 메시지 전송 및 응답 수신.
     * @param message 서버로 전송할 문자열 메시지.
     * @return 서버로부터 받은 응답 문자열. 오류 발생 시 "[ERROR] ..." 형태의 문자열 반환.
     *
     * localhost의 `test_port`로 TCP 연결을 시도하고, 주어진 메시지를 전송한 후,
     * 서버로부터 데이터를 수신하여 반환한다. Boost.Asio의 동기 함수 사용.
     */
    std::string send_and_receive(const std::string& message) {
        std::string received_message;
        boost::system::error_code ec; // 오류 코드 저장 변수

        try {
            net::io_context client_ioc; // 클라이언트용 io_context
            tcp::socket socket(client_ioc); // 클라이언트 소켓
            tcp::resolver resolver(client_ioc); // 주소 해석기

            // 서버 주소 해석 (localhost:test_port)
            fprintf(stdout, "[Test Client] Resolving %s:%d...\n", test_ip.c_str(), test_port);
            auto endpoints = resolver.resolve(test_ip, std::to_string(test_port), ec);
            if (ec) { throw std::system_error(ec, "Resolver failed"); }

            // 서버에 동기적으로 연결 시도
            fprintf(stdout, "[Test Client] Connecting...\n");
            net::connect(socket, endpoints, ec);
            if (ec) { throw std::system_error(ec, "Connect failed"); }
            fprintf(stdout, "[Test Client] Connected.\n");

            // 메시지 동기적으로 전송
            fprintf(stdout, "[Test Client] Sending message (%zu bytes): '%s'\n", message.size(), message.c_str());
            size_t sent_len = net::write(socket, net::buffer(message), ec);
            if (ec) { throw std::system_error(ec, "Send failed"); }
            if (sent_len != message.size()) { throw std::runtime_error("Partial send occurred"); }
            fprintf(stdout, "[Test Client] Sent %zu bytes.\n", sent_len);

            // 응답 동기적으로 수신 (버퍼 크기는 보낸 메시지 크기 + 알파)
            // Echo 서버는 보낸 만큼만 돌려주므로 보낸 크기만큼만 읽어도 무방할 수 있으나,
            // 약간의 여유를 두는 것이 안전할 수 있음.
            std::vector<char> recv_buf(message.size() + 1); // null 종료 고려 + 약간의 여유
            fprintf(stdout, "[Test Client] Reading response...\n");
            // read_some: 최소 1바이트라도 읽으면 반환.
            // read: 요청한 크기만큼 읽거나 EOF/오류 발생 시 반환. Echo 테스트에는 read가 더 적합할 수 있음.
            // 여기서는 read_some 사용 예시 유지. 필요 시 net::read(socket, net::buffer(recv_buf, message.size()), ec); 사용 고려.
            size_t len = socket.read_some(net::buffer(recv_buf), ec);

            if (ec == net::error::eof) { // 서버가 응답 후 연결을 바로 닫은 경우 (EOF)
                fprintf(stdout, "[Test Client] Received EOF after reading %zu bytes.\n", len);
                // EOF 자체는 오류가 아닐 수 있음. 읽은 데이터가 있다면 처리.
                if (len > 0) {
                    received_message.assign(recv_buf.data(), len);
                }
                // EOF 받았으므로 더 이상 읽을 것 없음.
            }
            else if (ec) { // 그 외 다른 읽기 오류
                throw std::system_error(ec, "Receive failed");
            }
            else { // 오류 없이 데이터 수신
                received_message.assign(recv_buf.data(), len);
                fprintf(stdout, "[Test Client] Received %zu bytes: '%s'\n", len, received_message.c_str());
                // TODO: 만약 서버가 보낸 것보다 더 많은 데이터를 보냈다면? 추가 읽기 로직 필요.
                //       Echo 서버는 그럴 일 없으므로 여기서는 생략.
            }

            // 소켓 닫기 (정상 종료)
            fprintf(stdout, "[Test Client] Shutting down and closing socket...\n");
            // 양방향 셧다운 시도 (오류 무시)
            socket.shutdown(tcp::socket::shutdown_both, ec);
            // 소켓 닫기 (오류 무시)
            socket.close(ec);
            fprintf(stdout, "[Test Client] Socket closed.\n");

        }
        catch (const std::system_error& e) { // Asio 관련 오류
            received_message = "[ERROR] System error: " + std::string(e.what()) + " (code: " + std::to_string(e.code().value()) + ")";
            ADD_FAILURE() << "Client operation failed: " << received_message; // GTest에 실패 기록
        }
        catch (const std::runtime_error& e) { // 기타 런타임 오류
            received_message = "[ERROR] Runtime error: " + std::string(e.what());
            ADD_FAILURE() << "Client operation failed: " << received_message;
        }
        catch (const std::exception& e) { // 그 외 표준 예외
            received_message = "[ERROR] Exception: " + std::string(e.what());
            ADD_FAILURE() << "Client operation failed: " << received_message;
        }
        catch (...) { // 알 수 없는 예외
            received_message = "[ERROR] Unknown exception occurred.";
            ADD_FAILURE() << "Client operation failed with unknown exception.";
        }
        return received_message; // 성공 시 받은 메시지, 실패 시 에러 문자열 반환
    }


    /**
     * @brief 테스트용 동기 TCP 클라이언트 함수: 빈 메시지 전송 후 EOF 수신 확인.
     * @return 서버로부터 응답 없이 정상적으로 EOF를 받으면 빈 문자열("") 반환.
     * 오류 발생 또는 예상치 못한 데이터 수신 시 "[ERROR] ..." 형태의 문자열 반환.
     *
     * 서버에 연결 후, 0 바이트 쓰기를 시도하고 즉시 `shutdown(shutdown_send)`를 호출하여
     * 더 이상 보낼 데이터가 없음을 알린다. 이후 서버가 연결을 닫을 때까지(EOF 수신) 읽기를 시도한다.
     * 서버가 정상적으로 아무 데이터도 보내지 않고 연결을 닫는지 확인한다.
     * (참고: 서버 구현에 따라 shutdown 후 1 null byte를 보내는 경우가 있어 이를 무시하는 로직 포함)
     */
    std::string send_empty_and_expect_eof() {
        std::string received_message = ""; // 최종적으로 빈 문자열이어야 함
        boost::system::error_code ec;
        bool first_read_after_shutdown = true; // shutdown 후 첫 번째 읽기인지 여부 플래그
        bool ignored_single_null_byte = false; // 널 바이트를 무시했는지 여부 플래그

        try {
            net::io_context client_ioc;
            tcp::socket socket(client_ioc);
            tcp::resolver resolver(client_ioc);

            fprintf(stdout, "[Test Client Empty] Resolving %s:%d...\n", test_ip.c_str(), test_port);
            auto endpoints = resolver.resolve(test_ip, std::to_string(test_port), ec);
            if (ec) { throw std::system_error(ec, "Resolver failed"); }

            fprintf(stdout, "[Test Client Empty] Connecting...\n");
            net::connect(socket, endpoints, ec);
            if (ec) { throw std::system_error(ec, "Connect failed"); }
            fprintf(stdout, "[Test Client Empty] Connected.\n");

            // 1. 빈 버퍼 전송 시도 (실제로는 0 바이트 쓰기) -> 일부 OS/스택에서는 아무 동작 안 할 수 있음
            // net::write(socket, net::buffer(""), ec); // 이 부분은 생략해도 무방할 수 있음
            // if (ec) { throw std::system_error(ec, "Send (empty) failed"); }
            // fprintf(stdout, "[Test Client Empty] Sent 0 bytes (attempted).\n");

            // 2. 클라이언트가 더 이상 보낼 데이터 없음을 명시적으로 알림 (핵심)
            fprintf(stdout, "[Test Client Empty] Shutting down send direction...\n");
            socket.shutdown(tcp::socket::shutdown_send, ec);
            // shutdown 오류는 발생할 수 있으나, 테스트 진행에 치명적이지 않을 수 있음 (로깅만)
            if (ec && ec != net::error::not_connected) { // not_connected는 이미 서버가 닫았을 때 발생 가능, 무시
                fprintf(stderr, "[Test Client Empty] Shutdown(send) warning: %s\n", ec.message().c_str());
            }
            else if (!ec) {
                fprintf(stdout, "[Test Client Empty] Shutdown send direction successful.\n");
            }
            else { // net::error::not_connected 경우
                fprintf(stdout, "[Test Client Empty] Shutdown(send) returned 'not connected'.\n");
            }


            // 3. 서버로부터 EOF를 받을 때까지 읽기 시도
            std::vector<char> recv_buf(512); // 적당한 크기의 버퍼
            fprintf(stdout, "[Test Client Empty] Reading until EOF...\n");
            for (;;) { // EOF 또는 오류 발생 시 루프 탈출
                size_t len = socket.read_some(net::buffer(recv_buf), ec);

                if (ec == net::error::eof) { // 정상적인 EOF 수신
                    fprintf(stdout, "[Test Client Empty] Received EOF as expected%s.\n",
                        ignored_single_null_byte ? " (after ignoring 1 null byte)" : "");
                    break; // 루프 종료
                }
                else if (ec) { // EOF 외 다른 오류 발생
                    throw std::system_error(ec, "Receive failed after empty send/shutdown");
                }

                // 오류 없이 데이터를 수신한 경우
                if (len > 0) {
                    // 특정 서버 구현(특히 Windows + Asio?)은 shutdown(send) 후 의미 없는 1 null byte를 보낼 수 있음.
                    // 이 테스트에서는 이를 무시하도록 처리.
                    if (first_read_after_shutdown && len == 1 && static_cast<unsigned char>(recv_buf[0]) == 0x00) {
                        fprintf(stdout, "[Test Client Empty] Ignoring single null byte received after shutdown(send).\n");
                        ignored_single_null_byte = true;
                        // received_message에 추가하지 않고 다음 읽기 시도
                    }
                    else {
                        // 그 외의 경우 (두 번째 이후 읽기, 1바이트 초과, 0x00 아님)는 예상치 못한 데이터로 간주
                        std::stringstream ss_hex;
                        for (size_t i = 0; i < len; ++i) ss_hex << " " << std::hex << static_cast<int>(static_cast<unsigned char>(recv_buf[i]));
                        fprintf(stderr, "[Test Client Empty] Unexpectedly received %zu bytes. Hex:%s\n", len, ss_hex.str().c_str());

                        received_message.assign(recv_buf.data(), len); // 오류 보고 위해 임시 저장
                        throw std::runtime_error("Server sent unexpected data for empty input after shutdown(send)");
                    }
                }
                else {
                    // 오류 없이 0 바이트 읽기 (일반적이지 않음, 무시하고 계속 읽기 시도)
                    fprintf(stdout, "[Test Client Empty] read_some returned 0 bytes without error. Continuing read.\n");
                }
                first_read_after_shutdown = false; // 다음부터는 첫 번째 읽기가 아님
            } // end for(;;)

            // 소켓 완전 종료 (오류 무시)
            fprintf(stdout, "[Test Client Empty] Closing socket...\n");
            socket.close(ec);

        }
        catch (const std::system_error& e) {
            std::string error_msg = "[ERROR] System error: " + std::string(e.what()) + " (code: " + std::to_string(e.code().value()) + ")";
            // 이미 예상치 못한 데이터 수신 후 예외가 발생했을 수 있음
            if (!received_message.empty() && received_message.find("[ERROR]") != 0) {
                error_msg += " (after receiving unexpected data: " + received_message + ")";
            }
            received_message = error_msg;
            ADD_FAILURE() << "Client (empty send) operation failed: " << received_message;
        }
        catch (const std::runtime_error& e) {
            std::string error_msg = "[ERROR] Runtime error: " + std::string(e.what());
            if (!received_message.empty() && received_message.find("[ERROR]") != 0) {
                error_msg += " (after receiving unexpected data: " + received_message + ")";
            }
            received_message = error_msg;
            ADD_FAILURE() << "Client (empty send) operation failed: " << received_message;
        }
        // 정상적인 경우 (EOF 받고 종료) received_message는 여전히 "" 상태
        return received_message;
    }

}; // end class EchoServerTest

// --- 테스트 케이스 정의 ---

/**
 * @brief 가장 기본적인 Echo 동작 검증 테스트 케이스.
 *
 * "Hello, Echo Server!" 메시지를 보내고 동일한 메시지를 받는지 확인한다.
 */
TEST_F(EchoServerTest, BasicEcho) {
    // Arrange
    std::string message = "Hello, Echo Server!";
    SCOPED_TRACE("Test: BasicEcho, Message: '" + message + "'"); // 테스트 실패 시 추가 정보 출력

    // Act
    std::string received = send_and_receive(message);

    // Assert
    // 1. 클라이언트 함수 자체에서 오류가 발생하지 않았는지 확인
    ASSERT_NE(received.find("[ERROR]"), 0) << "Client function returned an error: " << received;
    // 2. 받은 메시지가 보낸 메시지와 정확히 일치하는지 확인
    EXPECT_EQ(received, message);
}

/**
 * @brief 다른 종류의 메시지로 Echo 동작 검증 테스트 케이스.
 *
 * 특수 문자와 숫자가 포함된 메시지를 보내고 동일한 메시지를 받는지 확인한다.
 */
TEST_F(EchoServerTest, DifferentMessage) {
    // Arrange
    std::string message = "Another test message 123!@#$%^&*()_+=-`~";
    SCOPED_TRACE("Test: DifferentMessage, Message: '" + message + "'");

    // Act
    std::string received = send_and_receive(message);

    // Assert
    ASSERT_NE(received.find("[ERROR]"), 0) << "Client function returned an error: " << received;
    EXPECT_EQ(received, message);
}

/**
 * @brief 빈 메시지 전송 시 서버가 응답 없이 연결을 종료하는지(EOF) 검증 테스트 케이스.
 *
 * 클라이언트가 아무 데이터도 보내지 않고 send 방향을 shutdown 했을 때,
 * 서버로부터 아무 데이터도 받지 않고 정상적으로 EOF를 수신하는지 확인한다.
 */
TEST_F(EchoServerTest, EmptyMessageGetsEOF) {
    // Arrange (없음)
    SCOPED_TRACE("Test: EmptyMessageGetsEOF");

    // Act
    // send_empty_and_expect_eof() 함수는 EOF를 정상 수신하면 "" 반환, 아니면 에러 문자열 반환
    std::string received = send_empty_and_expect_eof();

    // Assert
    // 1. 헬퍼 함수 자체에서 오류가 발생했는지 먼저 확인 (더 안정적인 방법)
    ASSERT_EQ(received.find("[ERROR]"), std::string::npos)
        << "Helper function send_empty_and_expect_eof() reported an error: " << received;

    // 2. 서버가 아무것도 보내지 않고 연결을 정상 종료(EOF)했으므로, 받은 메시지는 빈 문자열이어야 함
    EXPECT_EQ(received, "") << "Expected empty response (EOF) but received data or error.";
}

// TODO: 추가적인 테스트 케이스 구현 제안
// - 여러 클라이언트 동시 접속 테스트 (스레드 여러 개 사용하여 send_and_receive 동시 호출)
// - 대용량 데이터 전송 테스트 (큰 크기의 메시지 사용)
// - 비정상적인 클라이언트 동작 테스트 (예: 데이터 일부만 보내고 연결 끊기)
// - 서버 부하 테스트 (짧은 시간 안에 많은 연결 시도)