# CherryRecorder Server

Google Places API와 통합된 C++ HTTP/WebSocket 서버

## 🚀 빠른 시작

```bash
docker run -d \
  --name cherryrecorder-server \
  -p 8080:8080 \
  -p 33334:33334 \
  -e GOOGLE_MAPS_API_KEY="your-api-key" \
  kugorang/cherryrecorder-server:latest
```

## 🌐 네트워크 아키텍처 이해하기

### 핵심 원칙: SSL Termination

CherryRecorder Server는 **HTTP(8080)와 WebSocket(33334)만 제공**합니다.  
SSL/TLS 암호화는 nginx에서 처리합니다 (SSL Termination).

### 443 포트에서 HTTPS와 WSS 구분 원리

```
[사용자 브라우저]
    |
    | https://example.com/api → HTTPS 요청
    | wss://example.com/ws   → WSS 요청 (Upgrade 헤더 포함)
    ↓
[nginx - 443 포트]
    |
    | HTTP 헤더 확인:
    | - Upgrade: websocket 있음 → WebSocket으로 판단
    | - Upgrade: websocket 없음 → 일반 HTTPS로 판단
    ↓
[라우팅 결정]
    |
    ├─ /api/* → localhost:8080 (HTTP)
    └─ /ws    → localhost:33334 (WebSocket)
```

**기술적 설명**:
1. HTTPS와 WSS 모두 TLS 위에서 동작하므로 같은 443 포트 사용 가능
2. WebSocket은 HTTP에서 시작하여 프로토콜을 전환 (Upgrade)
3. nginx는 `Upgrade: websocket` 헤더를 보고 WebSocket 요청 식별

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

## 🏠 라즈베리파이 배포 가이드

### 1. 사전 준비

#### 도메인 설정
- 도메인을 라즈베리파이의 공인 IP에 연결
- DuckDNS, No-IP 같은 무료 DDNS 서비스 활용 가능

#### 공유기 포트 포워딩
```
외부 80  → 내부 192.168.0.100:80
외부 443 → 내부 192.168.0.100:443
```

### 2. nginx 설치 및 설정

```bash
sudo apt update && sudo apt install nginx
sudo nano /etc/nginx/sites-available/cherryrecorder
```

#### nginx 설정 파일
```nginx
# HTTP → HTTPS 리다이렉트
server {
    listen 80;
    server_name cherryrecorder.example.com;
    return 301 https://$server_name$request_uri;
}

# HTTPS 서버 블록
server {
    listen 443 ssl http2;
    server_name cherryrecorder.example.com;
    
    # SSL 인증서 (Let's Encrypt)
    ssl_certificate /etc/letsencrypt/live/cherryrecorder.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/cherryrecorder.example.com/privkey.pem;
    
    # SSL 보안 설정
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_prefer_server_ciphers on;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256;
    
    # API 프록시 (HTTPS → HTTP)
    location /api/ {
        proxy_pass http://localhost:8080/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        
        # CORS 헤더 (필요시)
        add_header Access-Control-Allow-Origin *;
    }
    
    # WebSocket 프록시 (WSS → WS)
    location /ws {
        proxy_pass http://localhost:33334;
        proxy_http_version 1.1;
        
        # WebSocket 필수 헤더
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        
        # 추가 헤더
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        
        # 타임아웃 설정 (WebSocket 연결 유지)
        proxy_read_timeout 86400;
    }
    
    # 정적 파일 (React 앱 등)
    location / {
        root /var/www/cherryrecorder;
        try_files $uri $uri/ /index.html;
    }
}
```

#### nginx 활성화
```bash
sudo ln -s /etc/nginx/sites-available/cherryrecorder /etc/nginx/sites-enabled/
sudo nginx -t  # 설정 검증
sudo systemctl restart nginx
```

