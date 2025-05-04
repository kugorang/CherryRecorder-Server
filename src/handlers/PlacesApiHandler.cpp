#include "../include/handlers/PlacesApiHandler.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/json.hpp>
#include <cstdlib>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace json = boost::json;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

PlacesApiHandler::PlacesApiHandler(const std::string& api_key)
    : m_apiKey(api_key) {
    std::cout << "PlacesApiHandler created with API key" << std::endl;
}

// 템플릿 함수 구현
http::response<http::string_body> PlacesApiHandler::handleNearbySearch(
    const http::request<http::string_body, http::basic_fields<std::allocator<char>>>& req) {
    
    std::cout << "handleNearbySearch 호출됨, API 키 길이: " << m_apiKey.length() << std::endl;
    
    try {
        // 요청 본문 파싱 (이제 req.body()는 std::string)
        const std::string& body_str = req.body();
        json::value req_json = json::parse(body_str);
        
        std::cout << "요청 본문 파싱 성공: " << body_str << std::endl;
        
        // 클라이언트 요청에서 필요한 파라미터 추출
        double latitude = req_json.at("latitude").as_double();
        double longitude = req_json.at("longitude").as_double();
        
        // radius 값을 double 또는 int64로 유연하게 처리
        double radius = 1500.0; // 기본값 설정
        if (req_json.as_object().contains("radius")) {
            const auto& radius_val = req_json.at("radius");
            if (radius_val.is_double()) {
                radius = radius_val.as_double();
            } else if (radius_val.is_int64()) {
                radius = static_cast<double>(radius_val.as_int64());
                std::cout << "Warning: Received radius as integer, converting to double: " << radius << std::endl;
            } else {
                std::cerr << "Warning: Invalid type for radius, using default." << std::endl;
            }
        }
        
        std::cout << "위치 정보 추출: lat=" << latitude << ", lng=" << longitude 
                  << ", 반경=" << radius << "m" << std::endl;
        
        // Google Places API 요청 데이터 구성
        json::object request_data;
        json::object location_restriction;
        json::object circle;
        json::object center;
        
        center["latitude"] = latitude;
        center["longitude"] = longitude;
        circle["center"] = center;
        circle["radius"] = radius;
        location_restriction["circle"] = circle;
        request_data["locationRestriction"] = location_restriction;
        
        // Google Places API 호출 (POST 사용)
        json::value response_data = this->requestGooglePlacesApi(
            http::verb::post, // 메서드 명시
            "https://places.googleapis.com/v1/places:searchNearby", 
            json::value(request_data));
        
        // ===== Google API 오류 확인 및 전파 =====
        if (response_data.is_object() && response_data.as_object().contains("__error_status_code")) {
            int status_code = response_data.at("__error_status_code").to_number<int>();
            std::string error_body = response_data.at("__error_body").as_string().c_str();
            return this->createErrorResponse(static_cast<http::status>(status_code), error_body);
        }
        // ===== 추가 끝 =====

        // 응답 반환 (정상)
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        // CORS 헤더 추가
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Accept");
        res.keep_alive(req.keep_alive());
        res.body() = json::serialize(response_data);
        res.prepare_payload();
        return res;
    }
    catch (const std::exception& e) {
        // 오류 로깅 시 요청 본문 출력 (string_body 사용)
        std::cerr << "Error parsing request body: " << req.body() << std::endl; // 오류 로깅 추가
        return this->createErrorResponse(http::status::bad_request, 
                                  std::string("Error processing request: ") + e.what());
    }
}

http::response<http::string_body> PlacesApiHandler::handleTextSearch(
    const http::request<http::string_body, http::basic_fields<std::allocator<char>>>& req) {
    
    try {
        // 요청 본문 파싱
        const std::string& body_str = req.body();
        json::value req_json = json::parse(body_str);
        
        // 클라이언트 요청에서 필요한 파라미터 추출
        std::string query = req_json.at("query").as_string().c_str();
        double latitude = req_json.as_object().contains("latitude") ? 
                         req_json.at("latitude").as_double() : 37.5665;
        double longitude = req_json.as_object().contains("longitude") ? 
                          req_json.at("longitude").as_double() : 126.9780;
                          
        // radius 값을 double 또는 int64로 유연하게 처리
        double radius = 50000.0; // 기본값 설정
        if (req_json.as_object().contains("radius")) {
            const auto& radius_val = req_json.at("radius");
            if (radius_val.is_double()) {
                radius = radius_val.as_double();
            } else if (radius_val.is_int64()) {
                radius = static_cast<double>(radius_val.as_int64());
                std::cout << "Warning: Received radius as integer, converting to double: " << radius << std::endl;
            } else {
                std::cerr << "Warning: Invalid type for radius, using default." << std::endl;
            }
        }
        
        // Google Places API 요청 데이터 구성
        json::object request_data;
        json::object location_bias;
        json::object circle;
        json::object center;
        
        request_data["textQuery"] = query;
        center["latitude"] = latitude;
        center["longitude"] = longitude;
        circle["center"] = center;
        circle["radius"] = radius;
        location_bias["circle"] = circle;
        request_data["locationBias"] = location_bias;
        
        // Google Places API 호출 (POST 사용)
        json::value response_data = this->requestGooglePlacesApi(
            http::verb::post, // 메서드 명시
            "https://places.googleapis.com/v1/places:searchText", 
            json::value(request_data));
        
        // ===== Google API 오류 확인 및 전파 =====
        if (response_data.is_object() && response_data.as_object().contains("__error_status_code")) {
            int status_code = response_data.at("__error_status_code").to_number<int>();
            std::string error_body = response_data.at("__error_body").as_string().c_str();
            return this->createErrorResponse(static_cast<http::status>(status_code), error_body);
        }
        // ===== 추가 끝 =====

        // 응답 반환 (정상)
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        // CORS 헤더 추가
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Accept");
        res.keep_alive(req.keep_alive());
        res.body() = json::serialize(response_data);
        res.prepare_payload();
        return res;
    }
    catch (const std::exception& e) {
        // 오류 로깅 시 요청 본문 출력 (string_body 사용)
        std::cerr << "Error parsing request body: " << req.body() << std::endl; // 오류 로깅 추가
        return this->createErrorResponse(http::status::bad_request, 
                                  std::string("Error processing request: ") + e.what());
    }
}

