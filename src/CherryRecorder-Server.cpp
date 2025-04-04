#include "CherryRecorder-Server.hpp"
#include <iostream>
#include <memory>
#include <boost/system/error_code.hpp> // error_code 사용 위해 포함
#include <boost/asio.hpp> // Boost.Asio 사용 위해 포함
#include <cstdio> // fprintf 사용 위해 포함


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
		try {
			fprintf(stdout, "[Session %p] Created for endpoint %s:%d\n",
				this,
				socket_.remote_endpoint().address().to_string().c_str(),
				socket_.remote_endpoint().port());
		}
		catch (...) { /* remote_endpoint() 실패 가능성 */ }
	}

	// 세션 소멸자 추가 (종료 확인용)
	~Session() {
		fprintf(stdout, "[Session %p] Destroyed.\n", this);
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
		fprintf(stdout, "[Session %p] Waiting to read...\n", this);
		socket_.async_read_some(boost::asio::buffer(data_),
			[this, self](boost::system::error_code ec, std::size_t length) {
				if (!ec) {
					fprintf(stdout, "[Session %p] Read successful: %zu bytes.\n", this, length);

					// --- 로그 추가 시작 ---
					if (length > 0 && length < sizeof(data_)) { // 버퍼 범위 확인
						fprintf(stdout, "[Session %p] Received data (hex):", this);
						for (size_t i = 0; i < length; ++i) {
							// 수신된 데이터를 16진수로 출력
							fprintf(stdout, " %02X", static_cast<unsigned char>(data_[i]));
						}
						fprintf(stdout, "\n");

						// 만약 1바이트이고 출력 가능한 문자라면 문자로도 출력 (디버깅 편의)
						if (length == 1 && isprint(data_[0])) {
							fprintf(stdout, "[Session %p] Received char: '%c'\n", this, data_[0]);
						}
					}
					// --- 로그 추가 끝 ---

					if (length == 0) {
						fprintf(stdout, "[Session %p] Received 0 bytes. Writing 0 bytes back.\n", this);
					}
					doWrite(length);
				}
				else {
					fprintf(stderr, "[Session %p] Read error: %s (%d).\n", this, ec.message().c_str(), ec.value());
					// 오류 발생 시 세션 종료됨 (별도 close 호출 불필요)
					// 만약 EOF 오류 시 정상 종료 로그를 남기고 싶다면:
					if (ec == boost::asio::error::eof) {
						fprintf(stdout, "[Session %p] Connection closed by peer (EOF).\n", this);
					}
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
		fprintf(stdout, "[Session %p] Writing %zu bytes...\n", this, length);
		boost::asio::async_write(socket_,
			boost::asio::buffer(data_, length),
			[this, self](boost::system::error_code ec, std::size_t bytes_sent) {
				if (!ec) {
					fprintf(stdout, "[Session %p] Write successful: %zu bytes.\n", this, bytes_sent);
					doRead(); // 다음 읽기 대기
				}
				else {
					fprintf(stderr, "[Session %p] Write error: %s (%d).\n", this, ec.message().c_str(), ec.value());
					// 오류 발생 시 세션 종료됨 (별도 close 호출 불필요)
				}
			}
		);
	}

	boost::asio::ip::tcp::socket socket_;	///< 클라이언트와 통신할 소켓
	char data_[1024]{};						///< 수신·전송에 사용하는 임시 버퍼
};

EchoServer::EchoServer(boost::asio::io_context& io_context, unsigned short port)
	: io_context_(io_context),
	// acceptor_를 생성만 하고, open/bind/listen은 아래에서 명시적으로 수행
	acceptor_(io_context)
	// 이전 코드: acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{
	boost::system::error_code ec;
	boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port); // 엔드포인트 생성

	// 1. Acceptor 열기
	acceptor_.open(endpoint.protocol(), ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Acceptor open error: %s\n", ec.message().c_str());
		throw std::system_error{ ec, "Acceptor open failed" }; // 생성자에서 예외 throw
	}

	// --- 2. SO_REUSEADDR 옵션 설정 (핵심 수정) ---
	acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Set reuse_address option error: %s\n", ec.message().c_str());
		acceptor_.close(); // 실패 시 acceptor 닫기
		throw std::system_error{ ec, "Set reuse_address failed" };
	}
	else {
		fprintf(stdout, "[EchoServer %p] Set reuse_address option successfully.\n", this);
	}
	// --- 옵션 설정 끝 ---

	// 3. 엔드포인트에 바인딩
	acceptor_.bind(endpoint, ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Bind error: %s\n", ec.message().c_str());
		acceptor_.close();
		throw std::system_error{ ec, "Bind failed" };
	}

	// 4. 리스닝 시작
	acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Listen error: %s\n", ec.message().c_str());
		acceptor_.close();
		throw std::system_error{ ec, "Listen failed" };
	}

	fprintf(stdout, "[EchoServer %p] Acceptor created and listening on port %d.\n", this, port);
}

EchoServer::~EchoServer() {
	fprintf(stdout, "[EchoServer %p] Destructor called. Closing acceptor...\n", this);
	if (acceptor_.is_open()) {
		boost::system::error_code ec;
		acceptor_.close(ec); // 명시적 닫기
		if (ec) {
			fprintf(stderr, "[EchoServer Destructor] Acceptor close error: %s\n", ec.message().c_str());
		}
		else {
			fprintf(stdout, "[EchoServer %p] Acceptor closed.\n", this);
		}
	}
	else {
		fprintf(stdout, "[EchoServer %p] Acceptor was already closed.\n", this);
	}
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
