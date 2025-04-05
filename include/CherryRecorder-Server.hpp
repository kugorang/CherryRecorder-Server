#pragma once

#include <boost/asio.hpp>
#include <string>

/**
 * @file CherryRecorder-Server.hpp
 *
 * @brief CherryRecorder 서버의 주요 기능을 정의한 헤더 파일.
 */

/**
 * @class EchoServer
 * @brief Boost.Asio를 이용한 간단한 TCP 에코 서버 클래스.
 *
 * 지정된 포트 번호로 리슨(listen)하여 클라이언트의 연결을 받고,
 * 받은 데이터를 그대로 다시 돌려주는 Echo 기능을 제공합니다.
 */
class EchoServer {
public:
    /**
     * @brief EchoServer 생성자
     * @param io_context Boost.Asio io_context 객체 참조
     * @param port 리슨할 TCP 포트 번호
     */
    EchoServer(boost::asio::io_context& io_context, unsigned short port);

	~EchoServer();

    /**
     * @brief 서버를 시작하여 클라이언트 연결을 비동기적으로 수락합니다.
     *
     * 이 메서드를 호출하면 acceptor가 작동하며,
     * 연결이 생성될 때마다 세션(Session)을 생성하여 에코를 처리합니다.
     */
    void start();

private:
    /**
     * @brief 내부적으로 async_accept를 호출하여 클라이언트 연결을 대기합니다.
     */
    void doAccept();

    boost::asio::io_context& io_context_;       ///< io_context 참조
    boost::asio::ip::tcp::acceptor acceptor_;   ///< 클라이언트 연결을 수락하는 TCP acceptor
};
