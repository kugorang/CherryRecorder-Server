#pragma once

#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/HTTPMethod.h>
#include <folly/Memory.h>
#include <folly/io/async/EventBaseManager.h>
#include <memory>
#include <string>
#include <vector>
#include "handlers/PlacesApiHandler.hpp"

/**
 * @file ProxygenHttpServer.hpp
 * @brief Facebook Proxygen을 사용한 고성능 HTTP/HTTPS 서버 구현
 * 
 * 이 파일은 Proxygen 프레임워크를 사용하여 HTTP(8080)와 HTTPS(58080) 포트를
 * 지원하는 서버를 정의한다. Boost.Beast 기반 구현을 대체한다.
 */

namespace proxygen {
    class RequestHandler;
}

/**
 * @class CherryRequestHandler
 * @brief 개별 HTTP 요청을 처리하는 핸들러
 * 
 * Proxygen의 RequestHandler를 상속하여 실제 요청 처리 로직을 구현한다.
 */
class CherryRequestHandler : public proxygen::RequestHandler {
public:
    /**
     * @brief 생성자
     * @param places_handler Places API 핸들러 공유 포인터
     */
    explicit CherryRequestHandler(std::shared_ptr<PlacesApiHandler> places_handler);
    
    /**
     * @brief HTTP 요청 수신 시 호출되는 콜백
     * @param headers HTTP 요청 헤더
     */
    void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override;
    
    /**
     * @brief HTTP 요청 본문 수신 시 호출되는 콜백
     * @param body 요청 본문 데이터
     */
    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;
    
    /**
     * @brief 요청 수신 완료 시 호출되는 콜백
     */
    void onEOM() noexcept override;
    
    /**
     * @brief 업그레이드 요청 처리 (WebSocket 등)
     * @param protocol 업그레이드할 프로토콜
     */
    void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override;
    
    /**
     * @brief 요청 처리 중 오류 발생 시 호출
     * @param err 발생한 오류
     */
    void requestComplete() noexcept override;
    
    /**
     * @brief 오류 발생 시 호출
     * @param err 프록시젠 예외
     */
    void onError(proxygen::ProxygenError err) noexcept override;
    
private:
    void handleHealthCheck();
    void handleMapsKey();
    void handlePlacesNearby();
    void handlePlacesSearch();
    void handlePlaceDetails(const std::string& placeId);
    void handleStatus();
    void handleNotFound();
    void handleBadRequest(const std::string& message);
    void handleOptions(); // CORS preflight
    
    void sendResponse(int statusCode, const std::string& body, 
                     const std::string& contentType = "text/plain");
    void addCorsHeaders(proxygen::ResponseBuilder& builder);
    
    std::unique_ptr<proxygen::HTTPMessage> headers_;
    std::unique_ptr<folly::IOBuf> body_;
    std::shared_ptr<PlacesApiHandler> places_handler_;
};

/**
 * @class CherryRequestHandlerFactory
 * @brief RequestHandler 인스턴스를 생성하는 팩토리
 */
class CherryRequestHandlerFactory : public proxygen::RequestHandlerFactory {
public:
    /**
     * @brief 생성자
     * @param places_handler Places API 핸들러
     */
    explicit CherryRequestHandlerFactory(std::shared_ptr<PlacesApiHandler> places_handler);
    
    /**
     * @brief 새로운 요청에 대한 핸들러 생성
     * @param handler 하위 핸들러
     */
    void onServerStart(folly::EventBase* evb) noexcept override;
    
    void onServerStop() noexcept override;
    
    proxygen::RequestHandler* onRequest(
        proxygen::RequestHandler* handler,
        proxygen::HTTPMessage* msg) noexcept override;
        
private:
    std::shared_ptr<PlacesApiHandler> places_handler_;
};

/**
 * @class ProxygenHttpServer
 * @brief Proxygen 기반 HTTP/HTTPS 서버
 */
class ProxygenHttpServer {
public:
    /**
     * @brief 생성자
     * @param http_port HTTP 포트 (기본값: 8080)
     * @param https_port HTTPS 포트 (기본값: 58080) 
     * @param threads 워커 스레드 수 (0이면 하드웨어 스레드 수 사용)
     */
    ProxygenHttpServer(uint16_t http_port = 8080, 
                      uint16_t https_port = 58080,
                      int threads = 0);
    
    /**
     * @brief 소멸자
     */
    ~ProxygenHttpServer();
    
    /**
     * @brief 서버 시작
     * @param cert_path SSL 인증서 경로 (HTTPS용, 선택사항)
     * @param key_path SSL 키 경로 (HTTPS용, 선택사항)
     */
    void start(const std::string& cert_path = "", 
               const std::string& key_path = "");
    
    /**
     * @brief 서버 중지
     */
    void stop();
    
private:
    uint16_t http_port_;
    uint16_t https_port_;
    int threads_;
    std::unique_ptr<proxygen::HTTPServer> http_server_;
    std::unique_ptr<proxygen::HTTPServer> https_server_;
    std::vector<std::unique_ptr<proxygen::HTTPServerOptions>> options_;
    std::shared_ptr<PlacesApiHandler> places_handler_;
}; 