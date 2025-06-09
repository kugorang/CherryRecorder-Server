# ALB WebSocket 설정 가이드

## 문제: WebSocket 연결 실패

### 1. ALB 리스너 설정 확인

```bash
# ALB 리스너 목록 확인
aws elbv2 describe-listeners \
  --load-balancer-arn <ALB_ARN> \
  --query 'Listeners[].{Port:Port,Protocol:Protocol}'
```

## 2. WebSocket을 위한 ALB 설정

### 옵션 1: HTTPS 리스너에서 WebSocket 처리 (권장)

ALB의 HTTPS 리스너는 WebSocket 업그레이드를 자동으로 처리합니다.

```bash
# 1. 단일 HTTPS 리스너 생성 (58080)
aws elbv2 create-listener \
  --load-balancer-arn <ALB_ARN> \
  --protocol HTTPS \
  --port 58080 \
  --certificates CertificateArn=<ACM_CERT_ARN> \
  --default-actions Type=forward,TargetGroupArn=<HTTP_TARGET_GROUP_ARN>

# 2. 경로 기반 라우팅 규칙 추가
# /ws/* 경로는 WebSocket 대상 그룹으로
aws elbv2 create-rule \
  --listener-arn <LISTENER_ARN> \
  --priority 1 \
  --conditions Field=path-pattern,Values="/ws/*" \
  --actions Type=forward,TargetGroupArn=<WS_TARGET_GROUP_ARN>
```

### 옵션 2: 별도 포트 사용 (현재 설정)

```bash
# HTTPS 리스너 (58080)
aws elbv2 create-listener \
  --load-balancer-arn <ALB_ARN> \
  --protocol HTTPS \
  --port 58080 \
  --certificates CertificateArn=<ACM_CERT_ARN> \
  --default-actions Type=forward,TargetGroupArn=<HTTP_TARGET_GROUP_ARN>

# WebSocket용 HTTPS 리스너 (33335) - TLS가 아닌 HTTPS 사용
aws elbv2 create-listener \
  --load-balancer-arn <ALB_ARN> \
  --protocol HTTPS \
  --port 33335 \
  --certificates CertificateArn=<ACM_CERT_ARN> \
  --default-actions Type=forward,TargetGroupArn=<WS_TARGET_GROUP_ARN>
```

## 3. 대상 그룹 설정

### HTTP/WebSocket 통합 대상 그룹
```bash
aws elbv2 create-target-group \
  --name cherryrecorder-http-ws-tg \
  --protocol HTTP \
  --port 8080 \
  --vpc-id <VPC_ID> \
  --target-type ip \  # ECS Fargate인 경우
  --health-check-protocol HTTP \
  --health-check-path /health \
  --health-check-interval-seconds 30 \
  --health-check-timeout-seconds 10 \
  --healthy-threshold-count 2 \
  --unhealthy-threshold-count 3
```

### 대상 그룹 속성 설정 (중요!)
```bash
# Sticky Sessions 활성화 (WebSocket 필수)
aws elbv2 modify-target-group-attributes \
  --target-group-arn <TARGET_GROUP_ARN> \
  --attributes \
    Key=stickiness.enabled,Value=true \
    Key=stickiness.type,Value=lb_cookie \
    Key=stickiness.lb_cookie.duration_seconds,Value=86400

# Deregistration delay 줄이기
aws elbv2 modify-target-group-attributes \
  --target-group-arn <TARGET_GROUP_ARN> \
  --attributes Key=deregistration_delay.timeout_seconds,Value=30
```

## 4. ALB 속성 설정

```bash
# WebSocket을 위한 타임아웃 증가
aws elbv2 modify-load-balancer-attributes \
  --load-balancer-arn <ALB_ARN> \
  --attributes \
    Key=idle_timeout.timeout_seconds,Value=3600 \
    Key=routing.http2.enabled,Value=true
```

## 5. 보안 그룹 확인

### ALB 보안 그룹 인바운드 규칙
```
- 58080/tcp from 0.0.0.0/0 (HTTPS + WebSocket)
- 33335/tcp from 0.0.0.0/0 (WSS) # 별도 포트 사용 시
```

### ECS 태스크 보안 그룹 인바운드 규칙
```
- 8080/tcp from ALB 보안 그룹
- 33334/tcp from ALB 보안 그룹 # 별도 포트 사용 시
```

## 6. 클라이언트 코드 수정 제안

### 현재 (문제 있음)
```javascript
const ws = new WebSocket('wss://cherryrecorder.kugora.ng:33335/');
```

### 수정안 1: 단일 포트 사용 (권장)
```javascript
const ws = new WebSocket('wss://cherryrecorder.kugora.ng:58080/ws');
```

### 수정안 2: 별도 포트 유지
```javascript
// ALB에서 33335 포트가 HTTPS 리스너로 설정되어 있는지 확인
const ws = new WebSocket('wss://cherryrecorder.kugora.ng:33335/');
```

## 7. 서버 코드 수정 제안

### HTTP와 WebSocket을 같은 포트에서 처리
```cpp
// main.cpp 수정
// HTTP 서버에서 /ws 경로로 WebSocket 업그레이드 처리
if (req_.target().starts_with("/ws") && 
    req_[http::field::upgrade] == "websocket") {
    // WebSocket 핸드셰이크 처리
    handle_websocket_upgrade();
}
```

## 8. 테스트 방법

### Health Check 테스트
```bash
# HTTPS 엔드포인트 테스트
curl -v https://cherryrecorder.kugora.ng:58080/health
```

### WebSocket 테스트
```javascript
// 브라우저 콘솔
const ws = new WebSocket('wss://cherryrecorder.kugora.ng:58080/ws');
ws.onopen = () => console.log('WebSocket 연결 성공!');
ws.onerror = (e) => console.error('WebSocket 에러:', e);
ws.onclose = (e) => console.log('WebSocket 종료:', e.code, e.reason);
```

## 9. 문제 해결 체크리스트

- [ ] ALB에 HTTPS 리스너가 생성되어 있는가? (58080, 33335)
- [ ] ACM 인증서가 유효하고 도메인이 일치하는가?
- [ ] 대상 그룹에 Sticky Session이 활성화되어 있는가?
- [ ] ALB idle timeout이 충분히 긴가? (3600초 이상)
- [ ] 보안 그룹이 올바르게 설정되어 있는가?
- [ ] Route 53에서 도메인이 ALB를 가리키고 있는가?
- [ ] ECS 태스크가 healthy 상태인가?