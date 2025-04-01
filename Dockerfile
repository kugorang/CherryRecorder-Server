# ======================================================================
# Dockerfile for CherryRecorder Server (Optimized Multi-stage Build)
# 최종 수정: 2025-04-01
# ======================================================================
# 이 Dockerfile은 C++ CMake/vcpkg 프로젝트를 Linux 컨테이너 환경에서
# 빌드하고 실행하기 위한 명세서입니다. 멀티 스테이지 빌드를 사용하여
# 최종 이미지의 크기와 보안을 최적화합니다.

# ----------------------------------------------------------------------
# 스테이지 1: "builder" - 애플리케이션 빌드 전용 환경
# ----------------------------------------------------------------------
# 목적: 소스 코드를 컴파일하고 실행 파일을 생성하는 데 필요한 모든 도구와
#       라이브러리(vcpkg 의존성 포함)를 설치하고 빌드를 수행합니다.
# 기본 이미지: Ubuntu 24.04 LTS 버전을 사용합니다. 특정 버전을 명시하여
#              빌드의 재현성을 높이고, 최종 스테이지와 OS를 통일하여
#              GLIBC/GLIBCXX 버전 호환성 문제를 방지합니다.
FROM ubuntu:24.04 AS builder

# 빌드 과정 중 apt가 사용자 입력을 요구하지 않도록 설정 (CI/CD 환경 필수)
ENV DEBIAN_FRONTEND=noninteractive

