#include "ProxygenHttpServer.hpp"
#include <folly/portability/GFlags.h>
#include <folly/init/Init.h>
#include <folly/String.h>
#include <folly/io/async/EventBase.h>
#include <glog/logging.h>
#include <thread>
#include <cstdlib>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

// main_proxygen.cpp에서 정의되므로 여기서는 제거합니다.
// DEFINE_int32(http_port, 8080, "Port to listen on for HTTP");
// DEFINE_int32(https_port, 58080, "Port to listen on for HTTPS");
// DEFINE_string(cert_path, "", "Path to SSL certificate file");
// DEFINE_string(key_path, "", "Path to SSL key file");
// DEFINE_int32(threads, 0, "Number of threads to use (0 for hardware concurrency)");

// ------------------------ CherryRequestHandler 구현 ------------------------

CherryRequestHandler::CherryRequestHandler(std::shared_ptr<PlacesApiHandler> places_handler)
    : places_handler_(places_handler) {}

void CherryRequestHandler::onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept {
    headers_ = std::move(headers);
    LOG(INFO) << "Request received: " << headers_->getMethodString() 
              << " " << headers_->getPath();
}

void CherryRequestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
    if (body_) {
        body_->prependChain(std::move(body));
    } else {
        body_ = std::move(body);
    }
}

void CherryRequestHandler::onEOM() noexcept {
    const auto& method = headers_->getMethod();
    const auto& path = headers_->getPath();
    
    // OPTIONS 요청 처리 (CORS preflight)
    if (method == proxygen::HTTPMethod::OPTIONS) {
        handleOptions();
        return;
    }
    
    // 라우팅 처리
    if (method == proxygen::HTTPMethod::GET && path == "/health") {
        handleHealthCheck();
    } else if (method == proxygen::HTTPMethod::GET && path == "/maps/key") {
        handleMapsKey();
    } else if (method == proxygen::HTTPMethod::POST && path == "/places/nearby") {
        handlePlacesNearby();
    } else if (method == proxygen::HTTPMethod::POST && path == "/places/search") {
        handlePlacesSearch();
    } else if (method == proxygen::HTTPMethod::GET && path.find("/places/details/") == 0) {
        // /places/details/{placeId} 형식 처리
        std::string placeId = path.substr(std::string("/places/details/").length());
        if (!placeId.empty()) {
            handlePlaceDetails(placeId);
        } else {
            handleBadRequest("Missing Place ID in /places/details/ request.");
        }
    } else if (path == "/status") {
        handleStatus();
    } else {
        handleNotFound();
    }
}

void CherryRequestHandler::onUpgrade(proxygen::UpgradeProtocol protocol) noexcept {
    // WebSocket 업그레이드 등은 현재 지원하지 않음
    LOG(WARNING) << "Upgrade not supported";
}

void CherryRequestHandler::requestComplete() noexcept {
    delete this;
}

void CherryRequestHandler::onError(proxygen::ProxygenError err) noexcept {
    LOG(ERROR) << "Request error: " << proxygen::getErrorString(err);
    delete this;
}

void CherryRequestHandler::handleHealthCheck() {
    sendResponse(200, "OK", "text/plain");
}

void CherryRequestHandler::handleMapsKey() {
    const char* api_key = std::getenv("GOOGLE_MAPS_API_KEY");
    if (api_key == nullptr || api_key[0] == '\0') {
        handleBadRequest("Google Maps API key is not configured on the server");
        return;
    }
    sendResponse(200, api_key, "text/plain");
}

void CherryRequestHandler::handlePlacesNearby() {
    LOG(INFO) << "Received request for /places/nearby";
    
    // Proxygen 요청 본문을 문자열로 변환
    std::string bodyStr = body_ ? body_->clone()->moveToFbString().toStdString() : "";
    
    // Boost.Beast HTTP 요청 객체 생성
    boost::beast::http::request<boost::beast::http::string_body> beast_req;
    beast_req.method(boost::beast::http::verb::post);
    beast_req.target("/places/nearby");
    beast_req.version(11); // HTTP/1.1
    beast_req.set(boost::beast::http::field::content_type, "application/json");
    beast_req.body() = bodyStr;
    beast_req.prepare_payload();
    
    // PlacesApiHandler 호출
    auto response = places_handler_->handleNearbySearch(beast_req);
    
    // Beast 응답을 Proxygen으로 변환
    sendResponse(static_cast<int>(response.result_int()), 
                response.body(), 
                "application/json");
}

