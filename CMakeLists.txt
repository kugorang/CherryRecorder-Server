# CMakeList.txt: CherryRecorder-Server에 대한 CMake 프로젝트, 여기에 소스를 포함하고
# 프로젝트 특정 논리를 정의합니다.
#
cmake_minimum_required (VERSION 3.8)

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
enable_testing()
find_package(GTest CONFIG REQUIRED)
add_executable(CherryRecorder-Server-Testing
    tests/test_main.cpp
    tests/test_echo_server.cpp
)
target_link_libraries(CherryRecorder-Server-Testing
    PRIVATE CherryRecorder-Server GTest::gtest_main)

add_test(NAME CherryRecorderServerTest COMMAND CherryRecorder-Server-Testing)