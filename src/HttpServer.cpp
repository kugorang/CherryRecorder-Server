#include "HttpServer.hpp"
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <chrono>
#include <exception> // std::terminate
#include <iostream>
#include <memory>
#include <thread> // std::thread
#include <vector>

// 요청을 처리하는 세션 클래스
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
    // Asio I/O 작업을 위한 스트림 (TCP 소켓 사용)
    beast::tcp_stream stream_;
    // I/O 작업을 위한 버퍼
    beast::flat_buffer buffer_;
    // (옵션) 정적 파일 루트 디렉토리 공유 포인터
    std::shared_ptr<std::string const> doc_root_;
    // 수신한 요청 메시지 객체
    http::request<http::string_body> req_; // string_body 사용 예시

public:
    // 소켓 소유권을 가져와 스트림을 생성한다.
    explicit HttpSession(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root)
        : stream_(std::move(socket)), doc_root_(doc_root) {
    }

    // 세션 시작 (비동기 읽기 시작)
    void run() {
        // I/O 작업이 동일한 스레드에서 순차적으로 실행되도록 strand 사용 권장
        // 여기서는 간단화를 위해 직접 post 사용
        net::dispatch(stream_.get_executor(),
            beast::bind_front_handler(&HttpSession::do_read, shared_from_this()));
    }

private:
    // 비동기적으로 HTTP 요청 메시지를 읽는다.
    void do_read() {
        // 새 요청을 위해 파서 초기화
        req_ = {};

        // 읽기 타임아웃 설정 (Beast 권장)
        stream_.expires_after(std::chrono::seconds(30));

        // 비동기적으로 요청 읽기
        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(&HttpSession::on_read, shared_from_this()));
    }

    // 읽기 작업 완료 콜백
    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        // 타임아웃 등 읽기 오류 처리
        if (ec == http::error::end_of_stream) // 클라이언트가 연결 종료
            return do_close();
        if (ec) {
            fprintf(stderr, "[HTTP Session] Read error: %s\n", ec.message().c_str());
            return do_close();
        }

        // ----- Health Check 처리 -----
        if (req_.method() == http::verb::get && req_.target() == "/health") {
            handle_health_check_request();
        }
        else {
            // 다른 요청 처리 (예: 404 Not Found 또는 실제 앱 로직)
            handle_not_found_request();
            // 또는 정적 파일 처리 로직 등 추가
            // handle_static_file_request(req_.target());
        }
    }

    // Health Check 요청 처리 함수
    void handle_health_check_request() {
        http::response<http::string_body> res{ http::status::ok, req_.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req_.keep_alive());
        res.body() = "OK";
        res.prepare_payload(); // Content-Length 등 자동 계산
        send_response(std::move(res));
    }

    // 404 Not Found 처리 함수
    void handle_not_found_request() {
        http::response<http::string_body> res{ http::status::not_found, req_.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req_.keep_alive());
        res.body() = "The resource '" + std::string(req_.target()) + "' was not found.";
        res.prepare_payload();
        send_response(std::move(res));
    }

    // 생성된 응답 메시지를 전송하는 함수
    void send_response(http::response<http::string_body> res) { // 값 전달 (이동 발생)
        // 응답 객체를 shared_ptr로 관리하여 비동기 작업 동안 생존 보장
        auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
        // 세션 객체 생존 보장
        auto self = shared_from_this();

        // async_write에는 shared_ptr이 관리하는 객체의 참조를 전달
        http::async_write(stream_, *sp,
            // 람다에서 shared_ptr 캡처하여 응답 객체 생존 보장
            [self, sp](beast::error_code ec, std::size_t bytes_transferred) {
                // on_write 콜백 로직 수행 (sp->need_eof() 사용 등)
                self->on_write_common(sp->need_eof(), ec, bytes_transferred);
            });
    }

    // on_write 콜백 로직을 처리하는 별도 함수 (또는 기존 on_write 수정)
    void on_write_common(bool close, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) {
            fprintf(stderr, "[HTTP Session %p] Write error: %s (%d).\n", this, ec.message().c_str(), ec.value());
            return do_close();
        }
        if (close) {
            return do_close();
        }
        do_read(); // 다음 읽기 준비
    }

    // 쓰기 작업 완료 콜백
    //void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
    //    boost::ignore_unused(bytes_transferred);

    //    if (ec) {
    //        fprintf(stderr, "[HTTP Session] Write error: %s\n", ec.message().c_str());
    //        return do_close(); // 쓰기 오류 시 연결 종료
    //    }

    //    if (close) {
    //        // HTTP/1.0 이거나 Keep-Alive가 아닌 경우 연결 종료
    //        return do_close();
    //    }

    //    // Keep-Alive 연결이면 다음 요청 읽기 준비
    //    do_read();
    //}

    // 소켓을 정상적으로 종료한다.
    void do_close() {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        // 소켓 닫기는 스트림 소멸 시 자동으로 처리됨
    }
};

