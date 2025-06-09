# ECS + ALB 문제 해결 가이드

## 1단계: ECS 서비스 상태 확인

### 1.1 AWS 콘솔에서 확인
1. **ECS 콘솔** → 클러스터 선택 → 서비스 탭
2. `cherryrecorder-server` 서비스 클릭
3. 확인사항:
   - **Desired tasks**: 원하는 태스크 수
   - **Running tasks**: 실행 중인 태스크 수
   - **Pending tasks**: 대기 중인 태스크 수

### 1.2 CLI로 확인
```bash
# 서비스 상태 확인
aws ecs describe-services \
  --cluster <CLUSTER_NAME> \
  --services cherryrecorder-server \
  --query 'services[0].{desired:desiredCount,running:runningCount,pending:pendingCount}'

# 태스크 목록 확인
aws ecs list-tasks \
  --cluster <CLUSTER_NAME> \
  --service-name cherryrecorder-server
```

## 2단계: 태스크 상태 및 로그 확인

### 2.1 실행 중인 태스크 상세 정보
```bash
# 태스크 상세 정보
aws ecs describe-tasks \
  --cluster <CLUSTER_NAME> \
  --tasks <TASK_ARN> \
  --query 'tasks[0].{status:lastStatus,reason:stoppedReason,health:healthStatus}'
```

### 2.2 CloudWatch 로그 확인
```bash
# 로그 그룹 이름 확인 (보통 /ecs/<task-definition-name>)
aws logs tail /ecs/cherryrecorder-server --follow

# 최근 에러 로그만 필터링
aws logs filter-log-events \
  --log-group-name /ecs/cherryrecorder-server \
  --filter-pattern "ERROR" \
  --start-time $(date -u -d '1 hour ago' +%s)000
```

### 2.3 ECS Exec으로 컨테이너 접속 (활성화된 경우)
```bash
# 실행 중인 컨테이너에 접속
aws ecs execute-command \
  --cluster <CLUSTER_NAME> \
  --task <TASK_ID> \
  --container cherryrecorder-server \
  --interactive \
  --command "/bin/bash"

# 컨테이너 내부에서 확인
curl http://localhost:8080/health
netstat -tlnp | grep -E '8080|33334'
```

## 3단계: 태스크 정의 확인

### 3.1 태스크 정의 설정 확인
```bash
# 최신 태스크 정의 확인
aws ecs describe-task-definition \
  --task-definition cherryrecorder-server \
  --query 'taskDefinition.{family:family,revision:revision,cpu:cpu,memory:memory}'

# 컨테이너 정의 확인
aws ecs describe-task-definition \
  --task-definition cherryrecorder-server \
  --query 'taskDefinition.containerDefinitions[0].{name:name,image:image,ports:portMappings,env:environment}'
```

### 3.2 필수 환경 변수 확인
```json
// 태스크 정의에 있어야 할 환경 변수
{
  "environment": [
    {"name": "HTTP_PORT", "value": "8080"},
    {"name": "WS_PORT", "value": "33334"},
    {"name": "GOOGLE_MAPS_API_KEY", "value": "<YOUR_KEY>"}
  ]
}
```

## 4단계: ECS 서비스와 ALB 연결 확인

### 4.1 서비스의 로드 밸런서 설정 확인
```bash
aws ecs describe-services \
  --cluster <CLUSTER_NAME> \
  --services cherryrecorder-server \
  --query 'services[0].loadBalancers'
```

### 4.2 대상 그룹 헬스 확인
```bash
# 대상 그룹의 대상 상태 확인
aws elbv2 describe-target-health \
  --target-group-arn <TARGET_GROUP_ARN>

# ECS 태스크가 대상으로 등록되어 있는지 확인
# Target.Id는 <EC2_INSTANCE_ID>:<CONTAINER_PORT> 형식이거나
# ECS Fargate의 경우 ENI IP 주소
```

## 5단계: 일반적인 ECS 문제 해결

### 문제 1: 태스크가 계속 재시작됨
```bash
# 태스크 이벤트 확인
aws ecs describe-services \
  --cluster <CLUSTER_NAME> \
  --services cherryrecorder-server \
  --query 'services[0].events[0:5]'

# 일반적인 원인:
# - 헬스 체크 실패
# - 메모리 부족
# - 포트 충돌
```