void CherryRequestHandler::handlePlacesSearch() {
    LOG(INFO) << "Received request for /places/search";
    
    // Proxygen 요청 본문을 문자열로 변환
    std::string bodyStr = body_ ? body_->clone()->moveToFbString().toStdString() : "";
    
    // Boost.Beast HTTP 요청 객체 생성
    boost::beast::http::request<boost::beast::http::string_body> beast_req;
    beast_req.method(boost::beast::http::verb::post);
    beast_req.target("/places/search");
    beast_req.version(11); // HTTP/1.1
    beast_req.set(boost::beast::http::field::content_type, "application/json");
    beast_req.body() = bodyStr;
    beast_req.prepare_payload();
    
    // PlacesApiHandler 호출
    auto response = places_handler_->handleTextSearch(beast_req);
    
    // Beast 응답을 Proxygen으로 변환
    sendResponse(static_cast<int>(response.result_int()), 
                response.body(), 
                "application/json");
}

void CherryRequestHandler::handlePlaceDetails(const std::string& placeId) {
    // PlacesApiHandler 호출
    auto response = places_handler_->handlePlaceDetails(placeId);
    
    // Beast 응답을 Proxygen으로 변환
    sendResponse(static_cast<int>(response.result_int()), 
                response.body(), 
                "application/json");
}

void CherryRequestHandler::handleStatus() {
    sendResponse(200, "{\"status\": \"ok\"}", "application/json");
}

void CherryRequestHandler::handleNotFound() {
    std::string message = "The resource '" + headers_->getPath() + "' was not found.";
    sendResponse(404, message, "text/plain");
}

void CherryRequestHandler::handleBadRequest(const std::string& message) {
    sendResponse(400, message, "text/plain");
}

void CherryRequestHandler::handleOptions() {
    proxygen::ResponseBuilder(downstream_)
        .status(200, "OK")
        .header("Access-Control-Allow-Origin", "*")
        .header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        .header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept")
        .header("Access-Control-Max-Age", "86400")
        .sendWithEOM();
}

void CherryRequestHandler::sendResponse(int statusCode, const std::string& body, 
                                       const std::string& contentType) {
    proxygen::ResponseBuilder builder(downstream_);
    builder.status(statusCode, statusCode == 200 ? "OK" : 
                              statusCode == 404 ? "Not Found" : 
                              statusCode == 400 ? "Bad Request" : "Error")
           .header("Content-Type", contentType);
    
    // CORS 헤더 추가
    addCorsHeaders(builder);
    
    if (!body.empty()) {
        builder.body(body);
    }
    
    builder.sendWithEOM();
}

void CherryRequestHandler::addCorsHeaders(proxygen::ResponseBuilder& builder) {
    builder.header("Access-Control-Allow-Origin", "*")
           .header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
           .header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept")
           .header("Access-Control-Max-Age", "86400");
}

// ------------------------ CherryRequestHandlerFactory 구현 ------------------------

CherryRequestHandlerFactory::CherryRequestHandlerFactory(
    std::shared_ptr<PlacesApiHandler> places_handler)
    : places_handler_(places_handler) {}

void CherryRequestHandlerFactory::onServerStart(folly::EventBase* evb) noexcept {
    LOG(INFO) << "Server started";
}

void CherryRequestHandlerFactory::onServerStop() noexcept {
    LOG(INFO) << "Server stopped";
}

proxygen::RequestHandler* CherryRequestHandlerFactory::onRequest(
    proxygen::RequestHandler* handler,
    proxygen::HTTPMessage* msg) noexcept {
    return new CherryRequestHandler(places_handler_);
}

// ------------------------ ProxygenHttpServer 구현 ------------------------

