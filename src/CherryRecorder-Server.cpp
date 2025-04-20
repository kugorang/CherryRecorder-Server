#include "CherryRecorder-Server.hpp"
#include <memory>
#include <cstdio> // fprintf 사용 위해 포함
#include <cctype> // isprint 사용 위해 포함


/**
 * @file CherryRecorder-Server.cpp
 * @brief CherryRecorder 서버의 TCP 에코 서버 기능을 구현한 소스 파일.
 *
 * `EchoServer` 클래스와 내부적으로 사용되는 `Session` 클래스의 구현을 포함한다.
 * 전체 프로젝트는 TCP 에코 서버, HTTP 서버(Google Places API 연동), 
 * 그리고 웹소켓 기반 채팅 서버로 구성되며, 이 파일은 TCP 에코 서버 부분을 담당한다.
 * @see EchoServer, Session, HttpServer, ChatServer
 */

 /**
  * @class Session
  * @brief 한 클라이언트의 TCP 에코 기능을 담당하는 세션 클래스.
  *
  * `std::enable_shared_from_this`를 상속하여 비동기 작업 중 객체 수명을 관리한다.
  * 클라이언트로부터 데이터를 비동기로 수신하고, 받은 데이터를 동일하게 다시 클라이언트에게 전송한다.
  * 상세한 로깅을 통해 데이터 송수신 및 오류 상황을 추적한다.
  */
class Session : public std::enable_shared_from_this<Session> {
public:
	/**
	 * @brief Session 생성자.
	 * @param socket 클라이언트와 연결된 TCP 소켓. 소유권이 Session 객체로 이동된다.
	 */
	explicit Session(boost::asio::ip::tcp::socket socket)
		: socket_(std::move(socket)) {
		try {
			fprintf(stdout, "[Session %p] Created for endpoint %s:%d\n",
				(void*)this, // Session 객체의 메모리 주소 로깅
				socket_.remote_endpoint().address().to_string().c_str(),
				socket_.remote_endpoint().port());
		}
		catch (const std::exception& e) { // 구체적인 예외 타입 명시
			fprintf(stderr, "[Session %p] Created, but failed to get remote endpoint info: %s\n", (void*)this, e.what());
		}
	}

	/**
	 * @brief Session 소멸자.
	 *
	 * 세션 객체가 소멸될 때 로그를 남긴다. 소켓은 RAII 패턴에 의해 자동으로 닫힌다.
	 */
	~Session() {
		fprintf(stdout, "[Session %p] Destroyed.\n", (void*)this);
	}

