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
│   └── Doxyfile                    # Doxygen 설정
├── include/
│   ├── CherryRecorder-Server.hpp   # TCP Echo Server 헤더
│   ├── HttpServer.hpp              # HTTP 서버 헤더
│   ├── ChatServer.hpp              # WebSocket 채팅 서버 헤더
│   └── handlers/
│       └── PlacesApiHandler.hpp    # Google Places API 핸들러 헤더
├── src/
│   ├── main.cpp                    # 메인 함수 (세 서버 실행)
│   ├── CherryRecorder-Server.cpp   # TCP Echo Server 구현
│   ├── HttpServer.cpp              # HTTP 서버 구현
│   ├── ChatServer.cpp              # 채팅 서버 구현
│   └── handlers/
│       └── PlacesApiHandler.cpp    # Google Places API 핸들러 구현
├── tests/
│   ├── test_main.cpp               # GTest 메인
│   ├── test_echo_server.cpp        # Echo Server 테스트
│   ├── test_http_server.cpp        # HTTP Server 테스트
│   └── test_chat_server.cpp        # 채팅 서버 테스트
├── vcpkg/       # vcpkg 서브모듈
│   └── ... (생략)
├── CMakeLists.txt                  # CMake 설정
├── CMakePresets.json               # CMake 프리셋 설정
├── LICENSE.txt                     # BSD 3-Clause License
├── README.md                       # 프로젝트 설명
└── vcpkg.json                      # vcpkg manifest (Boost, GTest 등)
```

## 주요 기능

프로젝트는 다음 세 가지 주요 서버 기능을 포함하고 있습니다:

1. **TCP Echo Server**: 클라이언트로부터 받은 데이터를 그대로 돌려보내는 기본 TCP 서버
2. **HTTP 서버**: 다음 기능을 제공하는 HTTP 서버
   - `/health` 엔드포인트: 서버 상태 확인용 API
   - Google Places API 연동: 위치 기반 검색 기능
     - `/places/nearby`: 주변 장소 검색
     - `/places/search`: 텍스트 기반 장소 검색
     - `/places/details`: 특정 장소 상세 정보
   - Google Places API와의 HTTPS 통신을 위해 TLS 지원을 제공하는 OpenSSL을 사용
3. **채팅 서버**: 웹소켓 기반 채팅 서비스
   - 클라이언트 간 실시간 메시지 전송
   - 닉네임 기반 사용자 관리

## 환경 설정 가이드

### 환경 변수 설정

프로젝트 실행을 위해 환경 변수 파일을 사용합니다:

```bash
# 환경 변수 파일 생성
cp .env.example .env
```

파일을 편집하여 필요한 설정을 구성하세요.

### 빌드 및 실행

CMake를 사용하여 프로젝트를 빌드합니다:

```bash
# Release 모드 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### Docker 컨테이너 빌드

Docker 이미지를 빌드할 수 있습니다:

```bash
# Docker 이미지 빌드
docker build -t cherryrecorder-server .
```

### GitHub Actions CI/CD

GitHub Actions를 통한 CI/CD 파이프라인에서는:

- Main 브랜치 빌드: Release 모드로 빌드되어 AWS ECS에 배포됩니다.

## 실행 방법

### 로컬 실행 (Windows)

1. .env 파일 준비: .env.example을 참고하여 .env 파일을 생성하고 Google Maps API 키를 설정합니다.
2. build_and_run.bat 스크립트 실행:

```bash
build_and_run.bat
```

### Docker 실행

- 이미지 빌드:

```bash
# 이미지 빌드 (민감한 정보는 포함하지 않음)
docker build -t cherryrecorder-server .
```

## 보안 관련 참고사항

- 민감한 정보(API 키 등)는 Docker 이미지에 포함하지 않습니다.
- 항상 런타임에 환경 변수 파일(.env)을 통해 제공합니다.
- .env 파일은 반드시 .gitignore에 추가하여 버전 관리에서 제외해야 합니다.

## 문제 해결

### API 요청 404 오류

Google Maps API 관련 404 오류가 발생하는 경우 다음을 확인하세요:

1. .env 파일에 유효한 `GOOGLE_MAPS_API_KEY` 설정 여부
2. Docker 실행 시 환경 변수 파일 전달 여부 (`--env-file` 옵션)
3. API 키의 유효성 및 활성화 상태 (Google Cloud Console에서 확인)

문제가 지속되면 로그를 확인하고 관리자에게 문의하세요.
