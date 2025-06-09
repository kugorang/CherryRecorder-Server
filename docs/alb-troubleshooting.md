# ALB 문제 해결 가이드

## 1단계: EC2 인스턴스 확인

### 1.1 인스턴스 상태 확인
```bash
# AWS 콘솔에서 확인하거나 CLI 사용
aws ec2 describe-instances --instance-ids <INSTANCE_ID> \
  --query 'Reservations[0].Instances[0].State.Name'
# 결과가 "running"이어야 함
```

### 1.2 EC2에 SSH 접속
```bash
ssh -i <your-key.pem> ec2-user@<EC2_PRIVATE_IP>
```

### 1.3 Docker 컨테이너 상태 확인
```bash
# Docker 서비스 상태
sudo systemctl status docker

# 실행 중인 컨테이너 확인
docker ps

# 중지된 컨테이너 포함해서 확인
docker ps -a

# 예상 결과: cherryrecorder-server 컨테이너가 Up 상태여야 함
```

### 1.4 컨테이너 로그 확인
```bash
# 서버 로그 확인
docker logs cherryrecorder-server

# 실시간 로그 확인
docker logs -f cherryrecorder-server

# 찾아볼 내용:
# - "HTTP server started on 0.0.0.0:8080"
# - "WebSocket listener started on port 33334"
# - 에러 메시지
```

## 2단계: 네트워크 확인

### 2.1 포트 리스닝 확인
```bash
# EC2 내부에서 포트 확인
sudo netstat -tlnp | grep -E '8080|33334'
# 또는
sudo ss -tlnp | grep -E '8080|33334'

# 예상 결과:
# tcp    0.0.0.0:8080    LISTEN
# tcp    0.0.0.0:33334   LISTEN
```

### 2.2 로컬 테스트
```bash
# EC2 내부에서 직접 테스트
curl -v http://localhost:8080/health

# 컨테이너 내부에서 테스트
docker exec cherryrecorder-server curl http://localhost:8080/health

# telnet으로 포트 연결 테스트
telnet localhost 8080
```

## 3단계: ALB 대상 그룹 확인

### 3.1 대상 그룹 상태 확인
```bash
# 대상 그룹의 대상 헬스 확인
aws elbv2 describe-target-health \
  --target-group-arn <TARGET_GROUP_ARN>

# 확인할 내용:
# - State: "healthy" 또는 "unhealthy"
# - Description: 실패 이유
```

### 3.2 AWS 콘솔에서 확인
1. EC2 콘솔 → 대상 그룹
2. 해당 대상 그룹 선택
3. "대상" 탭에서 인스턴스 상태 확인
4. "상태 검사" 탭에서 설정 확인

## 4단계: 보안 그룹 확인

### 4.1 EC2 보안 그룹 인바운드 규칙
```bash
aws ec2 describe-security-groups --group-ids <EC2_SG_ID> \
  --query 'SecurityGroups[0].IpPermissions'

# 확인할 내용:
# - 8080 포트가 ALB 보안 그룹에서 허용되는지
# - 33334 포트가 ALB 보안 그룹에서 허용되는지
```

### 4.2 ALB 보안 그룹 확인
```bash
aws ec2 describe-security-groups --group-ids <ALB_SG_ID> \
  --query 'SecurityGroups[0].IpPermissions'

# 확인할 내용:
# - 58080 포트가 0.0.0.0/0에서 허용되는지
# - 33335 포트가 0.0.0.0/0에서 허용되는지
```

## 5단계: ALB 리스너 확인

### 5.1 리스너 목록 확인
```bash
aws elbv2 describe-listeners \
  --load-balancer-arn <ALB_ARN>

# 확인할 내용:
# - 58080 포트 리스너가 있는지
# - 33335 포트 리스너가 있는지
# - 올바른 대상 그룹으로 전달하는지
```

