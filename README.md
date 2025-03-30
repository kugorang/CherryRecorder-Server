# CherryRecorder-Server

본 프로젝트는 C++로 작성된 백엔드 서버로, MySQL DB와 연동하여 위치 기반 기능을 제공합니다.  
Docker, Kubernetes, vcpkg, Doxygen 등을 사용한 CI/CD 및 문서 자동화도 포함하고 있습니다.

## Doxygen

<https://kugorang.github.io/CherryRecorder-Server/>

## Directory Structure

```plaintext
CherryRecorder-Server/
├── .github/
│   └── workflows/
│       └── ci.yml     # GitHub Actions (CI)
├── docs/
│   └── Doxyfile     # Doxygen 설정
├── include/
│   └── CherryRecorder-Server.hpp
├── src/
│   ├── main.cpp     # 서버 실행 진입점
│   └── CherryRecorder-Server.cpp # Echo Server 구현
├── tests/
│   ├── test_main.cpp    # GTest 메인
│   └── test_echo_server.cpp  # Echo Server 동작 테스트
├── vcpkg/       # vcpkg 서브모듈
│   └── ... (생략)
├── CMakeLists.txt     # 전체 CMake 설정
├── CMakePresets.json    # CMake 프리셋 설정
├── LICENSE.txt      # BSD 3-Clause License
├── README.md
└── vcpkg.json      # vcpkg manifest (dependencies: boost, gtest)
```
