#include "CherryRecorder-Server.hpp"
#include <iostream>
#include <memory>

/**
 * @file CherryRecorder-Server.cpp
 * 
 * @brief CherryRecorder 서버의 주요 기능을 정의한 소스 파일.
 */

/**
 * @class Session
 * @brief 한 클라이언트의 에코 기능을 담당하는 세션 클래스.
 *
 * 클라이언트로부터 데이터를 비동기로 수신하고,
 * 받은 데이터를 동일하게 다시 클라이언트에게 전송합니다.
 */
class Session : public std::enable_shared_from_this<Session> {
public:
	/**
	 * @brief Session 생성자
	 * @param socket 클라이언트와 통신할 TCP 소켓
	 */
	explicit Session(boost::asio::ip::tcp::socket socket)
		: socket_(std::move(socket)) {
	}

	/**
	 * @brief 세션을 시작하여 클라이언트와의 송수신을 처리합니다.
	 */
	void start() {
		doRead();
	}

private:
	/**
	 * @brief 클라이언트로부터 데이터를 비동기적으로 읽어옵니다.
	 */
	void doRead() {
		auto self = shared_from_this();
		socket_.async_read_some(boost::asio::buffer(data_),
			[this, self](boost::system::error_code ec, std::size_t length) {
				if (!ec) {
					doWrite(length);
				}
			}
		);
	}

	/**
	 * @brief 수신된 데이터를 클라이언트에게 다시 돌려줍니다(에코).
	 * @param length 수신한 데이터의 바이트 크기
	 */
	void doWrite(std::size_t length) {
		auto self = shared_from_this();
		boost::asio::async_write(socket_,
			boost::asio::buffer(data_, length),
			[this, self](boost::system::error_code ec, std::size_t /*bytes_sent*/) {
				if (!ec) {
					// 전송이 끝나면 다시 읽기 대기
					doRead();
				}
			}
		);
	}

	boost::asio::ip::tcp::socket socket_;	///< 클라이언트와 통신할 소켓
	char data_[1024]{};						///< 수신·전송에 사용하는 임시 버퍼
};

EchoServer::EchoServer(boost::asio::io_context& io_context, unsigned short port)
	: io_context_(io_context),
	acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{
}

void EchoServer::start() {
	doAccept();
}

void EchoServer::doAccept() {
	acceptor_.async_accept(
		[this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
			if (!ec) {
				// 새 연결이 들어오면 세션(Session) 객체를 생성해 에코 기능 담당
				std::make_shared<Session>(std::move(socket))->start();
			}
			// 다음 연결도 계속 수락
			doAccept();
		}
	);
}