### 3. SSL 인증서 발급 (Let's Encrypt)

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d cherryrecorder.example.com
```

### 4. Docker 설치

```bash
# Docker 설치 스크립트
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
newgrp docker
```

### 5. Watchtower 설치 (자동 업데이트)

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

**Watchtower 기능**:
- 5분마다 Docker Hub에서 새 이미지 확인
- 업데이트 발견 시 자동으로 컨테이너 재시작
- 이전 이미지 자동 정리

### 6. CherryRecorder Server 실행

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
| `HTTP_BIND_IP` | 바인드 IP 주소 | 0.0.0.0 |
| `HTTP_THREADS` | 워커 스레드 수 | CPU 코어 수 |

## 🔄 CI/CD 파이프라인

### GitHub Actions 자동화 흐름

```
[코드 Push] 
    ↓
[GitHub Actions]
    ├─ 1. C++ 빌드 및 테스트
    ├─ 2. 문서 생성
    ├─ 3. Docker 이미지 빌드 (ARM64/AMD64)
    ├─ 4. Docker Hub 푸시
    └─ 5. 연결 확인 (Watchtower 대기)
    ↓
[Watchtower - 라즈베리파이]
    ├─ 새 이미지 감지
    └─ 컨테이너 자동 업데이트
```

### 필수 GitHub 설정

**Secrets**:
- `DOCKERHUB_USERNAME`: Docker Hub 사용자명
- `DOCKERHUB_TOKEN`: Docker Hub 액세스 토큰
- `GOOGLE_MAPS_API_KEY`: Google Maps API 키

**Variables**:
- `DOCKERHUB_REPO`: `cherryrecorder-server`
- `SERVER_ADDRESS`: 서버 도메인 주소
- `HTTP_PORT_VALUE`: `8080`
- `WS_PORT_VALUE`: `33334`

## 🔍 트러블슈팅

### 연결 테스트

```bash
# 포트 확인
sudo netstat -tlnp | grep -E '(80|443|8080|33334)'

# nginx 로그
sudo tail -f /var/log/nginx/access.log
sudo tail -f /var/log/nginx/error.log

# Docker 로그
docker logs cherryrecorder-server -f

# Watchtower 로그
docker logs watchtower -f
```

### WebSocket 연결 테스트

```javascript
// 브라우저 콘솔에서 테스트
const ws = new WebSocket('wss://cherryrecorder.example.com/ws');
ws.onopen = () => console.log('연결됨');
ws.onmessage = (e) => console.log('메시지:', e.data);
ws.onerror = (e) => console.error('에러:', e);
```

### 일반적인 문제 해결

**502 Bad Gateway**
- Docker 컨테이너 실행 확인: `docker ps`
- 포트 매핑 확인: `docker port cherryrecorder-server`

**WebSocket 연결 실패**
- nginx 설정에서 `Upgrade` 헤더 확인
- 프록시 타임아웃 설정 확인

**인증서 갱신**
```bash
sudo certbot renew --dry-run  # 테스트
sudo certbot renew            # 실제 갱신
```

## 🛠 개발 환경

### 소스에서 빌드

```bash
git clone https://github.com/kugorang/cherryrecorder-server.git
cd cherryrecorder-server

cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build

export GOOGLE_MAPS_API_KEY="your-api-key"
./build/CherryRecorder-Server-App
```

### 로컬 개발 팁

개발 중에는 nginx 없이 직접 접속:
- HTTP API: `http://localhost:8080`
- WebSocket: `ws://localhost:33334`

## 📄 라이선스

BSD 3-Clause License - [LICENSE](LICENSE) 파일 참조

## 🤝 기여하기

1. 저장소 포크
2. 기능 브랜치 생성
3. 변경사항 커밋
4. 브랜치 푸시
5. Pull Request 생성

## 💬 지원

- 이슈: [GitHub Issues](https://github.com/kugorang/cherryrecorder-server/issues)
- 문서: [API Docs](https://kugorang.github.io/cherryrecorder-server)
