# ---- Build stage ----
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y \
    g++ cmake git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target broker_server_epoll -j"$(nproc)"

# ---- Runtime stage ----
FROM ubuntu:24.04

RUN useradd -m streamforge
WORKDIR /app
COPY --from=build /src/build/broker_server_epoll /app/broker_server_epoll

ENV SF_PORT=9001
ENV SF_WORKERS=4
ENV SF_PARTITIONS=4
ENV SF_DATA_DIR=/data

RUN mkdir -p /data && chown streamforge:streamforge /data
USER streamforge

EXPOSE 9001
VOLUME ["/data"]

ENTRYPOINT ["/bin/sh", "-c", "exec stdbuf -oL -eL /app/broker_server_epoll --port \"$SF_PORT\" --workers \"$SF_WORKERS\" --partitions \"$SF_PARTITIONS\" --data-dir \"$SF_DATA_DIR\""]
