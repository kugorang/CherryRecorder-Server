# AWS ALB Best Practices for CherryRecorder

## 아키텍처 개요

```
Internet → ALB (SSL Termination) → EC2 (Plain Text)
         ↓                        ↓
    58080 (HTTPS)            8080 (HTTP)
    33335 (WSS)              33334 (WS)
```

## Health Check Best Practice

### ✅ 권장 방식: HTTP Health Check

ALB 대상 그룹의 Health Check는 EC2 인스턴스와 직접 통신하므로 HTTP를 사용해야 합니다.

```yaml
# 대상 그룹 health check 설정
Protocol: HTTP
Port: 8080
Path: /health
HealthyThresholdCount: 2
UnhealthyThresholdCount: 3
Timeout: 5
Interval: 30
Matcher: 200
```

### ❌ 비권장: HTTPS Health Check

EC2에서 HTTPS를 사용하면:
- 불필요한 SSL 인증서 관리
- 성능 오버헤드
- 복잡성 증가

## 보안 그룹 설정

### 1. ALB 보안 그룹

```bash
# 인바운드 규칙
- 58080/tcp from 0.0.0.0/0 (HTTPS)
- 33335/tcp from 0.0.0.0/0 (WSS)

# 아웃바운드 규칙
- All traffic to EC2 보안 그룹
```

### 2. EC2 보안 그룹

```bash
# 인바운드 규칙
- 8080/tcp from ALB 보안 그룹
- 33334/tcp from ALB 보안 그룹
- 22/tcp from 관리자 IP (SSH)

# 아웃바운드 규칙
- All traffic to 0.0.0.0/0
```

## 서버 환경 변수 설정

```bash
# EC2 인스턴스에서 (docker-compose.yml 또는 환경 변수)
HTTP_PORT=8080
WS_PORT=33334
WSS_PORT=33335  # 사용하지 않음 (ALB에서 처리)
HTTPS_PORT=58080  # 사용하지 않음 (ALB에서 처리)

# SSL 인증서 설정 불필요
# SSL_CERT_FILE=
# SSL_KEY_FILE=
```

## 클라이언트 설정

```dart
// Flutter 클라이언트
--dart-define=API_BASE_URL=https://cherryrecorder.kugora.ng:58080
--dart-define=CHAT_SERVER_IP=cherryrecorder.kugora.ng
--dart-define=CHAT_SERVER_PORT=33335
--dart-define=USE_WSS=true
```

## 모니터링 및 로깅

### CloudWatch 메트릭 확인
- TargetResponseTime
- UnHealthyHostCount
- HealthyHostCount
- RequestCount
- HTTPCode_Target_2XX_Count

### 로그 수집
```bash
# ALB 액세스 로그 활성화
aws elbv2 modify-load-balancer-attributes \
  --load-balancer-arn <ALB_ARN> \
  --attributes Key=access_logs.s3.enabled,Value=true \
               Key=access_logs.s3.bucket,Value=<S3_BUCKET>
```

## 문제 해결

### Health Check 실패 시

1. **EC2에서 직접 확인**
   ```bash
   curl http://localhost:8080/health
   ```

2. **서버 로그 확인**
   ```bash
   docker logs cherryrecorder-server
   ```

3. **포트 리스닝 확인**
   ```bash
   sudo netstat -tlnp | grep -E '8080|33334'
   ```

4. **보안 그룹 확인**
   - ALB → EC2 통신 허용 여부
   - 포트가 올바르게 열려 있는지

### WebSocket 연결 실패 시

1. **ALB 스티키 세션 활성화**
   ```bash
   aws elbv2 modify-target-group-attributes \
     --target-group-arn <WS_TG_ARN> \
     --attributes Key=stickiness.enabled,Value=true \
                  Key=stickiness.type,Value=lb_cookie
   ```

2. **타임아웃 설정 조정**
   ```bash
   # ALB 유휴 타임아웃 증가 (기본 60초)
   aws elbv2 modify-load-balancer-attributes \
     --load-balancer-arn <ALB_ARN> \
     --attributes Key=idle_timeout.timeout_seconds,Value=3600
   ```

## 성능 최적화

1. **Connection Draining 설정**
   ```bash
   aws elbv2 modify-target-group-attributes \
     --target-group-arn <TG_ARN> \
     --attributes Key=deregistration_delay.timeout_seconds,Value=30
   ```

2. **크로스존 로드 밸런싱 활성화**
   ```bash
   aws elbv2 modify-load-balancer-attributes \
     --load-balancer-arn <ALB_ARN> \
     --attributes Key=load_balancing.cross_zone.enabled,Value=true
   ```

## 비용 최적화

1. **사용하지 않는 리스너 제거**
2. **적절한 인스턴스 타입 선택**
3. **Auto Scaling 활용**
4. **예약 인스턴스 또는 Savings Plans 고려**

## 테스트 방법

### 1. Health Check 테스트

```bash
# ALB를 통한 HTTPS 테스트
curl https://cherryrecorder.kugora.ng:58080/health

# 예상 응답: OK
```

### 2. API 엔드포인트 테스트

```bash
# 주변 장소 검색 API
curl -X POST https://cherryrecorder.kugora.ng:58080/places/nearby \
  -H "Content-Type: application/json" \
  -d '{
    "latitude": 37.5665,
    "longitude": 126.9780,
    "radius": 1000,
    "type": "restaurant"
  }'
```

### 3. WebSocket 연결 테스트

```javascript
// 브라우저 콘솔에서 테스트
const ws = new WebSocket('wss://cherryrecorder.kugora.ng:33335');
ws.onopen = () => console.log('Connected');
ws.onmessage = (e) => console.log('Message:', e.data);
ws.onerror = (e) => console.error('Error:', e);
```

### 4. 부하 테스트

```bash
# Apache Bench를 사용한 간단한 부하 테스트
ab -n 1000 -c 10 https://cherryrecorder.kugora.ng:58080/health
```

## 요약

### ✅ 올바른 설정
- ALB: SSL 종료 처리 (58080 HTTPS, 33335 WSS)
- EC2: 평문 포트만 오픈 (8080 HTTP, 33334 WS)
- Health Check: HTTP 프로토콜 사용
- 보안 그룹: ALB↔EC2 간 통신만 허용

### ❌ 피해야 할 설정
- EC2에 SSL 인증서 설치
- HTTPS로 health check
- 모든 포트를 인터넷에 노출
- 불필요한 복잡성 추가