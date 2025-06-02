# ======================================================================
# Dockerfile for CherryRecorder Server (Optimized Multi-stage Build)
# ======================================================================

# ----------------------------------------------------------------------
# 스테이지 1: "builder" - 애플리케이션 빌드 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

LABEL stage="builder"

ENV DEBIAN_FRONTEND=noninteractive

# --- STEP 1: 빌드 도구 설치 ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    ninja-build \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# --- STEP 1.5: 최신 CMake 설치 ---
ARG CMAKE_VERSION=3.30.1
RUN wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh \
    -q -O /tmp/cmake-install.sh \
    && chmod +x /tmp/cmake-install.sh \
    && /tmp/cmake-install.sh --skip-license --prefix=/usr/local \
    && rm /tmp/cmake-install.sh

ENV PATH=/usr/local/bin:$PATH

# --- STEP 2: vcpkg 설치 (특정 버전 고정) ---
ARG VCPKG_COMMIT_HASH=b02e341c927f16d991edbd915d8ea43eac52096c
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git && \
    cd vcpkg && \
    git checkout ${VCPKG_COMMIT_HASH} && \
    cd .. && \
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# --- STEP 3: 애플리케이션 코드 및 설정 파일 복사 ---
WORKDIR /app
COPY vcpkg.json ./
COPY CMakeLists.txt ./
COPY src ./src
COPY include ./include

# --- STEP 4a: 애플리케이션 빌드 (CMake Configure) ---
RUN cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-linux \
      -DBUILD_TESTING=OFF \
      -DCMAKE_VERBOSE_MAKEFILE=ON

# --- STEP 4b: 애플리케이션 빌드 (CMake Build) ---
RUN cmake --build build -j $(nproc)

# --- STEP 5: 빌드된 실행 파일 의존성 확인 ---
RUN echo "--- Required Shared Libraries ---" && \
    ldd /app/build/CherryRecorder-Server-App || echo "ldd failed, executable might not exist yet" && \
    echo "--- End of Shared Library List ---"

# ----------------------------------------------------------------------
# 스테이지 2: "final" - 애플리케이션 실행 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS final

LABEL maintainer="Kim Hyeonwoo <ialskdji@gmail.com>" \
      description="CherryRecorder Server Application - HTTP & WebSocket Services" \
      version="0.1.1"

ENV DEBIAN_FRONTEND=noninteractive

# 런타임 의존성 설치
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Builder 스테이지에서 빌드된 vcpkg 라이브러리 복사
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_system.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_asio.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_beast.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_json.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libssl.so* /usr/local/lib/
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libcrypto.so* /usr/local/lib/

# 라이브러리 캐시 업데이트
RUN ldconfig

# 사용자 생성 및 디렉토리 설정
RUN useradd --system --create-home --shell /bin/bash appuser
WORKDIR /home/appuser/app

# 최종 실행 파일 복사
COPY --from=builder --chown=appuser:appuser /app/build/CherryRecorder-Server-App ./CherryRecorder-Server-App

# 애플리케이션이 사용할 디렉토리 생성 및 권한 설정
RUN mkdir -p history && chown appuser:appuser history

# 사용자 전환
USER appuser

# 기본 포트 노출 (문서화 목적, 실행 시 -p 옵션으로 오버라이드 가능)
EXPOSE 8080 33334 33333

# Health Check (기본 포트 사용, 실제 값은 환경 변수에서 가져옴)
HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
  CMD curl --fail http://127.0.0.1:${HTTP_PORT:-8080}/health || exit 1

# --- 컨테이너 실행 가이드 ---
# 실행 시 필요한 환경 변수는 docker run 명령어의 --env-file 옵션(.env 파일)을 통해 주입
# 예시: docker run -p 8080:8080 -p 33334:33334 -p 33333:33333 --env-file .env <이미지_이름>

CMD ["./CherryRecorder-Server-App"]