### 5.2 SSL 인증서 확인
```bash
aws acm list-certificates

# 인증서 상세 정보
aws acm describe-certificate \
  --certificate-arn <CERT_ARN>

# 도메인이 맞는지, 만료되지 않았는지 확인
```

## 6단계: DNS 확인

### 6.1 도메인 해석 확인
```bash
# 도메인이 ALB를 가리키는지 확인
nslookup cherryrecorder.kugora.ng
# 또는
dig cherryrecorder.kugora.ng

# ALB DNS 이름 확인
aws elbv2 describe-load-balancers \
  --load-balancer-arns <ALB_ARN> \
  --query 'LoadBalancers[0].DNSName'
```

## 7단계: 환경 변수 확인

### 7.1 Docker 컨테이너 환경 변수
```bash
# 컨테이너의 환경 변수 확인
docker exec cherryrecorder-server env | grep -E 'PORT|SSL'

# 예상 값:
# HTTP_PORT=8080
# WS_PORT=33334
```

## 일반적인 문제와 해결 방법

### 문제 1: "Connection refused"
```bash
# 해결: 서버가 실행 중인지 확인
docker start cherryrecorder-server

# 포트 매핑 확인
docker port cherryrecorder-server
```

### 문제 2: "502 Bad Gateway"
```bash
# 해결: 대상 그룹 health check 실패
# 1. EC2에서 서버 재시작
docker restart cherryrecorder-server

# 2. Health check 설정 완화 (임시)
# - Timeout: 5초 → 10초
# - Unhealthy threshold: 3 → 5
```

### 문제 3: "503 Service Unavailable"
```bash
# 해결: 건강한 대상이 없음
# 1. 대상 그룹에 인스턴스가 등록되어 있는지 확인
# 2. 인스턴스가 올바른 가용 영역에 있는지 확인
```

### 문제 4: SSL/TLS 관련 오류
```bash
# 해결: ACM 인증서 확인
# 1. 인증서가 검증됨 상태인지 확인
# 2. 도메인이 인증서에 포함되어 있는지 확인
# 3. 인증서가 만료되지 않았는지 확인
```

## 디버깅 체크리스트

- [ ] EC2 인스턴스가 running 상태인가?
- [ ] Docker 컨테이너가 실행 중인가?
- [ ] 서버가 8080, 33334 포트를 리스닝하고 있는가?
- [ ] EC2 내부에서 curl http://localhost:8080/health가 작동하는가?
- [ ] 대상 그룹에서 인스턴스가 healthy 상태인가?
- [ ] 보안 그룹이 올바르게 설정되어 있는가?
- [ ] ALB 리스너가 올바른 포트에서 리스닝하고 있는가?
- [ ] DNS가 ALB를 올바르게 가리키고 있는가?
- [ ] SSL 인증서가 유효한가?

## 로그 수집 명령어 모음

```bash
# 한 번에 모든 정보 수집
echo "=== Docker Status ===" && \
docker ps && \
echo -e "\n=== Container Logs (last 50 lines) ===" && \
docker logs --tail 50 cherryrecorder-server && \
echo -e "\n=== Port Listening ===" && \
sudo netstat -tlnp | grep -E '8080|33334' && \
echo -e "\n=== Health Check Test ===" && \
curl -v http://localhost:8080/health && \
echo -e "\n=== Environment Variables ===" && \
docker exec cherryrecorder-server env | grep -E 'PORT|SSL'
```

## NLB 58080 포트 설정 가이드

### 현재 문제
- WebSocket (33335) 포트는 정상 작동
- HTTPS API (58080) 포트는 작동하지 않음
- 서버는 8080 포트에서 HTTP만 처리

### 해결 방법

#### 1. 현재 NLB 리스너 확인

```bash
# NLB ARN 확인
aws elbv2 describe-load-balancers \
  --names cherryrecorder-nlb \
  --query 'LoadBalancers[0].LoadBalancerArn' \
  --output text

# 현재 리스너 목록 확인
aws elbv2 describe-listeners \
  --load-balancer-arn <NLB_ARN> \
  --query 'Listeners[].{Port:Port,Protocol:Protocol}'
```

