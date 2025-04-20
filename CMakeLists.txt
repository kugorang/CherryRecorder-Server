# ======================================================================
# CMakeLists.txt for CherryRecorder-Server
# ======================================================================

# ----------------------------------------------------------------------
# CMake 최소 요구 버전 및 정책 설정
# ----------------------------------------------------------------------
# 비교적 최신 기능(예: vcpkg 통합, C++20 지원 등)을 사용하므로 3.20 권장
# 너무 오래된 CMake와의 호환성이 필요 없다면 이 정도로 충분합니다.
cmake_minimum_required(VERSION 3.20)
# 이 CMakeLists 파일이 작성될 때 기준으로 삼은 CMake 정책 버전을 명시합니다.
# minimum_required 버전과 일치시키는 것이 일반적입니다.
cmake_policy(VERSION 3.20)
# Set policy for FindBoost removal warning (CMP0167)
cmake_policy(SET CMP0167 NEW)
# 주석: set(CMAKE_POLICY_VERSION_MINIMUM ...)은 특별한 경우가 아니면 사용하지 않습니다.

# ----------------------------------------------------------------------
# vcpkg toolchain 설정
# ----------------------------------------------------------------------
# vcpkg 툴체인 파일 경로를 설정합니다. (CACHE STRING은 사용자가 변경 가능하도록 함)
set(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")
message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")

# ----------------------------------------------------------------------
# 프로젝트 및 C++ 표준 설정
# ----------------------------------------------------------------------
# 프로젝트 이름과 사용 언어(C++)를 정의합니다.
project("CherryRecorder-Server" LANGUAGES CXX)

# 전역적으로 사용할 C++ 표준 및 설정을 지정합니다.
set(CMAKE_CXX_STANDARD 20)          # C++20 표준 사용
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 지정된 표준을 반드시 사용하도록 강제
set(CMAKE_CXX_EXTENSIONS OFF)       # 컴파일러별 확장 기능 사용 금지 (표준 준수)

# Enable trace logging for spdlog
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

message(STATUS "Project Name: ${PROJECT_NAME}")
message(STATUS "CXX Standard: ${CMAKE_CXX_STANDARD}")

# ----------------------------------------------------------------------
# 플랫폼별 설정 (Windows / MSVC)
# ----------------------------------------------------------------------
if(WIN32)
  # 타겟 Windows 버전 정의 (Windows 10 이상 권장)
  add_definitions(-D_WIN32_WINNT=0x0A00)
  message(STATUS "Targeting Windows 10 or later (_WIN32_WINNT=0x0A00)")

  if(MSVC)
    # MSVC 컴파일러 UTF-8 소스 코드 처리 옵션
    add_compile_options(/utf-8)
    message(STATUS "MSVC: Added /utf-8 compile option.")

    # MSVC Hot Reload (Edit and Continue) 지원 설정
    if(POLICY CMP0141)
      cmake_policy(SET CMP0141 NEW)
      set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
      message(STATUS "MSVC: Enabled Edit and Continue debug information format.")
    endif()
  endif()
endif()

# ----------------------------------------------------------------------
# 라이브러리 타겟 정의 (CherryRecorder-Server)
# ----------------------------------------------------------------------
# 애플리케이션 핵심 로직을 포함하는 정적 라이브러리(기본값)를 정의합니다.
# --- Boost 라이브러리 찾기 (Asio + Beast) ---
# find_package 시 필요한 컴포넌트를 명시한다.
# Boost::headers는 많은 Boost 라이브러리에서 필요하다.
find_package(Boost 1.70.0 REQUIRED COMPONENTS system asio beast json)
find_package(OpenSSL REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED) # Find pthreads on non-Windows

message(STATUS "Boost include directories: ${Boost_INCLUDE_DIRS}")
message(STATUS "OpenSSL include directories: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "spdlog include directories: ${spdlog_INCLUDE_DIRS}") # Check variable name from find_package

# ----------------------------------------------------------------------
# Chat Library Target (ChatLib)
# ----------------------------------------------------------------------
add_library(ChatLib) # Default: STATIC library
target_sources(ChatLib PRIVATE
    src/MessageHistory.cpp
    src/ChatRoom.cpp
    src/ChatSession.cpp
    src/ChatListener.cpp
    src/ChatServer.cpp
)
target_include_directories(ChatLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # Public headers
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}> # Boost needed by headers
    $<BUILD_INTERFACE:${spdlog_INCLUDE_DIRS}> # spdlog needed by implementation details (log macros if used in headers) - consider PRIVATE if only in .cpp
)
target_link_libraries(ChatLib PUBLIC # Libraries needed by ChatLib and potentially its users
    Boost::asio
    Boost::beast
    Boost::system
    spdlog::spdlog
)
# Link Threads conditionally for ChatLib if it uses threads directly
if(Threads_FOUND AND NOT WIN32)
    target_link_libraries(ChatLib PUBLIC Threads::Threads)
endif()
message(STATUS "Configured target: ChatLib (Chat Server Logic)")

# ----------------------------------------------------------------------
# Echo Server Library Target (EchoServerLib) - Renamed for clarity
# ----------------------------------------------------------------------
add_library(EchoServerLib) # 소스 파일은 target_sources 로 추가 권장

# target_sources를 사용하여 소스 파일을 명시적으로 추가합니다 (가독성 및 관리 용이성)
target_sources(EchoServerLib
    PRIVATE
        src/CherryRecorder-Server.cpp
)

# 외부에서 이 라이브러리를 사용할 때 필요한 include 경로를 PUBLIC으로 지정합니다.
target_include_directories(EchoServerLib PUBLIC
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # Boost 헤더 경로 추가 (find_package 결과 사용)
        $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>
)

# vcpkg를 통해 Boost.Asio 의존성을 찾습니다. CONFIG 모드는 vcpkg 연동 시 표준 방식입니다.
# find_package(boost_asio CONFIG REQUIRED)  # vcpkg + find_package(Boost ...) 사용 시 불필요
# find_package(boost_json CONFIG REQUIRED)  # vcpkg + find_package(Boost ...) 사용 시 불필요
# 찾은 Boost.Asio 라이브러리를 PUBLIC으로 링크합니다 (헤더에서 Asio 사용 시 필요).
target_link_libraries(EchoServerLib
    PUBLIC
        Boost::asio
        Boost::system
        Boost::json
        spdlog::spdlog
)
message(STATUS "Configured target: CherryRecorder-Server library (TCP Echo)")

# --- 라이브러리 타겟 정의: HttpServerLib (HTTP Health Check) ---
add_library(HttpServerLib)
target_sources(HttpServerLib PRIVATE 
    src/HttpServer.cpp 
    src/handlers/PlacesApiHandler.cpp
) # HttpServer 구현 소스
target_include_directories(HttpServerLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # HttpServer.hpp 포함
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>               # Beast/Asio 헤더 필요
)
target_link_libraries(HttpServerLib PUBLIC # 필요한 라이브러리 링크
    Boost::beast
    Boost::system
    Boost::asio # HttpServer 내부에서 Asio 사용 시 필요
    Boost::json # Places API 응답 처리에 필요
    spdlog::spdlog
)

# 플랫폼별 스레드 라이브러리 링크 (HttpServer 가 스레드 사용 시)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    if(Threads_FOUND)
        target_link_libraries(HttpServerLib PUBLIC Threads::Threads)
    endif()
endif()
message(STATUS "Configured target: HttpServerLib library (HTTP Health)")

# --- 라이브러리 타겟 정의: ChatServerLib (채팅 서버 로직) ---
add_library(ChatServerLib)
target_sources(ChatServerLib PRIVATE 
    src/ChatServer.cpp
    src/ChatSession.cpp
    src/ChatRoom.cpp
    src/ChatListener.cpp
    src/MessageHistory.cpp
) 
target_include_directories(ChatServerLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # ChatServer.hpp 등
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>              
)
target_link_libraries(ChatServerLib PUBLIC 
    Boost::asio
    Boost::beast # WebSocket 사용
    Boost::system
    spdlog::spdlog
)
# 플랫폼별 스레드 라이브러리 링크 (ChatServer 가 스레드 사용 시)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    if(Threads_FOUND)
        target_link_libraries(ChatServerLib PUBLIC Threads::Threads)
    endif()
endif()
message(STATUS "Configured target: ChatServerLib library (WebSocket Chat)")

# ----------------------------------------------------------------------
# 실행 파일 타겟 정의 (CherryRecorder-Server-App)
# ----------------------------------------------------------------------
# OpenSSL 패키지를 찾는다 (vcpkg를 통해 설치되었다고 가정)
find_package(OpenSSL REQUIRED)

# --- 디버깅 메시지 추가 ---
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL Found!")
    message(STATUS "  Includes: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "  Libraries (SSL): ${OPENSSL_SSL_LIBRARY}")
    message(STATUS "  Libraries (Crypto): ${OPENSSL_CRYPTO_LIBRARY}")
    message(STATUS "  Libraries (Imported Targets): ${OPENSSL_LIBRARIES}") # CMake 3.18+ 에서 OpenSSL::SSL, OpenSSL::Crypto 타겟 제공 확인
else()
    message(FATAL_ERROR "OpenSSL could not be found!")
endif()
# --- 디버깅 메시지 끝 ---

message(STATUS "Found OpenSSL: TRUE (includes: ${OPENSSL_INCLUDE_DIR}, libraries: ${OPENSSL_LIBRARIES})")

# 실제 서버 애플리케이션의 진입점(main 함수 포함)을 정의합니다.
add_executable(CherryRecorder-Server-App)

target_sources(CherryRecorder-Server-App 
    PRIVATE
        src/main.cpp
)
target_include_directories(CherryRecorder-Server-App 
    PRIVATE 
        # Include paths needed specifically by main.cpp, if any
        # Inherited PUBLIC includes from linked libraries are usually sufficient
        ${Boost_INCLUDE_DIRS}) # main 에서 Boost 사용 시

# 필요한 라이브러리들 링크 (하나로 병합)
target_link_libraries(CherryRecorder-Server-App
    PRIVATE
        EchoServerLib           # TCP Echo 로직 라이브러리
        HttpServerLib           # HTTP 서버 로직 라이브러리
        ChatServerLib           # 채팅 서버 로직 라이브러리
        # main.cpp에서 직접 사용하는 라이브러리가 있다면 여기에 추가
        OpenSSL::SSL            # OpenSSL SSL 라이브러리 링크
        OpenSSL::Crypto         # OpenSSL Crypto 라이브러리 링크
        # spdlog::spdlog # Already linked PUBLICLY by libraries, typically not needed here again
        # Threads::Threads # Already linked PUBLICLY by libraries if needed
)

# 위에서 정의한 CherryRecorder-Server 라이브러리를 PRIVATE으로 링크합니다.
# App 내부에서만 Server 라이브러리를 사용하고 외부로 노출하지 않습니다.

# 플랫폼에 따라 스레드 라이브러리 조건부 링크
if(Threads_FOUND AND NOT WIN32) # Threads를 찾았고 Windows가 아닐 경우에만 링크
  target_link_libraries(CherryRecorder-Server-App PRIVATE Threads::Threads)
  message(STATUS "Linking with Threads::Threads on non-Windows platform.")
elseif(WIN32)
  message(STATUS "Windows platform detected, assuming native threads support via C++ standard library. Skipping explicit Threads::Threads linkage.")
else()
  message(WARNING "Threads library not found and not on Windows. Manual thread library configuration might be needed for this platform.")
endif()

message(STATUS "Configured target: CherryRecorder-Server-App executable")

# ----------------------------------------------------------------------
# 테스트 타겟 정의 (CherryRecorder-Server-Testing)
# ----------------------------------------------------------------------
# 테스트 빌드 여부를 결정하는 CMake 옵션 (기본값 ON)
option(BUILD_TESTING "Build the unit tests" ON)

# BUILD_TESTING 옵션이 ON일 때만 테스트 관련 내용을 처리합니다.
if(BUILD_TESTING)
  # CTest 시스템 활성화
  enable_testing()

  # 테스트용 실행 파일을 정의합니다.
  add_executable(CherryRecorder-Server-Testing)
  target_sources(CherryRecorder-Server-Testing 
    PRIVATE
      tests/test_main.cpp
      tests/test_http_server.cpp
      tests/test_chat_server.cpp
      tests/test_echo_server.cpp
  )
  target_include_directories(CherryRecorder-Server-Testing 
    PRIVATE
        # Inherits PUBLIC includes from linked libs (like ChatLib's include dir)
        ${Boost_INCLUDE_DIRS}   # 테스트에도 Boost 필요
  )
  # 테스트 실행 파일에 필요한 라이브러리들을 PRIVATE으로 링크합니다.
  # GTest::gtest_main 타겟은 GTest 라이브러리와 main 함수를 포함합니다.
  # 테스트 실행 파일에 필요한 라이브러리 링크
  target_link_libraries(CherryRecorder-Server-Testing PRIVATE
      EchoServerLib         # EchoServer 테스트 위해 필요
      HttpServerLib         # HttpServer 테스트 위해 필요
      ChatServerLib         # ChatServer 테스트 위해 필요
      GTest::gtest_main     # GTest
      Boost::system         # 테스트 코드에서 Boost 사용 시
      OpenSSL::SSL          # Add OpenSSL SSL library
      OpenSSL::Crypto       # Add OpenSSL Crypto library
      # 필요한 다른 라이브러리 (테스트 코드에서 사용 시)
      # Boost::system etc. are likely inherited via PUBLIC link from libraries
      # spdlog::spdlog is inherited
      # Threads::Threads is inherited if needed
  )

  # 플랫폼에 따라 스레드 라이브러리 조건부 링크
  if(Threads_FOUND AND NOT WIN32) # Threads를 찾았고 Windows가 아닐 경우에만 링크
    target_link_libraries(CherryRecorder-Server-Testing PRIVATE Threads::Threads)
    message(STATUS "Linking with Threads::Threads on non-Windows platform.")
  elseif(WIN32)
    message(STATUS "Windows platform detected, assuming native threads support via C++ standard library. Skipping explicit Threads::Threads linkage.")
  else()
    message(WARNING "Threads library not found and not on Windows. Manual thread library configuration might be needed for this platform.")
  endif()

  # CTest 프레임워크에 실행할 테스트를 등록합니다.
  # 기존 단일 테스트 주석 처리 또는 삭제
  # add_test(NAME CherryRecorderServerTest COMMAND CherryRecorder-Server-Testing)

  # HTTP 서버 테스트 분리 (타임아웃 추가)
  add_test(NAME HttpServerTest COMMAND CherryRecorder-Server-Testing --gtest_filter=HttpServerTest.*)
  set_tests_properties(HttpServerTest PROPERTIES TIMEOUT 60)

  # 채팅 서버 테스트 분리 (타임아웃 추가)
  add_test(NAME ChatServerTest COMMAND CherryRecorder-Server-Testing --gtest_filter=ChatServerTest.*)
  set_tests_properties(ChatServerTest PROPERTIES TIMEOUT 60)

  # 필요 시 다른 테스트 그룹 추가 (예: TCP Echo 테스트)
  add_test(NAME EchoServerTest COMMAND CherryRecorder-Server-Testing --gtest_filter=EchoServerTest.*)
  set_tests_properties(EchoServerTest PROPERTIES TIMEOUT 60)

  message(STATUS "Configured separate CTest tests: HttpServerTest, ChatServerTest, EchoServerTest")

else()
  message(STATUS "BUILD_TESTING is OFF. Skipping test configuration.")
endif()

# ======================================================================
# 설치 규칙 정의 (선택 사항)
# ======================================================================
# 만약 'cmake --install' 명령어를 사용할 계획이라면,
# 라이브러리, 실행 파일, 헤더 등을 설치할 위치와 방법을 정의해야 합니다.
# include(GNUInstallDirs)
# install(TARGETS CherryRecorder-Server CherryRecorder-Server-App
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )
# install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# message(STATUS "Install rules configured (optional).")
# ======================================================================

# 항상 Release 모드로 설정
add_compile_definitions(RELEASE_MODE)
message(STATUS "Using Release mode compilation definitions.")