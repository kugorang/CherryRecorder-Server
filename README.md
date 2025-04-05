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
│       ├── main-ci-cd.yml
│       ├── reusable-build-test.yml
│       ├── reusable-connection-check.yml
│       ├── reusable-deploy-aws.yml
│       ├── reusable-docker-build.yml
│       ├── reusable-docker-push-dh.yml
│       ├── reusable-docker-push-ecr.yml
│       └── reusable-docs-build.yml
├── docs/
│   └── Doxyfile					# Doxygen 설정
├── include/
│   ├── CherryRecorder-Server.hpp   # 기존 Echo Server 헤더 (TCP)
│   ├── HttpServer.hpp              # (신규) Proxygen HTTP 서버 헤더
│   └── HealthCheckHandler.hpp      # (신규) Health Check 핸들러 헤더
├── src/
│   ├── main.cpp                    # (수정) 메인 함수 (두 서버 실행)
│   ├── CherryRecorder-Server.cpp   # (유지) 기존 Echo Server 구현 (TCP)
│   ├── HttpServer.cpp              # (신규) Proxygen HTTP 서버 구현
│   └── HealthCheckHandler.cpp      # (신규) Health Check 핸들러 구현
├── tests/
│   ├── test_main.cpp               # GTest 메인
│   ├── test_echo_server.cpp        # (유지) Echo Server 테스트 (TCP)
│   └── test_http_server.cpp        # (신규) HTTP Server (Health Check) 테스트 (선택)
├── vcpkg/       # vcpkg 서브모듈
│   └── ... (생략)
├── CMakeLists.txt                  # (수정) 전체 CMake 설정 (Proxygen 추가)
├── CMakePresets.json               # CMake 프리셋 설정
├── LICENSE.txt                     # BSD 3-Clause License
├── README.md                       # 프로젝트 설명
└── vcpkg.json                      # (수정) vcpkg manifest (Boost, GTest, Proxygen)
```