// --- HttpListener 구현 ---
HttpListener::HttpListener(
    net::io_context& ioc,
    tcp::endpoint endpoint,
    std::shared_ptr<std::string const> const& doc_root) // doc_root는 예시
    : ioc_(ioc)
    , acceptor_(net::make_strand(ioc)) // acceptor는 strand에서 실행 권장
    , doc_root_(doc_root) // 예시
{
    beast::error_code ec;

    // Acceptor 열기
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        fprintf(stderr, "[HTTP Listener] Open error: %s\n", ec.message().c_str());
        return;
    }

    // 주소 재사용 옵션 설정
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        fprintf(stderr, "[HTTP Listener] Set option error: %s\n", ec.message().c_str());
        return;
    }

    // 로컬 엔드포인트에 바인딩
    acceptor_.bind(endpoint, ec);
    if (ec) {
        fprintf(stderr, "[HTTP Listener] Bind error: %s\n", ec.message().c_str());
        return;
    }

    // 리스닝 시작
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        fprintf(stderr, "[HTTP Listener] Listen error: %s\n", ec.message().c_str());
        return;
    }
}

void HttpListener::run() {
    // 첫 번째 비동기 accept 호출 시작
    do_accept();
}

void HttpListener::do_accept() {
    // 다음 연결을 위한 소켓 생성
    acceptor_.async_accept(
        net::make_strand(ioc_), // 새 연결도 strand에서 처리 권장
        beast::bind_front_handler(&HttpListener::on_accept, shared_from_this()));
}

void HttpListener::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        fprintf(stderr, "[HTTP Listener] Accept error: %s\n", ec.message().c_str());
        // 리스닝 중단 또는 재시도 로직 추가 가능
    }
    else {
        // 새 연결에 대한 세션 생성 및 실행
        std::make_shared<HttpSession>(std::move(socket), doc_root_)->run();
    }

    // 다음 연결 수락 준비
    do_accept();
}

// --- HttpServer 구현 ---
HttpServer::HttpServer(const std::string& address, unsigned short port, int threads, const std::string& doc_root)
    : address_(address), port_(port), threads_(threads), doc_root_(doc_root), ioc_(threads > 0 ? threads : 1) {
    shared_doc_root_ = std::make_shared<std::string const>(doc_root_); // 공유 포인터 생성 (예시)
}

