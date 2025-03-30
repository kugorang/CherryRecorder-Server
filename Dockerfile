# ======================================================================
# Dockerfile for cherryrecorder-server (Optimized Multi-stage Build)
# ======================================================================

# ----------------------------------------------------------------------
# 스테이지 1: Builder - 애플리케이션 빌드 환경
# ----------------------------------------------------------------------
# 기본 이미지: 특정 버전 태그 사용 (LTS 권장)
FROM ubuntu:24.04 AS builder

# 환경 변수: apt 설치 시 사용자 입력 방지
ENV DEBIAN_FRONTEND=noninteractive

# 빌드 도구 및 런타임 의존성 설치 (RUN 명령어 결합으로 레이어 최소화)
# - build-essential, cmake, git, pkg-config: C++ 및 CMake 빌드 필수 도구
# - curl, zip, unzip, tar: vcpkg 실행에 필요할 수 있음
# - libmysqlclient-dev: MySQL C API 라이브러리 (mysql-connector-cpp 빌드/링크 시 필요)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    libmysqlclient-dev \
    ca-certificates \
    # 더 이상 필요 없는 패키지 캐시 정리
    && rm -rf /var/lib/apt/lists/*

# vcpkg 설치 디렉토리 설정
WORKDIR /opt

# vcpkg 클론 및 부트스트랩 (Linux 용)
RUN git clone https://github.com/microsoft/vcpkg.git && \
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# 애플리케이션 빌드 작업 디렉토리 생성 및 이동
WORKDIR /app

# 빌드 캐시 최적화: 의존성 파일 먼저 복사
COPY vcpkg.json CMakeLists.txt ./

# vcpkg 의존성 설치 단계 (선택적이지만 캐싱에 유리할 수 있음)
# CMake가 자동으로 처리하게 두거나, 여기서 명시적으로 실행 가능
# RUN /opt/vcpkg/vcpkg install --triplet x64-linux

# 나머지 소스 코드 복사 (의존성 파일 이후에 복사하여 캐시 활용 극대화)
COPY src ./src
COPY include ./include
# COPY tests ./tests # 최종 이미지 불필요 시 제외 가능

# CMake 빌드 실행 (Release 모드)
# - DBUILD_TESTING=OFF: 테스트 관련 빌드 제외 (최종 이미지 크기 감소)
# - j $(nproc): 병렬 빌드로 속도 향상
RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-linux \
    -DBUILD_TESTING=OFF \
    && cmake --build build --config Release -j $(nproc)

# ----------------------------------------------------------------------
# 스테이지 2: Final - 애플리케이션 런타임 환경
# ----------------------------------------------------------------------
# 최종 런타임 이미지: Builder와 동일한 OS/버전 사용 권장
FROM ubuntu:24.04

# 환경 변수
ENV DEBIAN_FRONTEND=noninteractive

# 필요한 최소한의 런타임 라이브러리 설치
# - libmysqlclient21: MySQL C 클라이언트 런타임 (Debian 12 기준, 버전 확인 필요)
# - ca-certificates: SSL/TLS 통신에 필요
# - netcat-openbsd: HEALTHCHECK 용 nc 명령어
RUN apt-get update && apt-get install -y --no-install-recommends \
    libmariadb3 \
    ca-certificates \
    netcat-openbsd \
    # 필요한 다른 런타임 라이브러리 (예: libboost_system, libboost_thread 등, vcpkg 결과물 복사 방식도 고려)
    # 참고: 만약 builder 스테이지에서 모든 라이브러리를 정적 링크했다면 이 단계 불필요
    && rm -rf /var/lib/apt/lists/*

# (선택 사항) vcpkg로 빌드된 공유 라이브러리(.so) 복사 (정적 링크 안 했을 경우)
# COPY --from=builder /opt/vcpkg_installed/x64-linux/lib/*.so* /usr/local/lib/
# RUN ldconfig # 공유 라이브러리 경로 갱신

# 보안 강화: 애플리케이션 실행을 위한 비-루트 사용자 생성
RUN useradd --system --create-home appuser
USER appuser

# 애플리케이션 작업 디렉토리 설정
WORKDIR /home/appuser/app

# Builder 스테이지에서 빌드된 실행 파일만 복사
# <path_to_executable>/<your_executable_name> 부분은 실제 경로/파일명으로 수정!
COPY --from=builder /app/build/CherryRecorder-Server-App ./

# 애플리케이션 포트 설정 (기본값 지정, 실행 시 환경 변수로 덮어쓰기 가능)
ENV SERVER_PORT=${SERVER_PORT}
EXPOSE ${SERVER_PORT}

# 컨테이너 상태 확인 설정 (선택 사항)
# nc 명령어로 지정된 포트가 열려 있는지 5초 간격으로 확인 (3번 실패 시 unhealthy)
HEALTHCHECK --interval=5s --timeout=3s --start-period=10s --retries=3 \
  CMD nc -z 127.0.0.1 ${SERVER_PORT} || exit 1

# 메타데이터 라벨 추가
LABEL maintainer="Kim Hyeonwoo <ialskdji@gmail.com>" \
      description="CherryRecorder Server Application"

# 컨테이너 시작 시 실행될 명령어 (환경 변수 SERVER_PORT 사용)
CMD ["./CherryRecorder-Server-App"]