ProxygenHttpServer::ProxygenHttpServer(uint16_t http_port, uint16_t https_port, int threads)
    : http_port_(http_port)
    , https_port_(https_port)
    , threads_(threads == 0 ? std::thread::hardware_concurrency() : threads) {
    
    // Google Maps API 키 확인
    const char* api_key = std::getenv("GOOGLE_MAPS_API_KEY");
    if (api_key != nullptr && strlen(api_key) > 0) {
        LOG(INFO) << "Google Maps API key loaded (length: " << strlen(api_key) << ")";
        places_handler_ = std::make_shared<PlacesApiHandler>(api_key);
    } else {
        LOG(ERROR) << "GOOGLE_MAPS_API_KEY environment variable not set";
        places_handler_ = std::make_shared<PlacesApiHandler>("");
    }
}

ProxygenHttpServer::~ProxygenHttpServer() {
    stop();
}

void ProxygenHttpServer::start(const std::string& cert_path, const std::string& key_path) {
    LOG(INFO) << "ProxygenHttpServer::start() called";
    
    std::vector<proxygen::HTTPServer::IPConfig> IPs;
    
    // HTTP 서버 설정
    {
        proxygen::HTTPServer::IPConfig http_config(
            folly::SocketAddress("0.0.0.0", http_port_),
            proxygen::HTTPServer::Protocol::HTTP);
        IPs.push_back(http_config);
        LOG(INFO) << "HTTP server configured on port " << http_port_;
    }
    
    // HTTPS 서버 설정 (인증서가 제공된 경우)
    if (!cert_path.empty() && !key_path.empty()) {
        proxygen::HTTPServer::IPConfig https_config(
            folly::SocketAddress("0.0.0.0", https_port_),
            proxygen::HTTPServer::Protocol::HTTP2);
        
        // SSL 설정
        wangle::SSLContextConfig sslConfig;
        sslConfig.setCertificate(cert_path, key_path, "");
        https_config.sslConfigs.push_back(sslConfig);
        
        IPs.push_back(https_config);
        LOG(INFO) << "HTTPS server configured on port " << https_port_;
    } else {
        LOG(WARNING) << "SSL certificate not provided, HTTPS server disabled";
    }
    
    // HTTP 서버 옵션 설정
    auto opts = std::make_unique<proxygen::HTTPServerOptions>();
    opts->threads = threads_;
    opts->idleTimeout = std::chrono::milliseconds(60000);
    opts->shutdownOn = {SIGINT, SIGTERM};
    opts->enableContentCompression = true;
    opts->handlerFactories = proxygen::RequestHandlerChain()
        .addThen<CherryRequestHandlerFactory>(places_handler_)
        .build();
    opts->h2cEnabled = true;
    
    LOG(INFO) << "Creating HTTPServer instance...";
    
    // EventBase 테스트 (Proxygen이 사용할 것과 동일한 방식)
    try {
        folly::EventBase test_evb;
        LOG(INFO) << "Test EventBase created successfully";
        LOG(INFO) << "EventBase backend in use: " << test_evb.getBackend();
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to create test EventBase: " << e.what();
    }
    
    // 서버 생성
    http_server_ = std::make_unique<proxygen::HTTPServer>(std::move(*opts));

    LOG(INFO) << "Creating IO thread pool with " << threads_ << " threads...";
    
    // IO 스레드 풀 생성
    auto io_thread_pool = std::make_shared<folly::IOThreadPoolExecutor>(
        threads_,
        std::make_shared<folly::NamedThreadFactory>("ProxygenIO")
    );
    
    LOG(INFO) << "Binding to ports...";
    
    // 서버 생성 및 시작
    try {
        http_server_->bind(IPs);
        LOG(INFO) << "Successfully bound to ports";
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to bind: " << e.what();
        throw;
    }
    
    // 비동기로 서버 시작
    std::thread([this]() {
        LOG(INFO) << "Starting HTTP/HTTPS server thread...";
        try {
            http_server_->start();
            LOG(INFO) << "HTTP/HTTPS server thread started successfully";
        } catch (const std::exception& e) {
            LOG(ERROR) << "HTTP/HTTPS server thread error: " << e.what();
        }
    }).detach();
    
    LOG(INFO) << "Server started with " << threads_ << " threads";
}

void ProxygenHttpServer::stop() {
    if (http_server_) {
        LOG(INFO) << "Stopping HTTP/HTTPS server...";
        http_server_->stop();
        http_server_.reset();
    }
} 