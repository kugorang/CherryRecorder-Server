# CMakeList.txt: CherryRecorder-Server에 대한 CMake 프로젝트, 여기에 소스를 포함하고
# 프로젝트 특정 논리를 정의합니다.
#
cmake_minimum_required (VERSION 3.30)

# --- 타겟 Windows 버전 정의 (Windows 10 이상) ---
if(WIN32)
  # Windows 10 버전 코드는 0x0A00 입니다.
  # 다른 버전을 원할 경우 해당 버전 코드로 변경하세요. (예: Windows 7 = 0x0601)
  add_definitions(-D_WIN32_WINNT=0x0A00)
  message(STATUS "Targeting Windows 10 or later (_WIN32_WINNT=0x0A00)")
endif()
# --- 버전 정의 끝 ---

# --- MSVC 사용 시 /utf-8 컴파일 옵션 추가 (이전 단계에서 추가) ---
if(MSVC)
  add_compile_options(/utf-8)
  message(STATUS "MSVC: '/utf-8' compile option added.")
endif()
# --- 옵션 추가 끝 ---

# 경로: 서브모듈로 추가된 vcpkg 폴더
set(CMAKE_TOOLCHAIN_FILE 
    "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

# 지원되는 경우 MSVC 컴파일러에 대해 핫 다시 로드 사용하도록 설정합니다.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("CherryRecorder-Server" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# 1) EchoServer 라이브러리 (또는 소스) 빌드
add_library(CherryRecorder-Server
    src/CherryRecorder-Server.cpp
    include/CherryRecorder-Server.hpp
)
target_include_directories(CherryRecorder-Server
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
find_package(boost_asio CONFIG REQUIRED)
target_link_libraries(CherryRecorder-Server
    PUBLIC Boost::asio
)

# 2) main.cpp로 실행 파일
add_executable(CherryRecorder-Server-App
    src/main.cpp
)
target_link_libraries(CherryRecorder-Server-App
    PRIVATE CherryRecorder-Server
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET CherryRecorder-Server PROPERTY CXX_STANDARD 20)
endif()

# TODO: 필요한 경우 테스트를 추가하고 대상을 설치합니다.
# BUILD_TESTING 옵션이 ON일 때만 테스트 관련 설정을 처리합니다.
option(BUILD_TESTING "Build the tests" ON) # 기본적으로 테스트 빌드 활성화 (CI 등에서 사용)

if (BUILD_TESTING)
  enable_testing()  # CTest 활성화
  find_package(GTest CONFIG REQUIRED)   # GTest 패키지 찾기
  add_executable(CherryRecorder-Server-Testing  # 테스트 실행 파일 정의
      tests/test_main.cpp
      tests/test_echo_server.cpp
  )
  # 테스트 실행 파일에 필요한 라이브러리 링크
  target_link_libraries(CherryRecorder-Server-Testing
      PRIVATE CherryRecorder-Server GTest::gtest_main)

  # CTest에 테스트 추가
  add_test(NAME CherryRecorderServerTest COMMAND CherryRecorder-Server-Testing)
  message(STATUS "BUILD_TESTING is ON. Building tests.")
else()
  message(STATUS "BUILD_TESTING is OFF. Not building tests.")
endif()