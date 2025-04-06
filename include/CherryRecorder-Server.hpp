#pragma once

#include <boost/asio.hpp>
#include <string>

/**
 * @file CherryRecorder-Server.hpp
 *
 * @brief CherryRecorder 서버의 TCP 에코 서버 기능을 정의한 헤더 파일.
 *
 * 이 헤더는 Boost.Asio를 사용한 TCP Echo 서버 클래스(`EchoServer`)를 정의한다.
 * 클라이언트 연결을 수락하고, 각 연결에 대해 데이터를 에코하는 세션(내부 `Session` 클래스, `CherryRecorder-Server.cpp`에 정의됨)을 생성한다.
 * @see EchoServer, Session
 */

 /**
  * @class EchoServer
  * @brief Boost.Asio를 이용한 TCP 에코 서버 클래스.
  *
  * 지정된 포트 번호로 리슨(listen)하여 클라이언트의 연결을 받고,
  * 받은 데이터를 그대로 다시 돌려주는 Echo 기능을 제공한다.
  * SO_REUSEADDR 옵션을 사용하여 서버 재시작 시 'Address already in use' 오류를 방지한다.
  * 각 클라이언트 연결은 별도의 `Session` 객체(`CherryRecorder-Server.cpp`에 정의됨)에 의해 비동기적으로 처리된다.
  */
class EchoServer {
public:
    /**
     * @brief EchoServer 생성자.
     *
     * Acceptor를 생성하고, 지정된 포트에 바인딩하며, 리스닝을 시작한다.
     * SO_REUSEADDR 소켓 옵션을 설정한다.
     * @param io_context Boost.Asio io_context 객체 참조. 비동기 I/O 작업을 처리한다.
     * @param port 리슨할 TCP 포트 번호.
     * @throw std::system_error Acceptor 열기, 옵션 설정, 바인딩, 리스닝 중 오류 발생 시.
     */
    EchoServer(boost::asio::io_context& io_context, unsigned short port);

    /**
     * @brief EchoServer 소멸자.
     *
     * 서버가 소멸될 때 acceptor가 열려 있으면 명시적으로 닫는다.
     * 관련 리소스 정리를 담당한다.
     */
    ~EchoServer();

    /**
     * @brief 서버를 시작하여 클라이언트 연결을 비동기적으로 수락한다.
     *
     * 내부적으로 `doAccept()`를 호출하여 첫 번째 비동기 accept 작업을 시작한다.
     * 이 메서드 호출 후 `io_context.run()`이 실행되어야 실제 연결 수락이 이루어진다.
     */
    void start();

private:
    /**
     * @brief 비동기적으로 클라이언트 연결을 대기하고 수락한다.
     *
     * 연결이 성공적으로 수락되면, 해당 연결을 위한 `Session` 객체를 생성하고 `start()`를 호출한다.
     * 그리고 다음 연결을 받기 위해 다시 `async_accept`를 호출한다.
     * 오류 발생 시에도 다음 연결 수락을 계속 시도한다.
     */
    void doAccept();

    boost::asio::io_context& io_context_;       ///< @brief 비동기 작업을 위한 Boost.Asio io_context 참조.
    boost::asio::ip::tcp::acceptor acceptor_;   ///< @brief 클라이언트 연결 요청을 수락하는 TCP acceptor.
};