#include <gtest/gtest.h>
#include "HttpServer.hpp" // 우리가 만든 HttpServer 클래스 헤더
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include <thread>
#include <chrono> // std::chrono::milliseconds

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// --- 테스트를 위한 Fixture 클래스 ---
class HttpServerTest : public ::testing::Test {
protected:
    // 테스트에 사용할 포트 (실제 서버 포트와 겹치지 않도록 주의)
    // 또는 고정된 테스트용 포트 사용
    const unsigned short test_port = 8888;
    const std::string test_ip = "127.0.0.1";
    const int test_threads = 1; // 테스트에는 스레드 1개면 충분할 수 있음
    const std::string test_doc_root = "."; // 정적 파일 루트 (필요 시)

    std::unique_ptr<HttpServer> server;
    std::thread server_thread;

    // 각 테스트 케이스 시작 전 호출
    void SetUp() override {
        // 서버 객체 생성
        server = std::make_unique<HttpServer>(test_ip, test_port, test_threads, test_doc_root);

        // 별도 스레드에서 서버 실행
        server_thread = std::thread([this]() {
            try {
                server->run(); // 서버 시작 (내부 스레드 생성 및 io_context 실행)
                fprintf(stdout, "Test HTTP server thread finished.\n");
            }
            catch (const std::exception& e) {
                fprintf(stderr, "Test HTTP server thread exception: %s\n", e.what());
                // 테스트 실패 처리 또는 로깅
            }
            });

        // 서버가 시작되고 리스닝 상태가 될 때까지 잠시 대기
        // 더 견고한 방법: 서버 내부에서 준비 완료 신호를 보내거나,
        // 루프를 돌며 접속 시도를 해보는 방법 등이 있음
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 잠시 대기 (환경에 따라 조정 필요)
        fprintf(stdout, "Test HTTP server setup complete, running on port %d.\n", test_port);
    }

    // 각 테스트 케이스 종료 후 호출
    void TearDown() override {
        fprintf(stdout, "[HttpServerTest::TearDown] Starting teardown for port %d...\n", test_port);
        if (server) {
            fprintf(stdout, "[HttpServerTest::TearDown] Calling server->stop()...\n");
            server->stop();
            fprintf(stdout, "[HttpServerTest::TearDown] server->stop() returned.\n");
        }
        else {
            fprintf(stdout, "[HttpServerTest::TearDown] Server pointer is null.\n");
        }

        // 이 스레드는 server->run()을 호출한 스레드임
        if (server_thread.joinable()) {
            std::stringstream ss;
            ss << server_thread.get_id();
            fprintf(stdout, "[HttpServerTest::TearDown] Joining server starter thread (ID: %s)...\n", ss.str().c_str());
            server_thread.join();
            fprintf(stdout, "[HttpServerTest::TearDown] Server starter thread joined.\n");
        }
        else {
            fprintf(stdout, "[HttpServerTest::TearDown] Server starter thread is not joinable.\n");
        }
        fprintf(stdout, "[HttpServerTest::TearDown] Teardown complete.\n");
    }

    // 테스트용 간단한 동기 HTTP GET 요청 함수
    http::response<http::string_body> http_get(const std::string& target) {
        http::response<http::string_body> res; // 응답 객체
        try {
            net::io_context ioc; // 클라이언트용 io_context
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc); // 클라이언트 스트림

            auto const results = resolver.resolve(test_ip, std::to_string(test_port));
            stream.connect(results); // 서버에 연결

            // HTTP GET 요청 설정
            http::request<http::empty_body> req{ http::verb::get, target, 11 }; // version 1.1
            req.set(http::field::host, test_ip + ":" + std::to_string(test_port));
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            http::write(stream, req); // 동기적으로 요청 전송

            beast::flat_buffer buffer; // 응답 수신 버퍼
            http::read(stream, buffer, res); // 동기적으로 응답 읽기

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec); // 연결 종료

            // 연결 종료 오류 중 "not connected"는 무시 (이미 서버가 닫았을 수 있음)
            if (ec && ec != beast::errc::not_connected) {
                throw beast::system_error{ ec };
            }
        }
        catch (std::exception const& e) {
            fprintf(stderr, "HTTP GET Error: %s\n", e.what());
            // 오류 발생 시 응답 상태 코드를 비정상적인 값으로 설정하여 테스트 실패 유도 가능
            res.result(http::status::internal_server_error);
            res.body() = e.what();
        }
        return res;
    }
};

// --- 테스트 케이스 ---

// /health 경로 요청 시 200 OK와 "OK" 본문을 반환하는지 테스트
TEST_F(HttpServerTest, HealthCheckReturnsOK) {
    // Act
    auto response = http_get("/health");

    // Assert
    EXPECT_EQ(response.result_int(), 200); // 상태 코드가 200인지 확인
    EXPECT_EQ(response.at(http::field::content_type), "text/plain"); // Content-Type 확인
    EXPECT_EQ(response.body(), "OK");         // 본문이 "OK"인지 확인
}

// 존재하지 않는 경로 요청 시 404 Not Found를 반환하는지 테스트
TEST_F(HttpServerTest, NotFoundReturns404) {
    // Act
    auto response = http_get("/nonexistentpath");

    // Assert
    EXPECT_EQ(response.result_int(), 404); // 상태 코드가 404인지 확인
    EXPECT_NE(response.body().find("not found"), std::string::npos); // 본문에 "not found" 포함 확인
}