void HttpServer::run() {
    auto const addr = net::ip::make_address(address_);
    auto const port = static_cast<unsigned short>(port_);
    int num_threads = threads_;

    // 스레드 수 결정 (0 이하면 시스템 코어 수)
    if (num_threads <= 0) {
        unsigned int hardware_threads = std::thread::hardware_concurrency();
        num_threads = hardware_threads > 0 ? hardware_threads : 2; // 최소 2개 스레드
    }
    // io_context 생성 시 스레드 수를 지정했으므로 여기서는 정보만 출력
    fprintf(stdout, "HTTP Health Check server starting on %s:%d with %d IO threads...\n", address_.c_str(), port, num_threads);

    // Listener 생성 및 실행
    listener_ = std::make_shared<HttpListener>(ioc_, tcp::endpoint{ addr, port }, shared_doc_root_);
    listener_->run();

    // IO 스레드 생성 및 io_context 실행
    io_threads_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        io_threads_.emplace_back([this, i] { // 스레드 인덱스 'i' 캡처 (로깅용)
            std::stringstream thread_id_ss;
            thread_id_ss << std::this_thread::get_id();
            std::string thread_id_str = thread_id_ss.str();
            try {
                fprintf(stdout, "[HTTP IO Thread #%d ID: %s] Starting ioc.run().\n", i, thread_id_str.c_str());
                ioc_.run(); // 이벤트 루프 실행
                fprintf(stdout, "[HTTP IO Thread #%d ID: %s] ioc.run() finished cleanly.\n", i, thread_id_str.c_str());
            }
            catch (const std::exception& e) {
                // 표준 예외 처리
                fprintf(stderr, "[HTTP IO Thread #%d ID: %s] Exception caught: %s\n", i, thread_id_str.c_str(), e.what());
                // GTest에 직접 실패 보고는 어려우므로, 전역 플래그 설정 또는 로깅 후 종료
                // std::terminate(); // 또는 다른 방식으로 에러 전파
            }
            catch (...) {
                // 그 외 모든 예외 처리
                fprintf(stderr, "[HTTP IO Thread #%d ID: %s] Unknown exception caught.\n", i, thread_id_str.c_str());
                // std::terminate();
            }
            });
    }
    // 메인 스레드는 여기서 반환될 수 있음 (비동기 실행)
    // 실제 종료는 stop() 메서드 또는 시그널 핸들러에서 처리
}

void HttpServer::stop() {
    fprintf(stdout, "[HttpServer::stop] Initiating stop sequence...\n"); // 시작 로그

    // 1. Acceptor 중지 (필요 시 Listener 클래스에 stop 메서드 구현)
    // fprintf(stdout, "[HttpServer::stop] Stopping listener...\n");
    // if (listener_) { listener_->stop(); }

    // 2. io_context 중지 요청
    fprintf(stdout, "[HttpServer::stop] Stopping io_context (ioc_.stop())...\n");
    if (!ioc_.stopped()) {
        ioc_.stop();
        fprintf(stdout, "[HttpServer::stop] io_context stop requested.\n");
    }
    else {
        fprintf(stdout, "[HttpServer::stop] io_context already stopped.\n");
    }

    // 3. 모든 IO 스레드 종료 대기
    fprintf(stdout, "[HttpServer::stop] Waiting for %zu IO threads to finish...\n", io_threads_.size());
    for (size_t i = 0; i < io_threads_.size(); ++i) {
        std::thread& t = io_threads_[i];
        std::stringstream ss; // 스레드 ID 로깅용
        ss << t.get_id();
        if (t.joinable()) {
            fprintf(stdout, "[HttpServer::stop] Joining IO thread #%zu (ID: %s)...\n", i, ss.str().c_str());
            t.join(); // 스레드 종료 대기
            fprintf(stdout, "[HttpServer::stop] IO thread #%zu (ID: %s) joined.\n", i, ss.str().c_str());
        }
        else {
            fprintf(stderr, "[HttpServer::stop] Warning: IO thread #%zu (ID: %s) is not joinable.\n", i, ss.str().c_str());
        }
    }
    fprintf(stdout, "[HttpServer::stop] All HTTP IO threads finished.\n");

    // 4. 리소스 정리 (스레드 종료 후)
    fprintf(stdout, "[HttpServer::stop] Resetting listener and shared resources...\n");
    listener_.reset();
    shared_doc_root_.reset();

    fprintf(stdout, "[HttpServer::stop] Stop sequence complete.\n"); // 종료 로그
}