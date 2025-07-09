# syntax=docker/dockerfile:1

FROM ubuntu:24.04 AS bazel

WORKDIR /root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update \
 && apt-get upgrade -y \
 && apt-get install -y --no-install-recommends \
    curl \
    build-essential \
    openjdk-21-jdk \
    python3 \
    python-is-python3 \
    zip \
    unzip \
 && rm -rf /var/lib/apt/lists/*

ENV BAZEL_VERSION=8.3.1
RUN curl -L -O https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-dist.zip && \
    unzip bazel-${BAZEL_VERSION}-dist.zip -d bazel-src && \
    cd bazel-src && \
    env EXTRA_BAZEL_ARGS="--tool_java_runtime_version=local_jdk" bash ./compile.sh

FROM ubuntu:24.04 AS psi-build

WORKDIR /root

COPY --from=bazel /root/bazel-src/output/bazel /usr/local/bin

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update \
 && apt-get upgrade -y \
 && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    ca-certificates \
    nodejs \
    npm \
    openjdk-21-jdk \
 && rm -rf /var/lib/apt/lists/*