http::response<http::string_body> PlacesApiHandler::handlePlaceDetails(
    const std::string& place_id) {
    
    try {
        // 요청 본문 파싱 로직 제거 (GET 요청이므로)
        
        // Google Places API 요청 URL 구성 (인자로 받은 place_id 사용)
        std::string api_url = "https://places.googleapis.com/v1/places/" + place_id;
        
        // Google Places API 호출 (GET 사용)
        json::value response_data = this->requestGooglePlacesApi(
            http::verb::get, 
            api_url, 
            json::object()); 
        
        // ===== Google API 오류 확인 및 전파 =====
        if (response_data.is_object() && response_data.as_object().contains("__error_status_code")) {
            int status_code = response_data.at("__error_status_code").to_number<int>();
            std::string error_body = response_data.at("__error_body").as_string().c_str();
            // Google API 오류 시 상태 코드와 본문을 그대로 클라이언트에 전달
            http::response<http::string_body> error_res{static_cast<http::status>(status_code), 11};
            error_res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            error_res.set(http::field::content_type, "application/json"); // Google 오류는 JSON일 수 있음
            // CORS 헤더 추가
            error_res.set(http::field::access_control_allow_origin, "*");
            error_res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
            error_res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Accept");
            error_res.body() = error_body;
            error_res.prepare_payload();
            return error_res;
            // 또는 createErrorResponse 사용:
            // return this->createErrorResponse(static_cast<http::status>(status_code), error_body);
        }
        // ===== 추가 끝 =====

        // 응답 반환 (정상)
        http::response<http::string_body> res{http::status::ok, 11}; 
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        // CORS 헤더 추가
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Accept");
        res.body() = json::serialize(response_data); // 여기서 response_data는 파싱된 Google 응답
        res.prepare_payload();
        return res;
    }
    catch (const std::exception& e) {
        // 오류 로깅 시 요청 본문 출력 제거 (req 객체 없음)
        return this->createErrorResponse(http::status::internal_server_error, 
                                  std::string("Error processing place details request: ") + e.what());
    }
}

