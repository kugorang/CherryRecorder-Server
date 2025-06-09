# ALB + ACM을 사용한 HTTPS 설정 (가장 간단한 방법)

## 장점
- AWS Certificate Manager(ACM)의 무료 인증서 사용
- 자동 갱신, 관리 불필요
- 앱에서 인증서 신뢰 문제 없음
- 서버 코드 변경 최소화

## 1. ACM에서 인증서 요청

```bash
# AWS CLI 사용
aws acm request-certificate \
  --domain-name cherryrecorder.kugora.ng \
  --validation-method DNS \
  --region ap-northeast-2
```

또는 AWS 콘솔:
1. Certificate Manager → 인증서 요청
2. 퍼블릭 인증서 요청
3. 도메인 이름: cherryrecorder.kugora.ng
4. DNS 검증 선택
5. Route53에서 자동으로 CNAME 레코드 생성

## 2. ALB 생성

```bash
# Target Group 생성 (HTTP)
aws elbv2 create-target-group \
  --name cherryrecorder-tg \
  --protocol HTTP \
  --port 8080 \
  --vpc-id vpc-xxxxx \
  --health-check-path /health

# ALB 생성
aws elbv2 create-load-balancer \
  --name cherryrecorder-alb \
  --subnets subnet-xxxxx subnet-yyyyy \
  --security-groups sg-xxxxx \
  --scheme internet-facing \
  --type application

# HTTPS 리스너 추가
aws elbv2 create-listener \
  --load-balancer-arn arn:aws:elasticloadbalancing:... \
  --protocol HTTPS \
  --port 443 \
  --certificates CertificateArn=arn:aws:acm:... \
  --default-actions Type=forward,TargetGroupArn=arn:aws:elasticloadbalancing:...
```

## 3. WebSocket 지원 설정

ALB는 WebSocket을 지원하지만 설정이 필요합니다:

```bash
# Target Group 속성 수정
aws elbv2 modify-target-group-attributes \
  --target-group-arn arn:aws:elasticloadbalancing:... \
  --attributes Key=stickiness.enabled,Value=true \
               Key=stickiness.type,Value=lb_cookie
```

## 4. 서버 설정 간소화

### server-config.env
```bash
# ALB가 SSL을 처리하므로 서버는 HTTP만
USE_SSL=false
HTTP_PORT=8080
WS_PORT=33334
```

### ECS 태스크 정의
```json
{
  "portMappings": [
    {
      "containerPort": 8080,
      "protocol": "tcp"
    },
    {
      "containerPort": 33334,
      "protocol": "tcp"
    }
  ]
}
```

## 5. 클라이언트 설정

### launch.json (프로덕션)
```json
{
  "args": [
    "--dart-define=API_BASE_URL=https://cherryrecorder.kugora.ng",
    "--dart-define=CHAT_SERVER_IP=cherryrecorder.kugora.ng",
    "--dart-define=CHAT_SERVER_PORT=443",
    "--dart-define=USE_WSS=true"
  ]
}
```

## 6. 보안 그룹 설정

### ALB 보안 그룹
- 인바운드: 443 (HTTPS) from 0.0.0.0/0
- 아웃바운드: 8080, 33334 to ECS 보안 그룹

### ECS 보안 그룹  
- 인바운드: 8080, 33334 from ALB 보안 그룹
- 아웃바운드: 443 (Google API), 53 (DNS)

## 비용
- ALB: 월 $20-25 정도
- ACM 인증서: 무료
- 데이터 전송: GB당 요금

## NLB vs ALB 비교

| 항목 | NLB | ALB |
|------|-----|-----|
| Layer | 4 (TCP/UDP) | 7 (HTTP/HTTPS) |
| SSL 종료 | 가능하지만 복잡 | 간단함 (ACM 통합) |
| WebSocket | 패스스루만 | 네이티브 지원 |
| 비용 | 저렴 | 약간 비쌈 |
| 설정 복잡도 | 높음 | 낮음 | 