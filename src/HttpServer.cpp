#include "HttpServer.hpp"
#include <boost/beast/version.hpp>
#include <boost/config.hpp> // BOOST_ASSERT 사용
#include <boost/asio/dispatch.hpp> // net::dispatch 사용
#include <boost/beast/http/write.hpp> // http::async_write 사용
#include <boost/beast/http/read.hpp> // http::async_read 사용
#include <boost/core/ignore_unused.hpp> // boost::ignore_unused 사용

#include <chrono>
#include <exception> // std::terminate, std::exception
#include <iostream>
#include <memory>
#include <thread> // std::thread
#include <vector>
#include <string> // std::string 사용
#include <cstdio> // fprintf 사용
#include <utility> // std::move 사용
#include <sstream> // std::stringstream (스레드 ID 로깅용)

// 필요한 네임스페이스 정의 (HttpServer.hpp 와 동일하게)
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @file HttpServer.cpp
 * @brief Boost.Beast를 이용한 비동기 HTTP 서버 관련 클래스 구현 파일.
 *
 * `HttpSession`, `HttpListener`, `HttpServer` 클래스의 실제 구현을 포함한다.
 * `HttpSession`은 개별 클라이언트 연결의 HTTP 요청/응답 처리를 담당한다.
 * 현재는 `/health` 경로에 대한 GET 요청만 처리하며, 그 외 경로는 404 Not Found를 반환한다.
 * @see HttpSession, HttpListener, HttpServer
 */

 /**
  * @class HttpSession
  * @brief 개별 HTTP 클라이언트 연결을 처리하는 세션 클래스.
  * @details `std::enable_shared_from_this`를 상속하여 비동기 작업 중 객체 생존을 보장한다.
  * Beast의 비동기 작업을 사용하여 요청을 읽고 응답을 보낸다.
  * 현재 `/health` 엔드포인트를 지원한다.
  */
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
    beast::tcp_stream stream_; ///< @brief TCP 소켓을 감싸는 Beast 스트림 객체. 비동기 I/O 작업을 수행한다.
    beast::flat_buffer buffer_; ///< @brief HTTP 메시지 읽기/쓰기를 위한 버퍼.
    std::shared_ptr<std::string const> doc_root_; ///< @brief (옵션) 정적 파일 루트 디렉토리 경로 공유 포인터. 현재 사용되지 않음.
    http::request<http::string_body> req_; ///< @brief 수신한 HTTP 요청 메시지 객체. 본문은 문자열로 저장.

public:
    /**
     * @brief HttpSession 생성자.
     * @param socket 클라이언트와 연결된 TCP 소켓. 소유권이 이동된다.
     * @param doc_root 정적 파일 루트 디렉토리 경로 공유 포인터.
     */
    explicit HttpSession(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root)
        : stream_(std::move(socket)), doc_root_(doc_root) {
        fprintf(stdout, "[HttpSession %p] Created.\n", (void*)this);
    }

    /**
    * @brief HttpSession 소멸자.
    *
    * 세션 객체가 소멸될 때 로그를 남긴다. 스트림과 소켓은 자동으로 정리된다.
    */
    ~HttpSession() {
        fprintf(stdout, "[HttpSession %p] Destroyed.\n", (void*)this);
    }

    /**
     * @brief 세션을 시작하여 비동기적으로 HTTP 요청 읽기를 시작한다.
     *
     * `net::dispatch`를 사용하여 `do_read()` 메서드를 스트림의 실행자(executor) 컨텍스트에서 실행하도록 예약한다.
     */
    void run() {
        // I/O 작업이 스트림의 실행자(기본적으로 소켓의 io_context와 동일한 strand 또는 스레드)에서 실행되도록 보장
        net::dispatch(stream_.get_executor(),
            // bind_front_handler: 멤버 함수 호출 시 첫 번째 인자로 this (shared_ptr)를 자동으로 전달
            beast::bind_front_handler(&HttpSession::on_run_dispatched, shared_from_this()));
    }

