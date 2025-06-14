# CherryRecorder Server

Google Places API와 통합된 C++ HTTP/WebSocket 서버

## ✨ 주요 기능

- ✅ HTTP REST API 서버
- ✅ WebSocket 채팅 서버
- ✅ Google Places API 프록시
- ✅ 멀티 아키텍처 지원 (AMD64/ARM64)
- ✅ Docker 컨테이너화
- ✅ GitHub Actions CI/CD 자동화

## 🚀 빠른 시작

```bash
docker run -d \
  --name cherryrecorder-server \
  -p 8080:8080 \
  -p 33334:33334 \
  -e GOOGLE_MAPS_API_KEY="your-api-key" \
  kugorang/cherryrecorder-server:latest
```

## 🌐 네트워크 아키텍처

### SSL Termination 구조

```
[클라이언트] → HTTPS(443) → [nginx] → HTTP(8080) → [Docker Container]
                                   → WS(33334)  →
```

### HTTPS/WSS 라우팅 원리

nginx는 동일한 443 포트에서 HTTP 헤더를 확인하여 라우팅합니다:
- `Upgrade: websocket` 헤더 있음 → WebSocket (33334)
- `Upgrade: websocket` 헤더 없음 → HTTP API (8080)

## 📋 API 엔드포인트

| 메서드 | 경로 | 설명 |
|--------|------|------|
| GET | `/health` | 헬스체크 |
| GET | `/status` | 서버 상태 |
| GET | `/maps/key` | Google Maps API 키 조회 |
| POST | `/places/nearby` | 주변 장소 검색 |
| POST | `/places/search` | 텍스트로 장소 검색 |
| GET | `/places/details/{id}` | 장소 상세 정보 |
| GET | `/place/photo/{reference}` | 장소 사진 조회 |

### API 사용 예시

```bash
# 주변 장소 검색
curl -X POST https://your-domain.com/api/places/nearby \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 37.5665,
    "longitude": 126.9780,
    "radius": 1500
  }'

# WebSocket 연결 (JavaScript)
const ws = new WebSocket('wss://your-domain.com/ws');
ws.onmessage = (event) => console.log('메시지:', event.data);
```

## 🏠 라즈베리파이 배포

### 1. 네트워크 설정

#### 공유기 포트 포워딩
```
외부 80  → 내부 192.168.0.100:80
외부 443 → 내부 192.168.0.100:443
```

#### 방화벽 설정
```bash
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw allow 22/tcp  # SSH
sudo ufw enable
```

### 2. nginx 설정

```bash
sudo apt update && sudo apt install nginx
sudo nano /etc/nginx/sites-available/cherryrecorder
```

```nginx
server {
    listen 80;
    server_name your-domain.com;
    return 301 https://$server_name$request_uri;
}

server {
    listen 443 ssl http2;
    server_name your-domain.com;
    
    ssl_certificate /etc/letsencrypt/live/your-domain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/your-domain.com/privkey.pem;
    
    location /api/ {
        proxy_pass http://localhost:8080/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
    
    location /ws {
        proxy_pass http://localhost:33334;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400;
    }
}
```

```bash
sudo ln -s /etc/nginx/sites-available/cherryrecorder /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl restart nginx
```

### 3. SSL 인증서

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d your-domain.com
```

### 4. Docker 설치

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
newgrp docker
```

### 5. 자동 업데이트 (Watchtower)

```bash
docker run -d \
  --name watchtower \
  --restart unless-stopped \
  -v /var/run/docker.sock:/var/run/docker.sock \
  containrrr/watchtower \
  --interval 300 \
  --cleanup \
  cherryrecorder-server
```

### 6. 서버 실행

```bash
docker run -d \
  --name cherryrecorder-server \
  --restart unless-stopped \
  -p 8080:8080 \
  -p 33334:33334 \
  -e GOOGLE_MAPS_API_KEY="your-api-key" \
  kugorang/cherryrecorder-server:latest
```

## 🔧 환경 변수

| 변수명 | 설명 | 기본값 |
|--------|------|--------|
| `GOOGLE_MAPS_API_KEY` | **필수** Google Maps API 키 | - |
| `HTTP_PORT` | HTTP API 포트 | 8080 |
| `WS_PORT` | WebSocket 포트 | 33334 |

## 🔄 CI/CD 설정

### GitHub Actions
- **Secrets**: `DOCKERHUB_USERNAME`, `DOCKERHUB_TOKEN`, `GOOGLE_MAPS_API_KEY`
- **Variables**: `DOCKERHUB_REPO`, `SERVER_ADDRESS`, `HTTP_PORT_VALUE`, `WS_PORT_VALUE`

### 자동화 프로세스
1. 코드 Push → GitHub Actions 트리거
2. C++ 빌드/테스트 → Docker 이미지 생성
3. Docker Hub 푸시 → Watchtower 감지
4. 컨테이너 자동 업데이트

## 🛠 개발

### 빌드
```bash
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 의존성
- Boost.Beast (HTTP/WebSocket)
- Boost.JSON
- OpenSSL
- spdlog

## 🔍 트러블슈팅

```bash
# 포트 확인
sudo netstat -tlnp | grep -E '(80|443|8080|33334)'

# 로그 확인
docker logs cherryrecorder-server -f
sudo tail -f /var/log/nginx/error.log
```

## 📄 라이선스

BSD 3-Clause License

## 💬 지원

- [GitHub Issues](https://github.com/kugorang/cherryrecorder-server/issues)
- [API Docs](https://kugorang.github.io/cherryrecorder-server)
