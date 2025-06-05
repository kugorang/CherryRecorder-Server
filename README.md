# 🍒 CherryRecorder-Server

[![CI/CD](https://github.com/kugorang/CherryRecorder-Server/actions/workflows/main-ci-cd.yml/badge.svg)](https://github.com/kugorang/CherryRecorder-Server/actions/workflows/main-ci-cd.yml)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://kugorang.github.io/CherryRecorder-Server/)
[![License](https://img.shields.io/badge/License-BSD_3--Clause-green.svg)](LICENSE.txt)
[![C++](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)

C++20으로 구현된 CherryRecorder의 고성능 백엔드 서버입니다. 위치 기반 서비스와 실시간 채팅 기능을 제공합니다.

## 📋 목차

- [프로젝트 소개](#-프로젝트-소개)
- [아키텍처](#-아키텍처)
- [시작하기](#-시작하기)
- [개발 환경 설정](#-개발-환경-설정)
- [실행 방법](#-실행-방법)
- [API 문서](#-api-문서)
- [CI/CD](#-cicd)
- [배포](#-배포)
- [테스트](#-테스트)
- [문제 해결](#-문제-해결)

## 📱 프로젝트 소개

CherryRecorder Server는 사용자의 위치 기반 혜택 정보를 관리하고, 실시간 채팅 서비스를 제공하는 백엔드 시스템입니다.

### 주요 기능

1. **🌐 HTTP API 서버 (포트 8080)**
   - 헬스체크 엔드포인트 (`/health`)
   - Google Places API 프록시
     - `/places/nearby` - 주변 장소 검색
     - `/places/search` - 텍스트 기반 장소 검색
     - `/places/details` - 장소 상세 정보

2. **💬 WebSocket 채팅 서버 (포트 33334)**
   - 실시간 메시지 전송
   - 닉네임 기반 사용자 관리
   - 브로드캐스트 메시징

3. **🔌 TCP Echo 서버 (포트 33333)**
   - 네트워크 테스트 및 디버깅용
   - 연결 상태 확인

## 🏗️ 아키텍처

### 기술 스택

- **언어**: C++20
- **빌드 시스템**: CMake 3.20+
- **패키지 관리**: vcpkg
- **주요 라이브러리**:
  - Boost.Beast (HTTP/WebSocket)
  - Boost.Asio (비동기 I/O)
  - OpenSSL (TLS/SSL)
  - Google Test (단위 테스트)
- **문서화**: Doxygen
- **컨테이너**: Docker
- **CI/CD**: GitHub Actions
- **배포**: AWS ECS

### 프로젝트 구조

```
CherryRecorder-Server/
├── .github/
│   ├── workflows/           # CI/CD 워크플로우
│   └── task-definition.json.template
├── docs/
│   └── Doxyfile            # Doxygen 설정
├── include/                # 헤더 파일
│   ├── CherryRecorder-Server.hpp
│   ├── HttpServer.hpp
│   └── handlers/
│       └── PlacesApiHandler.hpp
├── src/                    # 소스 파일
│   ├── main.cpp
│   ├── CherryRecorder-Server.cpp
│   ├── HttpServer.cpp
│   └── handlers/
│       └── PlacesApiHandler.cpp
├── tests/                  # 테스트 코드
├── CMakeLists.txt         # CMake 설정
├── vcpkg.json             # 의존성 정의
└── Dockerfile             # 컨테이너 빌드
```

## 🚀 시작하기

### 사전 요구사항

- CMake 3.20 이상
- C++20 지원 컴파일러 (GCC 11+, Clang 13+, MSVC 2019+)
- vcpkg 패키지 매니저
- Google Maps API 키

### 빠른 시작

```bash
# 저장소 클론 (서브모듈 포함)
git clone --recursive https://github.com/kugorang/CherryRecorder-Server.git
cd CherryRecorder-Server

# 환경 변수 설정
cp .env.example .env
# .env 파일을 편집하여 GOOGLE_MAPS_API_KEY 설정

# 빌드 및 실행 (Windows)
./local_build_and_run.bat

# 빌드 및 실행 (Linux/Mac)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/CherryRecorder-Server
```

## ⚙️ 개발 환경 설정

### 1. vcpkg 설정

프로젝트는 vcpkg를 서브모듈로 포함하고 있습니다:

```bash
# vcpkg 서브모듈 초기화
git submodule update --init --recursive

# vcpkg 부트스트랩 (Windows)
./vcpkg/bootstrap-vcpkg.bat

# vcpkg 부트스트랩 (Linux/Mac)
./vcpkg/bootstrap-vcpkg.sh
```

### 2. 환경 변수 설정

`.env` 파일 생성:

```env
# Google Maps API 키 (필수)
GOOGLE_MAPS_API_KEY=your_api_key_here

# 서버 설정 (선택사항 - 기본값 있음)
ECHO_SERVER_IP=0.0.0.0
ECHO_SERVER_PORT=33333
HEALTH_CHECK_IP=0.0.0.0
HEALTH_CHECK_PORT=8080
HTTP_THREADS=4
```

### 3. IDE 설정

#### Visual Studio Code

1. C/C++ Extension 설치
2. CMake Tools Extension 설치
3. 프로젝트 열기 후 CMake Configure 실행

#### Visual Studio

1. "Open a local folder" 선택
2. CMakePresets.json이 자동으로 인식됨
3. 빌드 구성 선택 후 빌드

## 🏃‍♂️ 실행 방법

### 로컬 개발

```bash
# Debug 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 실행
./build/CherryRecorder-Server
```

### Docker 실행

```bash
# 이미지 빌드
docker build -t cherryrecorder-server .

# 컨테이너 실행
docker run -p 8080:8080 \
  --env-file .env \
  cherryrecorder-server
```

### Docker Compose

```yaml
version: '3.8'
services:
  server:
    image: kugorang/cherryrecorder-server:latest
    ports:
      - "8080:8080"    # HTTP API
    env_file:
      - .env
    restart: unless-stopped
```

## 📚 API 문서

### HTTP API (포트 8080)

#### 헬스체크
```http
GET /health

Response:
{
  "status": "ok",
  "timestamp": "2024-01-01T00:00:00Z"
}
```

#### 주변 장소 검색
```http
GET /places/nearby?location=37.5665,126.9780&radius=1000&type=restaurant

Parameters:
- location: 위도,경도 (필수)
- radius: 검색 반경(미터) (필수)
- type: 장소 유형 (선택)
```

#### 텍스트 검색
```http
GET /places/search?query=커피&location=37.5665,126.9780&radius=1000

Parameters:
- query: 검색어 (필수)
- location: 위도,경도 (선택)
- radius: 검색 반경(미터) (선택)
```

#### 장소 상세정보
```http
GET /places/details?place_id=ChIJN1t_tDeuEmsRUsoyG83frY4

Parameters:
- place_id: Google Place ID (필수)
```

### WebSocket 채팅 (포트 33334)

WebSocket 연결 후 JSON 메시지 교환:

```javascript
// 연결
ws = new WebSocket('ws://localhost:33334');

// 메시지 전송
ws.send(JSON.stringify({
  type: 'message',
  nickname: '사용자1',
  content: '안녕하세요!'
}));
```

## 🚀 CI/CD

### GitHub Actions 워크플로우

프로젝트는 완전 자동화된 CI/CD 파이프라인을 사용합니다:

1. **빌드 & 테스트**: Linux 환경에서 C++ 빌드 및 단위 테스트
2. **문서 생성**: Doxygen을 통한 API 문서 자동 생성
3. **GitHub Pages 배포**: 생성된 문서를 자동 배포
4. **Docker 빌드**: 멀티 스테이지 빌드로 최적화된 이미지 생성
5. **레지스트리 푸시**: 
   - GitHub Container Registry (GHCR)
   - Docker Hub
   - AWS ECR
6. **AWS ECS 배포**: 프로덕션 환경 자동 배포
7. **연결 테스트**: 배포 후 자동 헬스체크

### 필요한 GitHub Secrets

```yaml
# AWS 관련
AWS_ROLE_TO_ASSUME: OIDC 연동 IAM 역할 ARN
TASK_EXECUTION_ROLE_ARN: ECS Task 실행 역할 ARN

# Docker Hub
DOCKERHUB_USERNAME: Docker Hub 사용자명
DOCKERHUB_TOKEN: Docker Hub 액세스 토큰

# API 키
GOOGLE_MAPS_API_KEY: Google Maps API 키
```

### 필요한 GitHub Variables

```yaml
# AWS 설정
AWS_REGION: ap-northeast-2
AWS_ACCOUNT_ID: 123456789012
AWS_ECR_REPOSITORY: cherryrecorder-server

# ECS 설정
ECS_CLUSTER_NAME: cherryrecorder-cluster
ECS_SERVICE_NAME: cherryrecorder-service
ECS_TASK_DEF_FAMILY: cherryrecorder-task
CONTAINER_NAME: cherryrecorder-server

# 서버 설정
SERVER_ADDRESS: cherryrecorder-nlb.elb.amazonaws.com
HEALTH_CHECK_PORT_VALUE: 8080
```

## 🚢 배포

### Docker 이미지

최신 이미지는 다음 레지스트리에서 사용 가능합니다:

```bash
# Docker Hub
docker pull kugorang/cherryrecorder-server:latest

# GitHub Container Registry  
docker pull ghcr.io/kugorang/cherryrecorder-server:latest

# AWS ECR (인증 필요)
docker pull 123456789012.dkr.ecr.ap-northeast-2.amazonaws.com/cherryrecorder-server:latest
```

### AWS ECS 배포

ECS 배포는 GitHub Actions를 통해 자동으로 이루어집니다:

1. main 브랜치에 푸시
2. CI/CD 파이프라인 자동 실행
3. 새 Task Definition 생성
4. ECS 서비스 업데이트
5. 롤링 배포 수행

## 🧪 테스트

### 단위 테스트 실행

```bash
# 테스트 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 테스트 실행
cd build
ctest --output-on-failure
```

### 통합 테스트

```bash
# Docker 환경에서 테스트
docker build -f Dockerfile.test -t cherryrecorder-test .
docker run --rm cherryrecorder-test
```

## 🔧 문제 해결

### Google Maps API 404 오류

1. `.env` 파일에 유효한 API 키가 설정되어 있는지 확인
2. Google Cloud Console에서 Places API가 활성화되어 있는지 확인
3. API 키에 IP 제한이 있다면 서버 IP 추가

### 빌드 실패

1. vcpkg 서브모듈이 제대로 초기화되었는지 확인
2. C++20을 지원하는 컴파일러인지 확인
3. CMake 버전이 3.20 이상인지 확인

### Docker 실행 오류

1. 모든 필요한 포트가 사용 가능한지 확인
2. `.env` 파일이 올바른 위치에 있는지 확인
3. 컨테이너 로그 확인: `docker logs <container-id>`

## 📄 라이선스

이 프로젝트는 BSD 3-Clause 라이선스 하에 배포됩니다. 자세한 내용은 [LICENSE.txt](LICENSE.txt) 파일을 참조하세요.

## 🤝 기여하기

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📞 지원

- 이슈 트래커: [GitHub Issues](https://github.com/kugorang/CherryRecorder-Server/issues)
- 문서: [Doxygen Documentation](https://kugorang.github.io/CherryRecorder-Server/)

---

Made with ❤️ by CherryRecorder Team