private:
    /**
    * @brief `run()`에서 `net::dispatch`된 후 실제 실행되는 함수.
    *
    * 첫 번째 `do_read()`를 호출하여 요청 읽기 프로세스를 시작한다.
    */
    void on_run_dispatched() {
        fprintf(stdout, "[HttpSession %p] Starting read loop.\n", (void*)this);
        do_read();
    }

    /**
     * @brief 비동기적으로 HTTP 요청 메시지를 읽는다.
     *
     * 요청 객체(`req_`)를 초기화하고, 스트림 타임아웃을 설정한 후,
     * `http::async_read`를 호출하여 요청 헤더와 본문을 비동기적으로 읽는다.
     * 완료 시 `on_read` 콜백 함수가 호출된다.
     */
    void do_read() {
        // 새 요청을 위해 파서(요청 객체) 초기화
        req_ = {};

        // 읽기 타임아웃 설정 (Beast 권장). 30초 동안 데이터 수신 없으면 타임아웃.
        stream_.expires_after(std::chrono::seconds(30));

        // 비동기적으로 요청 읽기 시작
        fprintf(stdout, "[HttpSession %p] Waiting to read request...\n", (void*)this);
        http::async_read(stream_, buffer_, req_,
            // 완료 시 on_read 호출. shared_from_this()로 객체 생존 보장.
            beast::bind_front_handler(&HttpSession::on_read, shared_from_this()));
    }

    /**
     * @brief 비동기 읽기 작업 완료 시 호출되는 콜백 함수.
     * @param ec 작업 결과 에러 코드.
     * @param bytes_transferred 전송된 바이트 수 (현재 사용 안 함).
     *
     * 에러를 확인하고, 정상 수신 시 요청 경로(`/health` 또는 기타)에 따라
     * 적절한 핸들러 함수(`handle_health_check_request` 또는 `handle_not_found_request`)를 호출한다.
     * 오류 발생 시 또는 클라이언트 연결 종료 시(`http::error::end_of_stream`) `do_close`를 호출하여 세션을 종료한다.
     */
    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred); // bytes_transferred 사용하지 않으므로 경고 방지

        // 클라이언트가 연결을 정상적으로 종료한 경우 (Keep-Alive 아닐 때 다음 요청 전에 발생 가능)
        if (ec == http::error::end_of_stream) {
            fprintf(stdout, "[HttpSession %p] Read finished: End of stream.\n", (void*)this);
            return do_close();
        }
        // 그 외 읽기 오류 (타임아웃 포함)
        if (ec) {
            fprintf(stderr, "[HttpSession %p] Read error: %s\n", (void*)this, ec.message().c_str());
            return do_close();
        }

        // 읽기 성공, 요청 처리 시작
        fprintf(stdout, "[HttpSession %p] Read successful. Request: %s %s\n", (void*)this,
            std::string(http::to_string(req_.method())).c_str(), std::string(req_.target()).c_str());

        // ----- 요청 라우팅 -----
        if (req_.method() == http::verb::get && req_.target() == "/health") {
            handle_health_check_request(); // Health Check 요청 처리
        }
        else {
            // 다른 모든 요청은 404 Not Found 처리
            handle_not_found_request();
            // TODO: 실제 서비스 로직(정적 파일 제공, API 처리 등) 추가 구현 필요
            // 예: handle_static_file_request(req_.target());
        }
    }

    /**
     * @brief `/health` 경로에 대한 GET 요청을 처리한다.
     *
     * 상태 코드 200 OK와 "OK" 문자열 본문을 포함하는 HTTP 응답을 생성하고 `send_response`를 통해 전송한다.
     */
    void handle_health_check_request() {
        fprintf(stdout, "[HttpSession %p] Handling /health request.\n", (void*)this);
        // 응답 객체 생성 (상태코드 OK, 요청과 동일한 HTTP 버전 사용)
        http::response<http::string_body> res{ http::status::ok, req_.version() };
        // 응답 헤더 설정
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING); // Server 헤더
        res.set(http::field::content_type, "text/plain");         // Content-Type 헤더
        res.keep_alive(req_.keep_alive());                       // 요청의 Keep-Alive 설정 따름
        // 응답 본문 설정
        res.body() = "OK";
        // 응답 본문 길이에 맞춰 Content-Length 헤더 등 자동 계산 및 설정
        res.prepare_payload();
        // 생성된 응답 전송
        send_response(std::move(res));
    }

    /**
     * @brief 정의되지 않은 경로에 대한 요청을 처리한다.
     *
     * 상태 코드 404 Not Found와 에러 메시지 본문을 포함하는 HTTP 응답을 생성하고 `send_response`를 통해 전송한다.
     */
    void handle_not_found_request() {
        fprintf(stdout, "[HttpSession %p] Handling unknown request (404 Not Found).\n", (void*)this);
        // 응답 객체 생성 (상태코드 Not Found, 요청과 동일한 HTTP 버전)
        http::response<http::string_body> res{ http::status::not_found, req_.version() };
        // 응답 헤더 설정
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req_.keep_alive());
        // 응답 본문 설정
        res.body() = "The resource '" + std::string(req_.target()) + "' was not found.";
        // 응답 본문 길이에 맞춰 Content-Length 헤더 등 자동 계산 및 설정
        res.prepare_payload();
        // 생성된 응답 전송
        send_response(std::move(res));
    }

    /**
     * @brief 생성된 HTTP 응답 메시지를 클라이언트에게 비동기적으로 전송한다.
     * @param res 전송할 HTTP 응답 객체 (`http::response<http::string_body>`). 소유권이 이동된다.
     *
     * 응답 객체를 `std::shared_ptr`로 감싸 비동기 쓰기 작업 중 생존을 보장한다.
     * `http::async_write`를 호출하여 응답을 전송하고, 완료 시 `on_write` 콜백 함수가 호출된다.
     */
    void send_response(http::response<http::string_body>&& res) { // rvalue reference로 받아 move 사용
        // 응답 객체의 수명을 비동기 작업 완료까지 연장하기 위해 shared_ptr 사용
        auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));

        // 쓰기 타임아웃 설정 (Beast 권장). 30초.
        stream_.expires_after(std::chrono::seconds(30));

        fprintf(stdout, "[HttpSession %p] Writing response (Status: %d)...\n", (void*)this, sp->result_int());
        // 비동기적으로 응답 쓰기 시작
        http::async_write(stream_, *sp, // shared_ptr이 가리키는 응답 객체 전달
            // 완료 시 on_write 호출. shared_ptr(sp)와 shared_from_this()(self) 캡처로 생존 보장.
            [self = shared_from_this(), sp](beast::error_code ec, std::size_t bytes_transferred) {
                // on_write 콜백에서 응답 객체 정보(예: need_eof) 접근 가능
                self->on_write(sp->need_eof(), ec, bytes_transferred);
            });
    }

    /**
     * @brief 비동기 쓰기 작업 완료 시 호출되는 콜백 함수.
     * @param close 연결 종료 필요 여부. 응답 객체의 `need_eof()` 결과. (HTTP/1.0 또는 Connection: close 헤더)
     * @param ec 작업 결과 에러 코드.
     * @param bytes_transferred 전송된 바이트 수 (현재 사용 안 함).
     *
     * 에러를 확인하고, 오류 발생 시 `do_close`를 호출한다.
     * 오류가 없고 `close` 플래그가 true이면 `do_close`를 호출한다.
     * 오류가 없고 `close` 플래그가 false (Keep-Alive)이면 `do_read`를 호출하여 다음 요청을 기다린다.
     */
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) { // 쓰기 오류 발생
            fprintf(stderr, "[HttpSession %p] Write error: %s (%d).\n", (void*)this, ec.message().c_str(), ec.value());
            return do_close(); // 오류 시 연결 종료
        }

        fprintf(stdout, "[HttpSession %p] Write successful (%zu bytes).\n", (void*)this, bytes_transferred);

        if (close) {
            // HTTP/1.0 이거나 응답에 Connection: close 헤더가 있으면 연결 종료
            fprintf(stdout, "[HttpSession %p] Closing connection after write (need_eof is true).\n", (void*)this);
            return do_close();
        }

        // Keep-Alive 연결이면, 다음 요청을 읽기 위해 do_read() 호출
        fprintf(stdout, "[HttpSession %p] Keep-Alive connection, waiting for next request.\n", (void*)this);
        do_read();
    }

    /**
     * @brief 소켓 연결을 정상적으로 종료한다.
     *
     * TCP 연결의 전송(send) 방향을 먼저 닫고, 스트림 객체가 소멸될 때 소켓이 완전히 닫히도록 한다.
     * (Beast::tcp_stream 소멸자가 내부적으로 close 호출)
     */
    void do_close() {
        fprintf(stdout, "[HttpSession %p] Closing connection.\n", (void*)this);
        beast::error_code ec;
        // 전송 방향 셧다운 시도. 오류 발생 가능성 있음 (이미 닫혔거나 등)
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        if (ec) {
            fprintf(stderr, "[HttpSession %p] Shutdown(send) warning: %s\n", (void*)this, ec.message().c_str());
        }
        // 스트림/소켓의 close는 HttpSession 객체가 소멸될 때 자동으로 처리됨.
        // 명시적 close가 필요하다면 여기에 추가. stream_.close(ec);
    }
};