### 문제 2: "service cherryrecorder-server was unable to place a task"
```bash
# 클러스터 리소스 확인
aws ecs describe-clusters \
  --clusters <CLUSTER_NAME> \
  --query 'clusters[0].{cpu:registeredContainerInstancesCount,memory:runningTasksCount}'

# 해결책:
# - 클러스터에 EC2 인스턴스 추가
# - 태스크 CPU/메모리 요구사항 줄이기
```

### 문제 3: 헬스 체크 실패
```yaml
# 태스크 정의에서 헬스 체크 설정
"healthCheck": {
  "command": ["CMD-SHELL", "curl -f http://localhost:8080/health || exit 1"],
  "interval": 30,
  "timeout": 5,
  "retries": 3,
  "startPeriod": 60
}
```

## 6단계: ECS 특화 모니터링

### 6.1 Container Insights 활성화
```bash
# 클러스터에 Container Insights 활성화
aws ecs update-cluster-settings \
  --cluster <CLUSTER_NAME> \
  --settings name=containerInsights,value=enabled
```

### 6.2 주요 CloudWatch 메트릭
- `CPUUtilization`: CPU 사용률
- `MemoryUtilization`: 메모리 사용률
- `RunningTasksCount`: 실행 중인 태스크 수
- `HealthyHostCount`: ALB에서 건강한 대상 수

## 7단계: 빠른 디버깅 스크립트

```bash
#!/bin/bash
CLUSTER="your-cluster-name"
SERVICE="cherryrecorder-server"

echo "=== ECS Service Status ==="
aws ecs describe-services --cluster $CLUSTER --services $SERVICE \
  --query 'services[0].{Status:status,Desired:desiredCount,Running:runningCount,Pending:pendingCount}' \
  --output table

echo -e "\n=== Recent Service Events ==="
aws ecs describe-services --cluster $CLUSTER --services $SERVICE \
  --query 'services[0].events[0:3].{Time:createdAt,Message:message}' \
  --output table

echo -e "\n=== Running Tasks ==="
TASKS=$(aws ecs list-tasks --cluster $CLUSTER --service $SERVICE --query 'taskArns' --output text)
if [ ! -z "$TASKS" ]; then
    aws ecs describe-tasks --cluster $CLUSTER --tasks $TASKS \
      --query 'tasks[].{TaskId:taskArn,Status:lastStatus,Health:healthStatus}' \
      --output table
fi

echo -e "\n=== Target Group Health ==="
TG_ARN=$(aws ecs describe-services --cluster $CLUSTER --services $SERVICE \
  --query 'services[0].loadBalancers[0].targetGroupArn' --output text)
if [ ! -z "$TG_ARN" ]; then
    aws elbv2 describe-target-health --target-group-arn $TG_ARN \
      --query 'TargetHealthDescriptions[].{Target:Target.Id,Health:TargetHealth.State,Description:TargetHealth.Description}' \
      --output table
fi

echo -e "\n=== Recent Logs ==="
aws logs tail /ecs/$SERVICE --since 5m
```

## 8단계: ECS + ALB 체크리스트

- [ ] ECS 서비스가 ACTIVE 상태인가?
- [ ] 원하는 태스크 수와 실행 중인 태스크 수가 일치하는가?
- [ ] CloudWatch 로그에 에러가 없는가?
- [ ] 태스크 정의에 올바른 포트 매핑이 있는가?
- [ ] 태스크 정의에 필요한 환경 변수가 모두 있는가?
- [ ] ALB 대상 그룹에 ECS 태스크가 healthy로 등록되어 있는가?
- [ ] 태스크에 충분한 CPU/메모리가 할당되어 있는가?
- [ ] 보안 그룹이 올바르게 설정되어 있는가?

## Fargate vs EC2 타입별 확인사항

### EC2 타입
- Container Instance가 클러스터에 등록되어 있는지 확인
- EC2 인스턴스에 ECS Agent가 실행 중인지 확인
- 인스턴스에 충분한 리소스가 있는지 확인

### Fargate 타입
- 태스크 정의가 Fargate 호환인지 확인
- 네트워크 모드가 awsvpc인지 확인
- 서브넷에 충분한 IP가 있는지 확인