json::value PlacesApiHandler::requestGooglePlacesApi(
    http::verb method, // HTTP 메서드 파라미터 추가
    const std::string& endpoint, 
    const json::value& requestData) {
    
    try {
        std::cout << "Google Places API 요청 (" << method << "): " << endpoint << std::endl;
        // POST 요청일 때만 요청 데이터 로깅
        if (method == http::verb::post) {
            std::cout << "요청 데이터: " << json::serialize(requestData) << std::endl;
        }
        std::cout << "API 키 길이: " << m_apiKey.length() << std::endl;
        
        // SSL 컨텍스트 및 IO 컨텍스트 설정
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        
        // HTTPS 연결 설정
        tcp::resolver resolver(ioc);
        ssl::stream<tcp::socket> stream(ioc, ctx);
        
        // 호스트 이름 추출
        std::string host = "places.googleapis.com";
        auto const results = resolver.resolve(host, "443");
        
        // 연결 설정
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);
        
        // HTTP 요청 준비 (메서드 파라미터 사용)
        http::request<http::string_body> req{method, endpoint, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "application/json");
        req.set("X-Goog-Api-Key", m_apiKey);
        
        // 메서드에 따라 다른 FieldMask 설정
        if (method == http::verb::get) { ///< Place Details (GET)
            req.set("X-Goog-FieldMask", "id,displayName,formattedAddress,location");
        } else { ///< Nearby Search, Text Search (POST)
            req.set("X-Goog-FieldMask", "places.id,places.displayName,places.formattedAddress,places.location");
        }
        
        // POST 요청일 때만 본문 설정
        if (method == http::verb::post) {
            req.body() = json::serialize(requestData);
        }
        req.prepare_payload();
        
        // 요청 전송
        http::write(stream, req);
        
        // 응답 수신
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        beast::error_code ec;
        http::read(stream, buffer, res);
        
        // http::read 이후 오류 코드 로깅 추가
        if (ec && ec != http::error::end_of_stream) {
            std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Error after http::read: " 
                      << ec.message() << " (Code: " << ec.value() << ")" << std::endl;
            // 기존 예외 처리는 유지 (필요시)
            // throw beast::system_error{ec, "Read failed"};
        } else if (ec == http::error::end_of_stream) {
             std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Normal EOF after http::read." << std::endl;
        } else {
             std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] No error after http::read." << std::endl;
        }

        // 연결 종료
        // beast::error_code ec; // ec 변수는 이미 선언됨
        stream.shutdown(ec);

        // stream.shutdown 이후 오류 코드 로깅 추가
        if(ec == net::error::eof) {
            std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Normal EOF during shutdown." << std::endl;
            ec = {}; ///< Clear the error code for EOF
        } else if (ec == ssl::error::stream_truncated) {
            std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Stream truncated during shutdown (Code: " 
                      << ec.value() << "). Ignored." << std::endl;
            ec = {}; ///< stream_truncated 오류는 일단 무시하고 로깅만
        } else if (ec) {
             std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Error during stream shutdown: " 
                      << ec.message() << " (Code: " << ec.value() << ")" << std::endl;
            // 기존 예외 처리 유지
            throw beast::system_error{ec};
        }
        
        // 응답 본문 파싱 및 변환
        json::value response_json;
        try {
             response_json = json::parse(res.body());
        } catch (const std::exception& parse_error) {
            std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Failed to parse Google API response as JSON: " 
                      << parse_error.what() << "\nResponse body: " << res.body() << std::endl;
            // JSON 파싱 실패 시, 원본 상태 코드와 본문을 오류로 반환
            json::object error_obj;
            error_obj["__error_status_code"] = res.result_int(); ///< 원본 상태 코드 저장
            error_obj["__error_body"] = res.body(); ///< 원본 본문 저장
            return error_obj;
        }
        
        std::cout << "Google API 응답 코드: " << res.result_int() << std::endl;
        std::cout << "응답 내용 일부: " << (res.body().length() > 100 ? res.body().substr(0, 100) + "..." : res.body()) << std::endl;
        
        // ===== Google API 오류 상태 코드 확인 =====
        ///< @brief Google API 오류 응답을 감지하여 클라이언트에 전달하기 위해 특수 필드에 저장한다.
        ///< 이 값들은 이후 호출자에 의해 적절한 HTTP 오류 응답으로 변환된다.
        if (res.result_int() < 200 || res.result_int() >= 300) {
             std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Google API returned error status: " << res.result_int() << std::endl;
             json::object error_obj;
             error_obj["__error_status_code"] = res.result_int(); ///< 원본 상태 코드 저장
             error_obj["__error_body"] = json::serialize(response_json); ///< 파싱된 JSON 오류 메시지 저장
             return error_obj;
        }
        // ===== 오류 처리 끝 =====

        // 클라이언트에 맞는 응답 형식으로 변환 (성공 시)
        json::object result;
        if (method == http::verb::get) { ///< Place Details 응답 처리
             ///< @note 현재는 Google API 응답을 그대로 반환합니다. 추후 응답 형식 변환이 필요한 경우
             ///< transformPlaceDetails 함수를 구현하여 사용할 수 있습니다.
             result = response_json.as_object();
        } else if (response_json.is_object() && response_json.as_object().contains("places")) { ///< Nearby/Text Search 응답 처리
             json::array places_array = response_json.at("places").as_array();
             json::array transformed_places;
             
             for (const auto& place : places_array) {
                 json::object transformed_place;
                 transformed_place["placeId"] = place.at("id").as_string();
                 transformed_place["name"] = place.at("displayName").at("text").as_string();
                 
                 if (place.as_object().contains("formattedAddress")) {
                     transformed_place["vicinity"] = place.at("formattedAddress").as_string();
                 }
                 
                 json::object location;
                 location["latitude"] = place.at("location").at("latitude").as_double();
                 location["longitude"] = place.at("location").at("longitude").as_double();
                 transformed_place["location"] = location;
                 
                 transformed_places.push_back(transformed_place);
             }
             
             result["places"] = transformed_places;
        }
        
        return result;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in requestGooglePlacesApi: " << e.what() << std::endl;
        
        // 오류 시 빈 응답 반환
        json::object error_object;
        error_object["error"] = e.what();
        return error_object;
    }
}

http::response<http::string_body> PlacesApiHandler::createErrorResponse(
    http::status status_code,
    const std::string& error) {
    
    http::response<http::string_body> res{status_code, 11}; // HTTP/1.1 가정
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    // CORS 헤더 추가
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Accept");
    
    json::object error_obj;
    error_obj["error"] = error;
    res.body() = json::serialize(error_obj);
    
    res.prepare_payload();
    return res;
}