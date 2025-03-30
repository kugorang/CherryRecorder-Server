#include "CherryRecorder-Server.hpp"
#include <cstdlib>    // getenv
#include <iostream>
#include <stdexcept>  // runtime_error

// 환경 변수 값을 읽고 필수적인 경우 오류를 발생시키는 헬퍼 함수
std::string get_required_env_var(const std::string& var_name) {
#ifdef _WIN32
    // Windows에서는 _dupenv_s 사용
    char* buf = nullptr;
    size_t size = 0;

    if (_dupenv_s(&buf, &size, var_name.c_str()) != 0 || buf == nullptr) {
        throw std::runtime_error("필수 환경 변수가 설정되지 않았습니다: " + var_name);
    }

    std::string result(buf);
    free(buf);  // _dupenv_s로 할당된 메모리 해제
    return result;
#else
    // 다른 플랫폼에서는 기존 getenv 사용
    const char* value = std::getenv(var_name.c_str());
    if (value == nullptr) {
        throw std::runtime_error("필수 환경 변수가 설정되지 않았습니다: " + var_name);
    }
    return std::string(value);
#endif
}

unsigned short get_required_port_env_var(const std::string& var_name) {
    std::string value_str = get_required_env_var(var_name);
    try {
        int port_int = std::stoi(value_str);
        if (port_int > 0 && port_int <= 65535) {
            return static_cast<unsigned short>(port_int);
        }
        else {
            throw std::runtime_error("환경 변수 " + var_name + " 값이 유효한 포트 범위(1-65535)가 아닙니다: " + value_str);
        }
    }
    catch (const std::exception& e) {
        throw std::runtime_error("환경 변수 " + var_name + " 값을 포트로 변환할 수 없습니다: " + value_str + " (" + e.what() + ")");
    }
}

int main() {
    // --- Windows 콘솔 UTF-8 설정 (main 함수 시작 부분에 추가) ---
    #ifdef _WIN32
        // 콘솔 출력 코드 페이지를 UTF-8(65001)로 설정
        if (!SetConsoleOutputCP(CP_UTF8)) { // CP_UTF8은 65001 입니다.
            std::cerr << "오류: 콘솔 출력 인코딩을 UTF-8로 변경하지 못했습니다!" << std::endl;
        }
        // 콘솔 입력 코드 페이지도 UTF-8로 설정 (선택 사항, 출력 문제와는 별개)
        if (!SetConsoleCP(CP_UTF8)) {
            std::cerr << "오류: 콘솔 입력 인코딩을 UTF-8로 변경하지 못했습니다!" << std::endl;
        }
        // (선택 사항) C++ 표준 라이브러리(cout 등)의 로케일 설정
        // 표준 라이브러리가 UTF-8을 올바르게 처리하도록 로케일을 설정할 수 있습니다.
         try {
             std::locale::global(std::locale(".UTF-8")); // 또는 "ko_KR.UTF-8"
             std::cout.imbue(std::locale()); // cout에 적용
             std::cerr.imbue(std::locale()); // cerr에 적용
         } catch (const std::runtime_error& e) {
             std::cerr << "경고: C++ 로케일을 UTF-8로 설정하지 못했습니다: " << e.what() << std::endl;
         }
    #endif
    // --- 콘솔 설정 끝 ---

    try {
        // --- 설정 값 읽기 (환경 변수에서, 없으면 오류 발생) ---
        unsigned short server_port = get_required_port_env_var("SERVER_PORT");
        // 예시: DB 설정 읽기
        // std::string db_host = get_required_env_var("DB_HOST");
        // unsigned short db_port = get_required_port_env_var("DB_PORT");
        // std::string db_user = get_required_env_var("DB_USER");
        // std::string db_password = get_required_env_var("DB_PASSWORD"); // 실제 비밀번호는 다른 방식(아래 참고) 권장

        // std::cout << "DB 호스트: " << db_host << ", 포트: " << db_port << ", 사용자: " << db_user << std::endl;
        // std::cout << "DB 비밀번호 길이: " << db_password.length() << std::endl; // 비밀번호 직접 출력 금지!

        // --- 실제 서버 로직 (설정값 사용) ---
        boost::asio::io_context io_context;
        EchoServer server(io_context, server_port); // 외부에서 주입된 포트 사용
        server.start();
        // ... DB 연결 로직 등 ...

        std::cout << "서버가 포트 " << server_port << "에서 동작 중입니다..." << std::endl;
        io_context.run();
    }
    catch (const std::exception& e) {
        std::cerr << "[오류] 설정 또는 실행 중 문제 발생: " << e.what() << std::endl;
        return 1; // 오류 시 비정상 종료
    }
    return 0;
}