	/**
	 * @brief 세션을 시작하여 클라이언트로부터 데이터 읽기를 시작한다.
	 *
	 * 내부적으로 `doRead()`를 호출한다. `EchoServer`에서 연결 수락 후 호출된다.
	 */
	void start() {
		doRead();
	}

private:
	/**
	 * @brief 클라이언트로부터 데이터를 비동기적으로 읽어온다.
	 *
	 * `socket_.async_read_some`을 사용하여 데이터를 `data_` 버퍼로 읽고, 완료 시 콜백 함수를 실행한다.
	 * `shared_from_this()`를 사용하여 콜백 실행 중 `Session` 객체가 유효함을 보장한다.
	 * 성공적으로 데이터를 읽으면 `doWrite()`를 호출하여 에코하고,
	 * 오류 발생(EOF 포함) 시 세션은 자동으로 종료된다 (소멸자가 호출됨).
	 */
	void doRead() {
		auto self = shared_from_this(); // 비동기 작업 중 객체 생존 보장
		fprintf(stdout, "[Session %p] Waiting to read...\n", (void*)this);
		socket_.async_read_some(boost::asio::buffer(data_),
			// 람다 캡처: this(Session 객체 포인터), self(shared_ptr)
			[this, self](boost::system::error_code ec, std::size_t length) {
				if (!ec) { // 오류 없이 읽기 성공
					fprintf(stdout, "[Session %p] Read successful: %zu bytes.\n", (void*)this, length);

					// 수신 데이터 로깅 (16진수 및 가능하면 문자)
					if (length > 0 && length <= max_length) { // 버퍼 범위 재확인
						fprintf(stdout, "[Session %p] Received data (hex):", (void*)this);
						for (size_t i = 0; i < length; ++i) {
							// data_가 char 타입이므로 unsigned char로 캐스팅하여 %02X 출력
							fprintf(stdout, " %02X", static_cast<unsigned char>(data_[i]));
						}
						fprintf(stdout, "\n");
						// 단일 바이트이고 출력 가능한 문자인 경우 문자 로깅 추가
						if (length == 1 && isprint(static_cast<unsigned char>(data_[0]))) {
							fprintf(stdout, "[Session %p] Received char: '%c'\n", (void*)this, data_[0]);
						}
					}

					if (length == 0) { // 0 바이트 수신 (예외적인 경우)
						fprintf(stdout, "[Session %p] Received 0 bytes. Will write 0 bytes back.\n", (void*)this);
					}
					// 읽은 데이터를 다시 클라이언트에게 쓴다 (에코)
					doWrite(length);
				}
				else { // 읽기 중 오류 발생
					fprintf(stderr, "[Session %p] Read error: %s (%d).\n", (void*)this, ec.message().c_str(), ec.value());
					// EOF(End Of File)는 클라이언트가 연결을 정상 종료한 경우
					if (ec == boost::asio::error::eof) {
						fprintf(stdout, "[Session %p] Connection closed by peer (EOF).\n", (void*)this);
					} else if (ec == boost::asio::error::operation_aborted) {
                        fprintf(stdout, "[Session %p] Read operation aborted.\n", (void*)this);
                    }
					// 오류 발생 시 별도 close 호출 없이 Session 객체가 소멸되면서 소켓 정리됨
				}
			}
		);
	}

	/**
	 * @brief 수신된 데이터를 클라이언트에게 다시 비동기적으로 전송한다 (에코).
	 * @param length 전송할 데이터의 바이트 크기. `doRead`에서 받은 `length` 값.
	 *
	 * `boost::asio::async_write`를 사용하여 `data_` 버퍼의 `length` 바이트만큼 데이터를 전송하고,
	 * 완료 시 콜백 함수를 실행한다. `shared_from_this()`를 사용하여 콜백 실행 중 객체 유효성을 보장한다.
	 * 성공적으로 데이터를 전송하면 다시 `doRead()`를 호출하여 다음 데이터를 기다린다.
	 * 오류 발생 시 세션은 자동으로 종료된다.
	 */
	void doWrite(std::size_t length) {
		auto self = shared_from_this(); // 비동기 작업 중 객체 생존 보장
		fprintf(stdout, "[Session %p] Writing %zu bytes...\n", (void*)this, length);
		boost::asio::async_write(socket_,
			boost::asio::buffer(data_, length), // data_ 버퍼의 length 만큼 전송
			// 람다 캡처: this, self, 그리고 length 추가
			[this, self, length](boost::system::error_code ec, std::size_t bytes_sent) {
				if (!ec) { // 오류 없이 쓰기 성공
					// bytes_sent는 요청한 length와 일반적으로 동일해야 함
					if (bytes_sent != length) { // 이제 length 접근 가능
						fprintf(stderr, "[Session %p] Write warning: expected %zu bytes, sent %zu bytes.\n", (void*)this, length, bytes_sent); // 이제 length 접근 가능
					}
					fprintf(stdout, "[Session %p] Write successful: %zu bytes.\n", (void*)this, bytes_sent);
					// 쓰기 완료 후 다시 읽기 대기
					doRead();
				}
				else { // 쓰기 중 오류 발생
					fprintf(stderr, "[Session %p] Write error: %s (%d).\n", (void*)this, ec.message().c_str(), ec.value());
                    if (ec == boost::asio::error::operation_aborted) {
                        fprintf(stdout, "[Session %p] Write operation aborted.\n", (void*)this);
                    }
					// 오류 발생 시 별도 close 호출 없이 Session 객체가 소멸되면서 소켓 정리됨
				}
			}
		);
	}