// --- HttpListener 구현 ---

/**
 * @brief HttpListener 생성자 구현부.
 */
HttpListener::HttpListener(
    net::io_context& ioc,
    tcp::endpoint endpoint,
    std::shared_ptr<std::string const> const& doc_root)
    : ioc_(ioc)
    , acceptor_(net::make_strand(ioc)) // Acceptor는 strand 위에서 동작하도록 생성 (스레드 안전성 강화)
    , doc_root_(doc_root)
{
    beast::error_code ec;

    // 1. Acceptor 열기 (지정된 프로토콜 사용)
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        fprintf(stderr, "[HttpListener %p] Acceptor open error: %s\n", (void*)this, ec.message().c_str());
        // 생성자에서 예외 던지기 또는 상태 플래그 설정 고려
        return; // 또는 throw
    }

    // 2. SO_REUSEADDR 소켓 옵션 설정
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        fprintf(stderr, "[HttpListener %p] Set reuse_address option error: %s\n", (void*)this, ec.message().c_str());
        acceptor_.close(); // 실패 시 리소스 정리
        return; // 또는 throw
    }

    // 3. 지정된 로컬 엔드포인트에 바인딩
    acceptor_.bind(endpoint, ec);
    if (ec) {
        fprintf(stderr, "[HttpListener %p] Bind error to %s:%d : %s\n", (void*)this,
            endpoint.address().to_string().c_str(), endpoint.port(), ec.message().c_str());
        acceptor_.close();
        return; // 또는 throw
    }

    // 4. 리스닝 시작
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        fprintf(stderr, "[HttpListener %p] Listen error: %s\n", (void*)this, ec.message().c_str());
        acceptor_.close();
        return; // 또는 throw
    }
    fprintf(stdout, "[HttpListener %p] Listening on %s:%d\n", (void*)this,
        endpoint.address().to_string().c_str(), endpoint.port());
}

