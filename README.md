# CherryRecorder Server

WebSocket 기반 실시간 채팅 서버 및 Google Maps Places API 프록시 서버

## 🚀 주요 기능

- **HTTP API 서버**: Google Maps Places API 프록시
- **WebSocket 채팅 서버**: 실시간 멀티룸 채팅 지원
- **Docker 지원**: 멀티 아키텍처 이미지 (AMD64/ARM64)
- **자동 배포**: GitHub Actions + Docker Hub + Watchtower

## 📋 시스템 요구사항

- Ubuntu 22.04/24.04
- CMake 3.20+
- C++20 지원 컴파일러
- Docker (선택사항)

## 🔧 빠른 시작

### Docker를 사용한 실행 (권장)

```bash
# Docker Hub에서 이미지 받기
docker pull kugorang/cherryrecorder-server:latest

# 환경 변수 파일 생성
cat > .env.docker << EOF
GOOGLE_MAPS_API_KEY=your_google_maps_api_key_here
HISTORY_DIR=/home/appuser/app/history
HTTP_PORT=8080
EOF

# 실행
docker run -d \
  --name cherryrecorder-server \
  --env-file .env.docker \
  -p 8080:8080 \
  -p 33334:33334 \
  -v $(pwd)/history:/home/appuser/app/history \
  kugorang/cherryrecorder-server:latest
```

### 로컬 빌드 및 실행

```bash
# 의존성 설치 (vcpkg)
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install

# 빌드
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 환경 변수 설정
export GOOGLE_MAPS_API_KEY=your_google_maps_api_key_here

# 실행
./build/CherryRecorder-Server-App
```

## 🌐 API 엔드포인트

### HTTP API (포트 8080)

| 메서드 | 경로 | 설명 |
|--------|------|------|
| GET | `/health` | 헬스체크 |
| GET | `/status` | 서버 상태 |
| GET | `/maps/key` | Google Maps API 키 반환 |
| POST | `/places/nearby` | 주변 장소 검색 |
| POST | `/places/search` | 텍스트 기반 장소 검색 |
| GET | `/places/details/{placeId}` | 장소 상세정보 |
| GET | `/place/photo/{photoRef}` | 장소 사진 |

#### 요청 예시

**주변 장소 검색**
```bash
curl -X POST http://localhost:8080/places/nearby \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 37.5665,
    "longitude": 126.9780,
    "radius": 500
  }'
```

**텍스트 검색**
```bash
curl -X POST http://localhost:8080/places/search \
  -H "Content-Type: application/json" \
  -d '{
    "query": "강남역 카페",
    "latitude": 37.5665,
    "longitude": 126.9780,
    "radius": 5000
  }'
```

**장소 상세정보**
```bash
curl http://localhost:8080/places/details/ChIJxxxxxxxxxxxxxx
```

### WebSocket (포트 33334)
- 엔드포인트: `/chat`
- 메시지 형식: JSON
  ```json
  {
    "type": "join|leave|message",
    "room": "room_name",
    "message": "content",
    "nickname": "user_nickname"
  }
  ```

## 🏗️ 아키텍처

- **Boost.Beast**: HTTP/WebSocket 서버
- **nlohmann/json**: JSON 파싱
- **spdlog**: 로깅
- **cpr**: HTTP 클라이언트
- **SQLite3**: 메시지 히스토리 저장

## 🚀 CI/CD

### GitHub Actions 워크플로우
1. `main` 브랜치 푸시 시 자동 실행
2. C++ 빌드 및 테스트
3. Docker 이미지 빌드 (멀티 아키텍처)
4. Docker Hub 푸시
5. Watchtower가 자동으로 프로덕션 서버 업데이트

### 필요한 GitHub Secrets
- `DOCKERHUB_USERNAME`: Docker Hub 사용자명
- `DOCKERHUB_TOKEN`: Docker Hub 액세스 토큰
- `GOOGLE_MAPS_API_KEY`: Google Maps API 키

### 필요한 GitHub Variables
- `DOCKERHUB_REPO`: Docker Hub 리포지토리명
- `SERVER_ADDRESS`: 서버 주소 (예: example.com)
- `HTTP_PORT_VALUE`: HTTP 포트 (기본: 8080)
- `WS_PORT_VALUE`: WebSocket 포트 (기본: 33334)

## 🐳 Docker 이미지

- **최신 버전**: `kugorang/cherryrecorder-server:latest`
- **지원 아키텍처**: linux/amd64, linux/arm64
- **베이스 이미지**: Ubuntu 24.04

## 🔒 프로덕션 배포

### nginx 리버스 프록시 설정 예시

```nginx
server {
    listen 443 ssl http2;
    server_name example.com;

    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;

    # API 프록시 (/api 접두사 제거)
    location /api/ {
        proxy_pass http://localhost:8080/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    location /ws {
        proxy_pass http://localhost:33334;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

### Watchtower 자동 배포

```bash
docker run -d \
  --name watchtower \
  -v /var/run/docker.sock:/var/run/docker.sock \
  containrrr/watchtower \
  --interval 300 \
  cherryrecorder-server
```

## 📝 환경 변수

| 변수명 | 설명 | 기본값 | 필수 |
|--------|------|--------|------|
| `GOOGLE_MAPS_API_KEY` | Google Maps API 키 | - | ✓ |
| `HTTP_PORT` | HTTP 서버 포트 | 8080 | |
| `HISTORY_DIR` | 채팅 히스토리 저장 경로 | ./history | |

## 📄 라이센스

이 프로젝트는 BSD 3-Clause 라이센스 하에 배포됩니다.

## 👤 개발자

- **Kim Hyeonwoo** - [kugorang](https://github.com/kugorang)
- 이메일: ialskdji@gmail.com

## 🤝 기여

1. 프로젝트를 Fork 합니다
2. 기능 브랜치를 생성합니다 (`git checkout -b feature/AmazingFeature`)
3. 변경사항을 커밋합니다 (`git commit -m 'Add some AmazingFeature'`)
4. 브랜치에 푸시합니다 (`git push origin feature/AmazingFeature`)
5. Pull Request를 생성합니다