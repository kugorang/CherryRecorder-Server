#include "CherryRecorder-Server.hpp"
#include <boost/asio.hpp>
#include <iostream>

/**
 * @file main.cpp
 * 
 * @brief CherryRecorder 서버의 메인 함수를 정의한 소스 파일.
 */

/**
 * @brief 메인 함수: EchoServer를 특정 포트에서 실행합니다.
 */
int main() {
    try {
        boost::asio::io_context io_context;
        EchoServer server(io_context, 12345);
        server.start();

        std::cout << "에코 서버가 포트 12345에서 동작 중입니다..." << std::endl;
        io_context.run();
    }
    catch (const std::exception& e) {
        std::cerr << "서버 실행 오류: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