/**
 * @brief 리스너 실행 함수 구현부.
 */
void HttpListener::run() {
    fprintf(stdout, "[HttpListener %p] Starting accept loop...\n", (void*)this);
    // 첫 번째 비동기 accept 호출 시작
    do_accept();
}

/**
 * @brief 비동기 accept 함수 구현부.
 */
void HttpListener::do_accept() {
    // 다음 연결 요청을 비동기적으로 기다린다.
    // 새 연결에 대한 소켓 생성 및 accept 작업은 지정된 strand(acceptor_ 생성 시 지정)에서 실행됨.
    acceptor_.async_accept(
        net::make_strand(ioc_), // 새 연결 소켓도 별도의 strand에서 처리하는 것이 일반적 (동시성 관리)
        // 완료 시 on_accept 호출. shared_from_this()로 HttpListener 객체 생존 보장.
        beast::bind_front_handler(&HttpListener::on_accept, shared_from_this()));
}

/**
 * @brief 비동기 accept 완료 콜백 함수 구현부.
 */
void HttpListener::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) { // Accept 중 오류 발생
        fprintf(stderr, "[HttpListener %p] Accept error: %s\n", (void*)this, ec.message().c_str());
        // 오류 처리 로직 (예: 특정 오류 코드에 따라 리스너 중지 또는 로깅 후 계속)
        // 여기서는 일단 로깅만 하고 다음 accept 시도
    }
    else { // Accept 성공
        try {
            fprintf(stdout, "[HttpListener %p] Accepted connection from %s:%d\n", (void*)this,
                socket.remote_endpoint().address().to_string().c_str(),
                socket.remote_endpoint().port());
        }
        catch (...) { /* remote_endpoint 실패 가능성 */
            fprintf(stdout, "[HttpListener %p] Accepted connection (remote endpoint info unavailable).\n", (void*)this);
        }

        // 새 연결에 대한 HttpSession 객체 생성 및 실행
        // std::move(socket)으로 소켓 소유권 이전
        std::make_shared<HttpSession>(std::move(socket), doc_root_)->run();
    }

    // 오류 발생 여부와 관계없이 다음 연결 수락 준비 (리스너가 중지되지 않는 한 계속)
    // TODO: 리스너 중지 로직 필요 시 추가 (예: 특정 오류 발생 시 또는 외부 신호 수신 시)
    do_accept();
}

