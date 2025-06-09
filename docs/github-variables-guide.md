# GitHub Variables 설정 가이드

## 포트 번호 변수 구분

### 컨테이너 내부 포트 (ECS Task Definition용)
- `HTTP_PORT_VALUE`: 8080 (컨테이너 내부 HTTP 서버)
- `WS_PORT_VALUE`: 33334 (컨테이너 내부 WebSocket 서버)
- `HTTPS_PORT_VALUE`: 58080 (사용하지 않음 - NLB가 TLS 처리)
- `WSS_PORT_VALUE`: 33335 (사용하지 않음 - NLB가 TLS 처리)

### NLB 리스너 포트 (외부 접속용)
- `NLB_HTTPS_PORT`: 58080 (NLB TLS 리스너 → 8080)
- `NLB_WSS_PORT`: 33335 (NLB TLS 리스너 → 33334)

## GitHub 저장소 설정 방법

1. 저장소 → Settings → Secrets and variables → Actions
2. Variables 탭 선택
3. 다음 변수들 추가:

```
# 서버 주소
SERVER_ADDRESS=cherryrecorder.kugora.ng

# 컨테이너 포트 (ECS용)
HTTP_PORT_VALUE=8080
WS_PORT_VALUE=33334
HEALTH_CHECK_PORT_VALUE=8080

# NLB 포트 (연결 확인용)
NLB_HTTPS_PORT=58080
NLB_WSS_PORT=33335

# AWS 설정
AWS_REGION=ap-northeast-2
AWS_ECR_REPOSITORY=cherryrecorder-server
ECS_CLUSTER_NAME=cherryrecorder-cluster
ECS_SERVICE_NAME=cherryrecorder-service
ECS_TASK_DEF_FAMILY=cherryrecorder
CONTAINER_NAME=cherryrecorder-server
```

## 워크플로우 수정 (권장)

`main-ci-cd.yml`의 연결 확인 부분:

```yaml
call_connection_check:
  name: 8. Connection Check to Deployed Server
  if: needs.call_deploy_aws.result == 'success'
  uses: ./.github/workflows/reusable-connection-check.yml
  needs: call_deploy_aws
  with:
    server_address: ${{ vars.SERVER_ADDRESS }}
    health_check_port: ${{ vars.HEALTH_CHECK_PORT_VALUE }}
    # NLB 리스너 포트 사용
    http_port: ${{ vars.NLB_HTTPS_PORT }}
    websocket_port: ${{ vars.NLB_WSS_PORT }}
``` 