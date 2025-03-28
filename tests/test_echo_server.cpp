#include <gtest/gtest.h>
#include "../include/CherryRecorder-Server.hpp"
#include <boost/asio.hpp>
#include <thread>

/**
 * @file test_echo_server.cpp
 * @brief EchoServer 기능을 테스트하기 위한 단위 테스트 모음.
 *
 * Boost.Asio의 비동기 동작을 실제로 검증하기 위해,
 * 서버를 별도 스레드에서 run한 뒤 클라이언트 소켓을 통해 Echo 동작을 확인합니다.
 */

 /**
  * @test EchoServer_BasicEcho
  * @brief 가장 기본적인 Echo 동작을 검증하는 테스트.
  *
  * 1) 특정 포트(33333)에 서버를 실행.
  * 2) 클라이언트 소켓으로 접속 후 문자열 전송.
  * 3) 수신된 문자열이 원본 문자열과 같은지 확인.
  */
TEST(EchoServer, BasicEcho) {
    // Arrange
    boost::asio::io_context io_context;
    EchoServer server(io_context, 33333);
    server.start();

    // 별도 스레드에서 서버의 io_context.run() 실행
    std::thread serverThread([&io_context]() {
        io_context.run();
        });

    // 서버가 완전히 준비될 때까지 약간 대기
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Act: 클라이언트로 연결, 메시지 전송
    boost::asio::io_context client_io;
    boost::asio::ip::tcp::socket socket(client_io);
    socket.connect(boost::asio::ip::tcp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), 33333));
    std::string sendMsg = "안녕하세요, 에코 테스트!";
    socket.send(boost::asio::buffer(sendMsg));

    char recvBuf[100];
    size_t len = socket.receive(boost::asio::buffer(recvBuf));

    // Assert
    std::string received(recvBuf, len);
    EXPECT_EQ(received, sendMsg);

    // Cleanup
    socket.close();
    io_context.stop();
    serverThread.join();
}
