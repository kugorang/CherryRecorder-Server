#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "handlers/PlacesApiHandler.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

// Forward declaration
class HttpsSession;

/**
 * @class HttpsListener
 * @brief HTTPS 연결을 수락하고 각 연결에 대한 HttpsSession을 생성하는 클래스.
 */
class HttpsListener : public std::enable_shared_from_this<HttpsListener>
{
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;
    std::shared_ptr<PlacesApiHandler> places_handler_;

public:
    /**
     * @brief HttpsListener 생성자.
     * @param ioc Boost.Asio io_context 참조.
     * @param ctx SSL context 참조.
     * @param endpoint 리슨할 로컬 TCP 엔드포인트.
     */
    HttpsListener(
        net::io_context& ioc,
        ssl::context& ctx,
        tcp::endpoint endpoint);

    /**
     * @brief 리스너를 시작하여 비동기적으로 연결 수락을 시작한다.
     */
    void run();

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);
};

/**
 * @class HttpsServer
 * @brief HTTPS 서버를 설정하고 실행하는 메인 래퍼 클래스.
 */
class HttpsServer {
public:
    /**
     * @brief HttpsServer 생성자.
     * @param address 바인딩할 IP 주소.
     * @param port 리슨할 HTTPS 포트 번호.
     * @param threads 사용할 IO 스레드 수.
     * @param cert_file SSL 인증서 파일 경로.
     * @param key_file SSL 개인키 파일 경로.
     * @param dh_file DH 파라미터 파일 경로 (선택사항).
     */
    HttpsServer(const std::string& address, 
                unsigned short port, 
                int threads,
                const std::string& cert_file,
                const std::string& key_file,
                const std::string& dh_file = "");

    /**
     * @brief 서버를 시작한다.
     */
    void run();

    /**
     * @brief 서버를 정상적으로 중지한다.
     */
    void stop();

private:
    std::string address_;
    unsigned short port_;
    int threads_;
    std::string cert_file_;
    std::string key_file_;
    std::string dh_file_;

    net::io_context ioc_;
    ssl::context ctx_{ssl::context::tlsv12};
    std::vector<std::thread> io_threads_;
    std::shared_ptr<HttpsListener> listener_{nullptr};
    
    /**
     * @brief SSL 컨텍스트를 설정한다.
     */
    void setup_ssl_context();
}; 