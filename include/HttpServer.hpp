#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp> // strand 사용 권장
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <thread>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// Forward declaration
class HttpSession;

/**
 * @class HttpListener
 * @brief 들어오는 HTTP 연결을 수락하고 세션을 생성하는 클래스.
 */
class HttpListener : public std::enable_shared_from_this<HttpListener>
{
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::string const> doc_root_; // (옵션) 정적 파일 제공 시 사용

public:
    HttpListener(
        net::io_context& ioc,
        tcp::endpoint endpoint,
        std::shared_ptr<std::string const> const& doc_root); // doc_root는 예시, 필요 없으면 제거

    /**
     * @brief 리스너를 시작하여 연결 수락을 시작한다.
     */
    void run();

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);
};


/**
 * @class HttpServer
 * @brief Boost.Beast를 사용하여 HTTP 서버를 설정하고 실행하는 래퍼 클래스.
 */
class HttpServer {
public:
    /**
     * @brief HttpServer 생성자
     * @param address 바인딩할 IP 주소
     * @param port 리슨할 HTTP 포트 번호
     * @param threads 사용할 IO 스레드 수
     * @param doc_root 정적 파일 루트 디렉토리 (예시, 필요 없으면 제거)
     */
    HttpServer(const std::string& address, unsigned short port, int threads, const std::string& doc_root);

    /**
     * @brief 서버를 시작한다 (내부적으로 스레드 풀 생성 및 io_context 실행).
     */
    void run();

    /**
     * @brief 서버를 중지한다.
     */
    void stop();

private:
    std::string address_;
    unsigned short port_;
    int threads_;
    std::string doc_root_; // (옵션) 정적 파일 제공 시 사용

    net::io_context ioc_; // Beast 서버용 io_context
    std::vector<std::thread> io_threads_;
    std::shared_ptr<HttpListener> listener_{ nullptr };
    std::shared_ptr<std::string const> shared_doc_root_{ nullptr }; // doc_root 공유 포인터
};