	boost::asio::ip::tcp::socket socket_; ///< @brief 클라이언트와 통신하는 TCP 소켓.
	static constexpr size_t max_length = 1024; ///< @brief 데이터 수신 및 전송에 사용하는 버퍼의 최대 크기 (bytes).
	char data_[max_length]{}; ///< @brief 수신/전송에 사용하는 데이터 버퍼. 기본값으로 0 초기화.
};

// --- EchoServer 구현 ---

/**
 * @brief EchoServer 생성자 구현부.
 * @details acceptor를 열고, 재사용 옵션을 설정하고, 지정된 엔드포인트에 바인딩 후 리스닝을 시작한다.
 * 각 단계에서 오류 발생 시 예외를 던져 서버 초기화 실패를 알린다.
 */
EchoServer::EchoServer(boost::asio::io_context& io_context, unsigned short port)
	: io_context_(io_context),
	// acceptor_를 io_context와 연결하여 생성. 엔드포인트 설정은 아래에서 명시적 수행.
	acceptor_(io_context)
{
	boost::system::error_code ec;
	// 모든 로컬 주소(0.0.0.0)의 지정된 포트를 사용하는 IPv4 엔드포인트 생성
	boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port); 

	// 1. Acceptor 열기 (지정된 프로토콜 사용)
	acceptor_.open(endpoint.protocol(), ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Acceptor open error on port %d: %s\n", port, ec.message().c_str());
		// 생성자에서 예외 throw: 서버 객체 생성이 불가능함을 나타냄
		throw std::system_error{ ec, "[EchoServer] Acceptor open failed on port " + std::to_string(port) }; 
	}

	// 2. SO_REUSEADDR 소켓 옵션 설정 (서버 즉시 재시작 가능하도록)
	acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Set reuse_address option error on port %d: %s\n", port, ec.message().c_str());
		acceptor_.close(); // 실패 시 리소스 정리
		throw std::system_error{ ec, "[EchoServer] Set reuse_address failed on port " + std::to_string(port) };
	}
	else {
		fprintf(stdout, "[EchoServer %p] Set reuse_address option successfully for port %d.\n", (void*)this, port);
	}

	// 3. 지정된 로컬 엔드포인트에 바인딩
	acceptor_.bind(endpoint, ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Bind error on port %d: %s\n", port, ec.message().c_str());
		// SO_REUSEADDR 설정 후에도 바인딩 실패 가능 (예: 다른 프로세스가 이미 사용 중)
		acceptor_.close();
		throw std::system_error{ ec, "[EchoServer] Bind failed on port " + std::to_string(port) };
	}

	// 4. 리스닝 시작 (연결 요청 대기열 설정)
	acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
	if (ec) {
		fprintf(stderr, "[EchoServer Constructor] Listen error on port %d: %s\n", port, ec.message().c_str());
		acceptor_.close();
		throw std::system_error{ ec, "[EchoServer] Listen failed on port " + std::to_string(port) };
	}

	fprintf(stdout, "[EchoServer %p] Acceptor created and listening on port %d.\n", (void*)this, port);
}

/**
 * @brief EchoServer 소멸자 구현부.
 * @details acceptor가 열려있는 경우 안전하게 닫는다. 소멸자에서는 예외를 던지지 않는다.
 */
EchoServer::~EchoServer() {
	fprintf(stdout, "[EchoServer %p] Destructor called. Closing acceptor...\n", (void*)this);
	if (acceptor_.is_open()) {
		boost::system::error_code ec;
		acceptor_.close(ec); // Acceptor 명시적 닫기
		if (ec) {
			// 소멸자에서 예외를 던지는 것은 피해야 하므로, 오류 로깅만 수행
			fprintf(stderr, "[EchoServer Destructor] Acceptor close error: %s (%d)\n", ec.message().c_str(), ec.value());
		}
		else {
			fprintf(stdout, "[EchoServer %p] Acceptor closed.\n", (void*)this);
		}
	}
	else {
		fprintf(stdout, "[EchoServer %p] Acceptor was already closed.\n", (void*)this);
	}
}