// --- HttpServer 구현 ---

/**
 * @brief HttpServer 생성자 구현부.
 */
HttpServer::HttpServer(const std::string& address, unsigned short port, int threads, const std::string& doc_root)
    : address_(address)
    , port_(port)
    , threads_(threads)
    , doc_root_(doc_root)
    // io_context 생성 시 스레드 수(concurrency hint) 지정. 1 이상이어야 함.
    , ioc_(threads_ > 0 ? threads_ : 1)
    , shared_doc_root_(std::make_shared<std::string const>(doc_root_)) // doc_root 문자열 복사하여 공유 포인터 생성
{
    fprintf(stdout, "[HttpServer %p] Created with address=%s, port=%d, threads=%d, doc_root=%s\n",
        (void*)this, address_.c_str(), port_, threads_, doc_root_.c_str());
}

/**
 * @brief 서버 실행 함수 구현부.
 */
void HttpServer::run() {
    auto const addr = net::ip::make_address(address_); // 문자열 주소를 Asio 주소 객체로 변환
    auto const port = static_cast<unsigned short>(port_);
    int num_threads = threads_; // 사용할 실제 스레드 수

    // 스레드 수 결정 (0 이하면 시스템 코어 수 기반으로 결정)
    if (num_threads <= 0) {
        unsigned int hardware_threads = std::thread::hardware_concurrency();
        // hardware_concurrency()가 0 또는 예상치 못한 값 반환 시 기본값(예: 2) 사용
        num_threads = (hardware_threads > 0) ? static_cast<int>(hardware_threads) : 2;
        fprintf(stdout, "[HttpServer %p] Thread count not specified or invalid, using system hardware concurrency: %d\n", (void*)this, num_threads);
    }
    // 실제 io_context가 사용할 동시성 수준(생성 시 지정됨)과 다를 수 있으므로 정보 로깅
    fprintf(stdout, "[HttpServer %p] Starting on %s:%d with %d IO threads...\n",
        (void*)this, address_.c_str(), port, num_threads);


    // Listener 생성 및 실행 (io_context 및 엔드포인트 전달)
    try {
        listener_ = std::make_shared<HttpListener>(ioc_, tcp::endpoint{ addr, port }, shared_doc_root_);
        listener_->run(); // Listener의 비동기 accept 루프 시작
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[HttpServer %p] Failed to create or run listener: %s\n", (void*)this, e.what());
        // 리스너 생성 실패 시 서버 실행 불가 처리
        return; // 또는 예외 재throw
    }

    // IO 스레드 생성 및 io_context 실행 시작
    io_threads_.reserve(num_threads); // 벡터 메모리 미리 할당
    for (int i = 0; i < num_threads; ++i) {
        io_threads_.emplace_back([this, i] { // 각 스레드가 실행할 람다 함수
            std::stringstream thread_id_ss; // 로깅을 위한 스레드 ID 추출
            thread_id_ss << std::this_thread::get_id();
            std::string thread_id_str = thread_id_ss.str();
            try {
                fprintf(stdout, "[HTTP IO Thread #%d ID: %s] Starting ioc.run().\n", i, thread_id_str.c_str());
                // 각 스레드는 io_context의 이벤트 루프 실행 (블로킹 호출)
                // io_context가 중지되거나 모든 작업이 완료될 때까지 실행됨
                ioc_.run();
                fprintf(stdout, "[HTTP IO Thread #%d ID: %s] ioc.run() finished cleanly.\n", i, thread_id_str.c_str());
            }
            catch (const std::exception& e) {
                // io_context.run() 내부에서 발생한 예외 처리
                fprintf(stderr, "[HTTP IO Thread #%d ID: %s] Exception caught in ioc.run(): %s\n", i, thread_id_str.c_str(), e.what());
                // 필요 시 추가 에러 처리 (예: 서버 강제 종료)
                // std::terminate();
            }
            catch (...) {
                // 그 외 알 수 없는 예외 처리
                fprintf(stderr, "[HTTP IO Thread #%d ID: %s] Unknown exception caught in ioc.run().\n", i, thread_id_str.c_str());
                // std::terminate();
            }
            });
    }
    // run() 함수는 IO 스레드들을 시작시킨 후 반환됨. 메인 스레드는 다른 작업을 하거나 대기 상태로 들어감.
    fprintf(stdout, "[HttpServer %p] run() method finished, IO threads are running.\n", (void*)this);
}

