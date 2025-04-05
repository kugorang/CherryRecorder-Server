# ======================================================================
# Dockerfile for CherryRecorder Server (Optimized Multi-stage Build)
# - Includes TCP Echo (Boost.Asio) & HTTP Health Check (Boost.Beast)
# - DB Dependencies Removed
# 최종 수정: 2025-04-05
# ======================================================================

# ----------------------------------------------------------------------
# 스테이지 1: "builder" - 애플리케이션 빌드 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

LABEL stage="builder"

# 환경 변수: apt 비대화형 설정
ENV DEBIAN_FRONTEND=noninteractive

# 빌드 필수 도구 및 vcpkg 의존성 빌드용 시스템 라이브러리 설치
# - DB 관련 (-dev) 패키지 제거됨
# - libssl-dev: Boost 등 다른 라이브러리에서 여전히 필요할 수 있음
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    ninja-build \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# vcpkg 설치 및 부트스트랩
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git && \
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# 애플리케이션 소스 코드 복사 및 빌드 준비
WORKDIR /app

# 중요: 이 Dockerfile을 빌드하기 전에 로컬 vcpkg.json 파일에서
# "mysql-connector-cpp" 의존성을 미리 제거해야 한다.
COPY vcpkg.json CMakeLists.txt ./

# 나머지 소스 코드 복사
COPY src ./src
COPY include ./include
# 테스트 코드는 최종 이미지에 포함되지 않음 (BUILD_TESTING=OFF)

# CMake 설정 및 빌드 실행 (Release 모드, 테스트 제외)
# vcpkg가 vcpkg.json을 읽어 필요한 의존성(Boost 등)만 설치/빌드함
RUN cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-linux \
      -DBUILD_TESTING=OFF \
    && cmake --build build -j $(nproc)

# (필수 확인 단계) 빌드된 실행 파일의 동적 라이브러리 의존성 확인
# 이 명령의 출력을 보고 아래 Final 스테이지의 COPY 명령어 목록을 확정해야 한다.
RUN echo "--- Required Shared Libraries (Check paths starting with /app/build/vcpkg_installed/...) ---" && \
    ldd /app/build/CherryRecorder-Server-App && \
    echo "--- End of Shared Library List ---"

# ----------------------------------------------------------------------
# 스테이지 2: "Final" - 애플리케이션 실행 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04

LABEL stage="final"

# 환경 변수: apt 비대화형 설정
ENV DEBIAN_FRONTEND=noninteractive

# 필수 런타임 시스템 라이브러리 설치
# - ca-certificates: SSL/TLS 통신 기본
# - curl: Health Check 용도
# - libssl3: OpenSSL 런타임 (Builder의 libssl-dev와 버전 호환성 확인 필요, Ubuntu 24.04는 libssl3 사용)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

# vcpkg로 빌드된 필수 공유 라이브러리(.so) 복사
# !! 중요 !!: 위 Builder 스테이지의 'ldd' 출력 결과를 보고 필요한 파일을 정확히 복사해야 한다.
#            아래 목록은 일반적인 Boost.Beast/Asio 사용 시 필요한 예시이다.
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_system.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_asio.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_beast.so* /usr/local/lib/
# COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_thread.so* /usr/local/lib/ # 스레드 사용 시 필요할 수 있음
# COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_context.so* /usr/local/lib/ # Asio/Beast 내부 의존성일 수 있음
# COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_date_time.so* /usr/local/lib/ # 필요 시
# ... (ldd 결과에 따라 필요한 다른 vcpkg .so 파일들) ...

# 복사된 공유 라이브러리 캐시 업데이트
RUN ldconfig

# 보안 강화를 위한 비-루트 사용자 생성
RUN useradd --system --create-home --shell /bin/bash appuser

# 애플리케이션 작업 디렉토리 설정
WORKDIR /home/appuser/app

# Builder 스테이지에서 빌드된 최종 실행 파일 복사 및 권한 설정
COPY --from=builder --chown=appuser:appuser /app/build/CherryRecorder-Server-App ./CherryRecorder-Server-App
# RUN chmod +x ./CherryRecorder-Server-App # COPY --chown 시 불필요할 수 있으나 명시적으로 추가 가능

# 비-루트 사용자로 전환
USER appuser

# --- 애플리케이션 포트 설정 ---
# Health Check 서버 포트
ARG HEALTH_CHECK_PORT=8080
ENV HEALTH_CHECK_PORT=${HEALTH_CHECK_PORT}
EXPOSE ${HEALTH_CHECK_PORT}

# TCP Echo 서버 포트
ARG ECHO_SERVER_PORT=33333
ENV ECHO_SERVER_PORT=${ECHO_SERVER_PORT}
EXPOSE ${ECHO_SERVER_PORT}

# --- 컨테이너 상태 확인 (Health Check) 설정 ---
# curl을 사용하여 HTTP Health Check 포트의 /health 경로 응답 확인
# ${VAR:-default}: VAR 변수가 없으면 default 값 사용
HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
  CMD curl --fail http://127.0.0.1:${HEALTH_CHECK_PORT:-8080}/health || exit 1

# 이미지 메타데이터 라벨
LABEL maintainer="Kim Hyeonwoo <ialskdji@gmail.com>" \
      description="CherryRecorder Server Application - TCP Echo & HTTP Health Check" \
      version="0.1.1"

# 컨테이너 시작 시 실행될 기본 명령어
CMD ["./CherryRecorder-Server-App"]