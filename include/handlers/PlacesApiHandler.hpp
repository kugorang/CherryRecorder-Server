#pragma once

#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/json.hpp>
#include <string>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

/**
 * @class PlacesApiHandler
 * @brief Google Places API 프록시 핸들러 클래스
 * 
 * Flutter 앱의 API 요청을 받아 Google Places API로 전달하고 응답을 반환하는 핸들러.
 * 모든 Google Places API 호출에는 API 키가 필요하므로 환경 변수에서 설정해야 한다.
 */
class PlacesApiHandler {
public:
    /**
     * @brief 생성자
     * @param api_key Google Places API 키
     */
    explicit PlacesApiHandler(const std::string& api_key);

    /**
     * @brief 주변 장소 검색 요청 처리
     * @param req HTTP 요청 (JSON 형식의 body에 latitude, longitude, radius 포함)
     * @return HTTP 응답 (JSON 형식의 장소 목록)
     */
    http::response<http::string_body> handleNearbySearch(
        const http::request<http::string_body, http::basic_fields<std::allocator<char>>>& req);

    /**
     * @brief 텍스트 기반 장소 검색 요청 처리
     * @param req HTTP 요청 (JSON 형식의 body에 query, 선택적으로 latitude, longitude, radius 포함)
     * @return HTTP 응답 (JSON 형식의 장소 목록)
     */
    http::response<http::string_body> handleTextSearch(
        const http::request<http::string_body, http::basic_fields<std::allocator<char>>>& req);

    /**
     * @brief 장소 상세 정보 요청 처리
     * @param place_id 장소 ID (URL 경로에서 추출됨)
     * @return HTTP 응답 (JSON 형식의 장소 상세 정보)
     * 
     * @note 이 함수는 Google Places API의 장소 정보를 그대로 반환한다.
     * 필요 시 응답 형식을 변환하는 로직을 추가할 수 있다.
     */
    http::response<http::string_body> handlePlaceDetails(
        const std::string& place_id);

private:
    std::string m_apiKey; ///< Google Places API 키

    /**
     * @brief Google Places API 요청 실행
     * @param method HTTP 메서드 (GET 또는 POST)
     * @param endpoint API 엔드포인트 URI
     * @param requestData 요청 데이터 (POST용)
     * @return API 응답 JSON 또는 오류 정보
     * 
     * @note 오류 발생 시 반환되는 JSON에는 "__error_status_code"와 "__error_body" 필드가 포함되며,
     * 이는 상위 핸들러 함수에서 적절한 HTTP 오류 응답으로 변환된다.
     */
    json::value requestGooglePlacesApi(
        http::verb method,
        const std::string& endpoint, 
        const json::value& requestData);

    /**
     * @brief 오류 응답 생성
     * @param status_code HTTP 상태 코드
     * @param error 오류 메시지
     * @return HTTP 오류 응답
     */
    http::response<http::string_body> createErrorResponse(
        http::status status_code,
        const std::string& error);
};