# 빌드 필수 도구 및 vcpkg 의존성 빌드에 필요한 시스템 라이브러리 설치
# - apt-get update && ... && rm -rf ... : 한 레이어에서 실행하여 이미지 크기 최적화
# - --no-install-recommends: 불필요한 추천 패키지 설치 방지
# - build-essential, cmake, git, pkg-config: C++ 및 CMake 빌드 기본 도구
# - curl, zip, unzip, tar: vcpkg가 소스 다운로드/압축 해제 시 필요
# - ca-certificates: HTTPS 통신(Git 클론 등)을 위한 CA 인증서
# - libssl-dev: OpenSSL 개발 라이브러리 (MySQL Connector 등 많은 라이브러리가 요구)
# - libmysqlclient-dev: MySQL C API 개발 라이브러리 (vcpkg가 mysql-connector-cpp 빌드 시 필요할 수 있음)
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
    libmysqlclient-dev \
    # 설치 후 apt 캐시 삭제하여 이미지 레이어 크기 감소
    && rm -rf /var/lib/apt/lists/*

# vcpkg 설치 및 부트스트랩
# - /opt 디렉토리에 vcpkg를 설치하는 것이 일반적입니다.
WORKDIR /opt
# vcpkg 저장소를 클론하고 Linux용 부트스트랩 스크립트를 실행합니다.
# -disableMetrics: MS로 사용 통계 전송 비활성화 (선택 사항)
RUN git clone https://github.com/microsoft/vcpkg.git && \
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# 애플리케이션 소스 코드 복사 및 빌드 준비
WORKDIR /app

# Docker 빌드 캐시 최적화:
# 자주 변경되지 않는 의존성 정의 파일들을 먼저 복사합니다.
# 이후 단계에서 이 파일들이 변경되지 않으면, Docker는 캐시된 레이어를 재사용하여 빌드 속도를 높입니다.
COPY vcpkg.json CMakeLists.txt ./

# 나머지 소스 코드 복사:
# src, include 등 실제 코드는 의존성 파일보다 자주 변경되므로 나중에 복사합니다.
COPY src ./src
COPY include ./include
# 주석: tests 디렉토리는 -DBUILD_TESTING=OFF 로 빌드하지 않으므로 복사하지 않습니다.

# CMake 설정 및 빌드 실행
# - Release 모드로 빌드하여 최적화된 실행 파일을 생성합니다.
# - vcpkg 툴체인 파일을 지정하여 vcpkg와 연동하고 의존성을 자동으로 설치/링크합니다.
# - BUILD_TESTING=OFF 옵션으로 테스트 관련 코드는 빌드하지 않아 최종 결과물 크기를 줄입니다.
# - Ninja 빌드 시스템을 명시적으로 사용하고 (-G Ninja), 병렬 빌드(-j $(nproc))로 속도를 높입니다.
RUN cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-linux \
      -DBUILD_TESTING=OFF \
    && cmake --build build -j $(nproc)

# (진단용) 빌드된 실행 파일의 동적 라이브러리 의존성 확인 (선택 사항)
# 최종 스테이지에 복사해야 할 .so 파일들을 파악하는 데 도움이 됩니다.
# 빌드 로그에서 ldd 출력을 확인하세요.
RUN echo "Checking dynamic dependencies for the executable..." && \
    ldd /app/build/CherryRecorder-Server-App || echo "ldd failed or executable not found at /app/build/CherryRecorder-Server-App"


# ----------------------------------------------------------------------
# 스테이지 2: "Final" - 애플리케이션 실행 전용 환경
# ----------------------------------------------------------------------
# 목적: 빌드된 애플리케이션과 실행에 필요한 최소한의 런타임 라이브러리만 포함하는
#       가볍고 안전한 최종 이미지를 만듭니다.
# 기본 이미지: Builder 스테이지와 동일한 OS/버전(ubuntu:24.04)을 사용하여
#              GLIBC, libstdc++ 등 핵심 시스템 라이브러리 호환성을 보장합니다.
FROM ubuntu:24.04

# 환경 변수: apt 설치 시 사용자 입력 방지
ENV DEBIAN_FRONTEND=noninteractive

# 필수 런타임 시스템 라이브러리 설치
# - ca-certificates: 보안 연결(HTTPS, SSL/TLS)에 필요합니다.
# - netcat-openbsd: HEALTHCHECK에서 사용할 nc 명령어 도구를 제공합니다.
# - libmariadb3: MySQL C API의 MariaDB 호환 런타임 라이브러리입니다.
#   (주의: Builder 스테이지의 ldd 결과 및 vcpkg 라이브러리 복사 여부에 따라
#          이 패키지가 필요 없을 수도 있고, 또는 libmysqlclient 계열 패키지가 필요할 수도 있습니다.
#          가장 확실한 것은 아래 vcpkg 라이브러리 복사 방식을 사용하는 것입니다.)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    netcat-openbsd \
    libmariadb3 \
    # 다른 필수 시스템 런타임 라이브러리가 있다면 여기에 추가
    && rm -rf /var/lib/apt/lists/*

# vcpkg로 빌드된 필수 공유 라이브러리(.so) 복사 (가장 권장되는 방식)
# 목적: 애플리케이션 실행에 필요한 vcpkg 의존성 라이브러리 파일들을
#       Builder 스테이지의 vcpkg 설치 경로에서 최종 이미지로 직접 복사합니다.
#       시스템 라이브러리 버전과의 불일치 문제를 원천적으로 방지합니다.
# 참고: Builder 스테이지에서 ldd 명령 결과를 보고 필요한 파일을 정확히 식별하는 것이 좋습니다.
#       아래는 일반적인 예시이며, 실제 필요한 파일은 다를 수 있습니다.
COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libmysqlcppconn*.so* /usr/local/lib/
# COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_system.so* /usr/local/lib/ # 예시
# COPY --from=builder /app/build/vcpkg_installed/x64-linux/lib/libboost_thread.so* /usr/local/lib/ # 예시
# ... (ldd 결과 기반으로 필요한 다른 vcpkg .so 파일들 추가) ...

# 복사된 공유 라이브러리를 시스템 링커가 찾을 수 있도록 캐시 업데이트
RUN ldconfig

# 보안 강화: 전용 사용자 및 그룹 생성 ('appuser')
# 루트 권한이 필요 없는 애플리케이션은 보안을 위해 일반 사용자로 실행하는 것이 좋습니다.
RUN useradd --system --create-home --shell /bin/bash appuser

# 애플리케이션 작업 디렉토리 생성 및 소유권 변경
WORKDIR /home/appuser/app
# RUN chown appuser:appuser /home/appuser/app # WORKDIR 생성 시 자동으로 소유권 문제 해결될 수 있음

# Builder 스테이지에서 빌드된 최종 실행 파일 복사 및 권한 설정
# 경로 주의: CMake 빌드 결과 실행 파일이 생성된 정확한 경로로 지정해야 합니다.
COPY --from=builder /app/build/CherryRecorder-Server-App ./CherryRecorder-Server-App
RUN chmod +x ./CherryRecorder-Server-App # 실행 권한 부여

# 생성한 비-루트 사용자로 전환
USER appuser

# 애플리케이션 실행 포트 설정
# ARG: Docker 빌드 시 `--build-arg` 로 기본값을 변경할 수 있게 합니다. (선택 사항)
ARG SERVER_PORT=8080
# ENV: 컨테이너 실행 시 환경 변수를 설정합니다. 애플리케이션 코드(getenv)가 이 값을 읽습니다.
ENV SERVER_PORT=${SERVER_PORT}
# EXPOSE: 이 컨테이너가 어떤 포트를 외부에 노출할 것인지 Docker에 알립니다. (실제 포트 개방은 `docker run -p` 또는 ECS 설정)
EXPOSE ${SERVER_PORT}

# 컨테이너 상태 확인 (Health Check) 설정
# 지정된 간격으로 컨테이너 내부에서 명령을 실행하여 서비스 상태를 확인합니다.
# nc 명령어로 localhost의 $SERVER_PORT로 TCP 연결(스캔)을 시도합니다.
# ${SERVER_PORT:-8080}: 런타임 시 $SERVER_PORT 변수가 없으면 8080을 사용 (안전장치)
HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
  CMD nc -z 127.0.0.1 ${SERVER_PORT:-8080} || exit 1

# 이미지 메타데이터 라벨
LABEL maintainer="Kim Hyeonwoo <ialskdji@gmail.com>" \
      description="CherryRecorder Server Application - Provides location-based services with MySQL integration." \
      version="0.1.0"
       # 예시 버전

# 컨테이너 시작 시 실행될 기본 명령어 (애플리케이션 실행)
# CMD ["./executable", "arg1", "arg2"] 형식 사용
CMD ["./CherryRecorder-Server-App"]