/**
 * @brief 서버 시작 함수 구현부.
 * @details 첫 번째 비동기 accept 작업을 시작한다. io_context가 run 상태여야 작업이 진행된다.
 */
void EchoServer::start() {
	// 첫 번째 비동기 accept 작업을 시작한다. 이후 작업은 콜백 내에서 연쇄적으로 호출됨.
	fprintf(stdout, "[EchoServer %p] Starting accept loop...\n", (void*)this);
	doAccept();
}

/**
 * @brief 비동기 accept 함수 구현부.
 * @details 새로운 클라이언트 연결을 비동기적으로 기다린다. 연결 수락 시 Session 객체를 생성하고 시작시킨 후, 
 *          다음 연결을 기다리기 위해 재귀적으로 자신을 호출한다.
 */
void EchoServer::doAccept() {
	acceptor_.async_accept(
		// 람다 캡처: this (EchoServer 객체 포인터)
		[this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
			if (!ec) { // 연결 수락 성공
				try {
					fprintf(stdout, "[EchoServer %p] Accepted connection from %s:%d\n", 
						(void*)this,
						socket.remote_endpoint().address().to_string().c_str(),
						socket.remote_endpoint().port());
					// 새 연결이 들어오면 해당 소켓으로 Session 객체를 생성하고 start() 호출
					// std::make_shared 를 사용하여 Session 객체 관리 (RAII)
					std::make_shared<Session>(std::move(socket))->start();
				} catch (const std::exception& e) {
					fprintf(stderr, "[EchoServer %p] Error getting remote endpoint after accept: %s\n", (void*)this, e.what());
                    // 소켓은 Session 생성자에게 전달되지 않았으므로 여기서 닫힘
				}
			}
			else { // 연결 수락 실패
				fprintf(stderr, "[EchoServer %p] Accept error: %s (%d)\n", (void*)this, ec.message().c_str(), ec.value());
                if (ec == boost::asio::error::operation_aborted) {
                    fprintf(stdout, "[EchoServer %p] Accept operation aborted (likely server stopping).\n", (void*)this);
                    return; // 서버 중지 시에는 루프 종료
                }
				// 실패하더라도 서버가 중지되지 않도록 다음 연결 수락 시도
				// 단, 특정 오류(예: 리소스 부족) 발생 시 로깅 외 추가 처리 고려 가능
			}
			// 작업 성공/실패 여부와 관계없이 다음 연결 요청을 받기 위해 다시 doAccept() 호출
            // 서버가 중지되지 않는 한 계속 accept 시도
            if (acceptor_.is_open()) { // acceptor가 닫히지 않았을 때만 다음 accept 시도
			    doAccept();
            } else {
                fprintf(stdout, "[EchoServer %p] Acceptor is closed, stopping accept loop.\n", (void*)this);
            }
		}
	);
}

/**
 * @brief EchoServer 중지 메서드 구현부.
 * @details acceptor를 닫아 새 연결 수락을 중단한다.
 */
void EchoServer::stop() {
    fprintf(stdout, "[EchoServer %p] stop() called. Closing acceptor...", (void*)this);
    if (acceptor_.is_open()) {
        boost::system::error_code ec;
        acceptor_.close(ec);
        if (ec) {
            fprintf(stderr, "[EchoServer %p] Acceptor close error during stop: %s (%d)\n", (void*)this, ec.message().c_str(), ec.value());
        } else {
            fprintf(stdout, "[EchoServer %p] Acceptor closed successfully by stop().\n", (void*)this);
        }
    } else {
        fprintf(stdout, "[EchoServer %p] Acceptor already closed when stop() called.\n", (void*)this);
    }
    // 참고: 이 stop()은 진행 중인 Session들을 강제로 종료시키지는 않는다.
    // io_context가 stop()되면 Session의 비동기 작업들도 중단될 것이다.
}