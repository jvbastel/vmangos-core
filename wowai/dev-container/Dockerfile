# The dev container of the fork. See `wowai/dev`.
#
# It uses the same base image and the same build packages as the build stage
# of `docker/server/Dockerfile` in vmangos-deploy. `wowai/dev` reads the base
# image from that Dockerfile and passes it as `BUILD_BASE`. This image adds
# only rsync, for the copy of the source tree, GoogleTest, for the code
# tests of src/bots, and shellcheck, for `wowai/shellcheck.sh`.
ARG BUILD_BASE=ubuntu:26.04
FROM ${BUILD_BASE}

ARG DEBIAN_FRONTEND=noninteractive

RUN \
  apt update && \
  apt install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libcurl4-openssl-dev \
    libmysqlclient-dev \
    libssl-dev \
    zlib1g-dev \
    libgtest-dev \
    rsync \
    shellcheck && \
  rm -rf /var/lib/apt/lists/*

CMD ["sleep", "infinity"]
