# syntax=docker/dockerfile:1

FROM ubuntu:26.04 AS bazel

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

# Keep BAZEL_VERSION equal to .bazelversion, and BAZEL_DIST_SHA256 equal to the
# release's bazel-<version>-dist.zip.sha256.
ENV BAZEL_VERSION=8.2.1
ENV BAZEL_DIST_SHA256=b12b95cc02cc5ee19ff7a6a7f71f9496631f937c8e13a8f53ee0ff7b7700bc16
RUN curl -fL -O https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-dist.zip \
    && echo "${BAZEL_DIST_SHA256}  bazel-${BAZEL_VERSION}-dist.zip" | sha256sum -c - \
    && unzip bazel-${BAZEL_VERSION}-dist.zip -d bazel-src \
    && cd bazel-src \
    && env EXTRA_BAZEL_ARGS="--tool_java_runtime_version=local_jdk" bash ./compile.sh

FROM ubuntu:26.04 AS psi-build

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

WORKDIR /work

