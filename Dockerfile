# ======================================================================
# Dockerfile for CherryRecorder Server (Optimized Multi-stage Build)
# - Cache Optimization Focused on vcpkg & Layering
# 최종 수정: 2025-04-07 (캐시 최적화 적용)
# ======================================================================

# ----------------------------------------------------------------------
# 스테이지 1: "builder" - 애플리케이션 빌드 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

LABEL stage="builder"

# 환경 변수: apt 비대화형 설정
ENV DEBIAN_FRONTEND=noninteractive

# --- STEP 1: 빌드 도구 설치 ---
# 가장 먼저, 가장 변경이 적은 시스템 의존성 설치
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

# --- STEP 2: vcpkg 설치 (특정 버전 고정) ---
# 특정 vcpkg 커밋을 체크아웃하여 vcpkg 자체의 변경 방지
# <사용할_vcpkg_커밋_해시> 부분은 실제 사용할 안정적인 커밋 해시로 변경해야 함
ARG VCPKG_COMMIT_HASH=b02e341c927f16d991edbd915d8ea43eac52096c
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git && \
    cd vcpkg && \
    git checkout ${VCPKG_COMMIT_HASH} && \
    cd .. && \
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# --- STEP 3: 애플리케이션 의존성 파일 복사 ---
# 소스 코드 복사 전에 의존성 정의 파일만 먼저 복사
# vcpkg.json 파일이 변경되지 않으면 이 레이어 및 다음 vcpkg 설치 단계가 캐시될 수 있음
WORKDIR /app
COPY vcpkg.json ./

# --- STEP 4: vcpkg 라이브러리 설치 ---
# vcpkg.json이 변경되었을 때만 이 단계가 실행됨
# CMake를 사용하여 의존성 설치 (실제 빌드는 하지 않음 - CMakeLists.txt 구조에 따라 조정 필요)
# 또는 간단하게 vcpkg install 명령을 직접 사용할 수도 있음 (이 경우 CMakeLists.txt 필요 없음)
# 여기서는 CMake가 vcpkg를 트리거한다고 가정. 더미 CMakeLists.txt를 사용하거나,
# vcpkg install 명령을 직접 사용하는 것이 캐싱에 더 유리할 수 있음.
# 우선, 원래 방식대로 CMake를 통해 설치되도록 두되, vcpkg.json 버전 고정이 중요함.

# --- STEP 5: CMake 설정 파일 복사 ---
COPY CMakeLists.txt ./

# --- STEP 6: 소스 코드 복사 ---
# 소스 코드는 의존성 설치 이후에 복사
COPY src ./src
COPY include ./include

# --- STEP 7: 애플리케이션 빌드 ---
# 소스 코드 또는 CMakeLists.txt가 변경되면 여기서부터 다시 실행됨
# vcpkg.json 에서 버전이 고정되었다면, 라이브러리는 다시 빌드되지 않고 캐시된 것을 사용함
RUN cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-linux \
      -DBUILD_TESTING=OFF \
    && cmake --build build -j $(nproc)

# --- STEP 8: 빌드된 실행 파일 의존성 확인 ---
# 이 단계는 레이어 캐시에 영향을 주지 않음
RUN echo "--- Required Shared Libraries (Check paths starting with /app/build/vcpkg_installed/...) ---" && \
    ldd /app/build/CherryRecorder-Server-App && \
    echo "--- End of Shared Library List ---"


# ----------------------------------------------------------------------
# 스테이지 2: "final" - 애플리케이션 실행 전용 환경 (이전과 거의 동일)
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS final

LABEL stage="final"
ENV DEBIAN_FRONTEND=noninteractive

# 런타임 의존성 설치 (변경 빈도 낮음)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libssl3 \
    # ldd 결과 확인 후 필요한 다른 시스템 라이브러리 추가
    && rm -rf /var/lib/apt/lists/*

# Builder 스테이지에서 빌드된 vcpkg 라이브러리 복사
# 이 레이어는 Builder 스테이지의 빌드 결과가 변경될 때만 변경됨
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_system.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_asio.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_beast.so* /usr/local/lib/
# ... (ldd 결과에 따라 필요한 다른 vcpkg .so 파일들) ...

# 라이브러리 캐시 업데이트
RUN ldconfig

# 사용자 생성 및 디렉토리 설정
RUN useradd --system --create-home --shell /bin/bash appuser
WORKDIR /home/appuser/app

# 최종 실행 파일 복사 (Builder 결과 변경 시 이 레이어 변경됨)
COPY --from=builder --chown=appuser:appuser /app/build/CherryRecorder-Server-App ./CherryRecorder-Server-App

# 사용자 전환
USER appuser

# 포트 설정 및 노출
ARG HEALTH_CHECK_PORT=8080
ENV HEALTH_CHECK_PORT=${HEALTH_CHECK_PORT}
EXPOSE ${HEALTH_CHECK_PORT}
ARG ECHO_SERVER_PORT=33333
ENV ECHO_SERVER_PORT=${ECHO_SERVER_PORT}
EXPOSE ${ECHO_SERVER_PORT}

# Health Check
HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
  CMD curl --fail http://127.0.0.1:${HEALTH_CHECK_PORT:-8080}/health || exit 1

# 메타데이터
LABEL maintainer="Kim Hyeonwoo <ialskdji@gmail.com>" \
      description="CherryRecorder Server Application - TCP Echo & HTTP Health Check" \
      version="0.1.1"

# 시작 명령어
CMD ["./CherryRecorder-Server-App"]