/**
 * @brief 서버 중지 함수 구현부.
 */
void HttpServer::stop() {
    fprintf(stdout, "[HttpServer %p] Initiating stop sequence...\n", (void*)this);

    // 1. Acceptor 중지 (새 연결 수락 중단)
    // HttpListener에 acceptor를 닫는 메서드가 있다면 호출하는 것이 좋음.
    // 예: if (listener_) { listener_->stop_accepting(); }
    // 현재는 io_context 중지만으로 acceptor의 async_accept 콜백이 취소(operation_aborted)되도록 유도.
    // 명시적으로 acceptor_.cancel() 또는 acceptor_.close() 호출 고려 가능.
    // if (listener_) { /* listener->acceptor_.cancel(); or close(); */ }
    fprintf(stdout, "[HttpServer %p] Stopping listener (via io_context stop)...\n", (void*)this);


    // 2. io_context 중지 요청
    // 모든 IO 스레드의 ioc_.run() 호출이 반환되도록 함.
    // 아직 처리되지 않은 비동기 작업 핸들러는 실행되지 않을 수 있음 (즉시 중단 아님).
    fprintf(stdout, "[HttpServer %p] Requesting io_context stop (ioc_.stop())...\n", (void*)this);
    if (!ioc_.stopped()) {
        ioc_.stop();
        fprintf(stdout, "[HttpServer %p] io_context stop requested.\n", (void*)this);
    }
    else {
        fprintf(stdout, "[HttpServer %p] io_context was already stopped.\n", (void*)this);
    }

    // 3. 모든 IO 스레드 종료 대기
    fprintf(stdout, "[HttpServer %p] Waiting for %zu IO threads to join...\n", (void*)this, io_threads_.size());
    for (size_t i = 0; i < io_threads_.size(); ++i) {
        std::thread& t = io_threads_[i];
        std::stringstream ss; // 스레드 ID 로깅용
        ss << t.get_id();
        if (t.joinable()) {
            fprintf(stdout, "[HttpServer %p] Joining IO thread #%zu (ID: %s)...\n", (void*)this, i, ss.str().c_str());
            t.join(); // 해당 스레드가 완전히 종료될 때까지 대기
            fprintf(stdout, "[HttpServer %p] IO thread #%zu (ID: %s) joined.\n", (void*)this, i, ss.str().c_str());
        }
        else {
            // 이미 종료되었거나 시작되지 않은 스레드일 수 있음
            fprintf(stderr, "[HttpServer %p] Warning: IO thread #%zu (ID: %s) is not joinable (already joined or not started?).\n", (void*)this, i, ss.str().c_str());
        }
    }
    io_threads_.clear(); // 스레드 벡터 비우기
    fprintf(stdout, "[HttpServer %p] All HTTP IO threads finished.\n", (void*)this);

    // 4. 리소스 정리 (스레드 종료 후 안전하게 수행)
    fprintf(stdout, "[HttpServer %p] Resetting listener and shared resources...\n", (void*)this);
    if (listener_) {
        // Listener 소멸자에서 acceptor가 닫히도록 보장해야 함.
        listener_.reset(); // shared_ptr 참조 카운트 감소, 0이 되면 소멸자 호출
    }
    if (shared_doc_root_) {
        shared_doc_root_.reset();
    }

    fprintf(stdout, "[HttpServer %p] Stop sequence complete.\n", (void*)this);
}