#### 2. 58080 TLS 리스너 추가

```bash
# 58080 포트에 TLS 리스너 생성
aws elbv2 create-listener \
  --load-balancer-arn <NLB_ARN> \
  --protocol TLS \
  --port 58080 \
  --certificates CertificateArn=<ACM_CERT_ARN> \
  --default-actions Type=forward,TargetGroupArn=<HTTP_TARGET_GROUP_ARN>
```

#### 3. 대상 그룹 설정

```bash
# HTTP 대상 그룹이 없다면 생성
aws elbv2 create-target-group \
  --name cherryrecorder-http-tg \
  --protocol TCP \
  --port 8080 \
  --vpc-id <VPC_ID> \
  --target-type instance \
  --health-check-protocol HTTP \
  --health-check-path /health \
  --health-check-port 8080
```

#### 4. 보안 그룹 확인

```bash
# NLB 보안 그룹에 58080 포트 추가
aws ec2 authorize-security-group-ingress \
  --group-id <NLB_SG_ID> \
  --protocol tcp \
  --port 58080 \
  --cidr 0.0.0.0/0
```

## NLB 타임아웃 문제 해결

### Google Places API 응답 지연으로 인한 타임아웃

#### 증상
- `/health` 체크는 정상 작동
- `/map` 화면에서 주변 장소 검색 시 타임아웃 발생
- 안드로이드 프로덕션 환경에서만 발생

#### 원인
1. 서버가 Google Places API를 호출할 때 응답이 느림
2. NLB의 아이들 타임아웃 설정이 너무 짧음 (기본 350초)
3. 서버 측 Google API 호출에 타임아웃 설정 없음

#### 해결 방법

##### 1. NLB 속성 수정 (즉시 적용 가능)

```bash
# 현재 NLB 속성 확인
aws elbv2 describe-load-balancer-attributes \
  --load-balancer-arn <NLB_ARN>

# Connection idle timeout 증가 (기본 350초 → 600초)
aws elbv2 modify-load-balancer-attributes \
  --load-balancer-arn <NLB_ARN> \
  --attributes Key=idle_timeout.timeout_seconds,Value=600

# TCP keepalive 활성화 (연결 유지)
aws elbv2 modify-load-balancer-attributes \
  --load-balancer-arn <NLB_ARN> \
  --attributes \
    Key=tcp.idle_timeout.timeout_seconds,Value=600 \
    Key=tcp.keepalive.enable,Value=true \
    Key=tcp.keepalive.interval_seconds,Value=75
```

##### 2. 대상 그룹 속성 수정

```bash
# Deregistration delay 감소 (빠른 재시작 지원)
aws elbv2 modify-target-group-attributes \
  --target-group-arn <TARGET_GROUP_ARN> \
  --attributes Key=deregistration_delay.timeout_seconds,Value=30

# Connection termination 비활성화
aws elbv2 modify-target-group-attributes \
  --target-group-arn <TARGET_GROUP_ARN> \
  --attributes Key=connection_termination.enabled,Value=false
```

##### 3. 서버 측 최적화 (장기 해결책)

서버 코드에서 Google API 호출 시:
- 연결 타임아웃 설정 (10초)
- 응답 캐싱 구현
- 동시 요청 수 제한

##### 4. 클라이언트 측 개선

```dart
// 타임아웃 시간 증가 (15초 → 30초)
final response = await _apiClient.post(
  ApiConstants.nearbySearchEndpoint,
  body: { /* ... */ },
).timeout(const Duration(seconds: 30));

// 재시도 로직 추가
int retryCount = 0;
while (retryCount < 3) {
  try {
    // API 호출
    break;
  } catch (e) {
    if (e is TimeoutException && retryCount < 2) {
      retryCount++;
      await Future.delayed(Duration(seconds: 2));
      continue;
    }
    throw e;
  }
}
```