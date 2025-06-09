# Let's Encrypt를 사용한 무료 SSL 인증서 설정

## 1. Certbot 설치

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install certbot

# Amazon Linux 2
sudo yum install -y certbot
```

## 2. 인증서 발급

### 독립 실행형 모드 (서버 중단 없이)
```bash
# HTTP-01 challenge 사용 (80번 포트 필요)
sudo certbot certonly --standalone \
  -d cherryrecorder.kugora.ng \
  --agree-tos \
  --email your-email@example.com \
  --non-interactive
```

### DNS-01 challenge 사용 (80번 포트 불필요)
```bash
# Route53 사용 시
sudo certbot certonly \
  --dns-route53 \
  -d cherryrecorder.kugora.ng \
  --agree-tos \
  --email your-email@example.com
```

## 3. 인증서 위치

발급된 인증서는 다음 위치에 저장됩니다:
- 인증서: `/etc/letsencrypt/live/cherryrecorder.kugora.ng/fullchain.pem`
- 개인키: `/etc/letsencrypt/live/cherryrecorder.kugora.ng/privkey.pem`

## 4. 서버 설정 업데이트

### server-config.env
```bash
USE_SSL=true
SSL_CERT_PATH=/etc/letsencrypt/live/cherryrecorder.kugora.ng/fullchain.pem
SSL_KEY_PATH=/etc/letsencrypt/live/cherryrecorder.kugora.ng/privkey.pem
```

### Docker Compose 볼륨 마운트
```yaml
services:
  cherryrecorder-server:
    volumes:
      - /etc/letsencrypt:/etc/letsencrypt:ro
```

## 5. 자동 갱신 설정

```bash
# Cron job 추가
sudo crontab -e

# 매일 새벽 2시에 갱신 시도
0 2 * * * certbot renew --post-hook "docker restart cherryrecorder-server"
```

## 6. NLB 설정 변경

Let's Encrypt 인증서를 사용하면 NLB에서 SSL 처리가 불필요합니다:

1. **TCP 패스스루 모드로 변경**
   - 리스너: TCP 58080 → TCP 8080
   - 리스너: TCP 33335 → TCP 33335

2. **서버에서 직접 SSL 처리**
   - 서버가 유효한 인증서로 HTTPS/WSS 제공
   - 앱에서 인증서 검증 문제 없음

## 7. 안드로이드 네트워크 설정 간소화

```xml
<?xml version="1.0" encoding="utf-8"?>
<network-security-config>
    <!-- 공인 인증서 사용 시 기본 설정만으로 충분 -->
    <base-config cleartextTrafficPermitted="false">
        <trust-anchors>
            <certificates src="system" />
        </trust-anchors>
    </base-config>
    
    <!-- 개발 환경만 예외 -->
    <domain-config cleartextTrafficPermitted="true">
        <domain includeSubdomains="true">10.0.2.2</domain>
    </domain-config>
</network-security-config>
```

## 장점

1. **무료**: Let's Encrypt는 완전 무료
2. **자동화**: 갱신 프로세스 자동화 가능
3. **신뢰성**: 모든 브라우저/앱에서 신뢰
4. **보안**: 최신 TLS 표준 지원 