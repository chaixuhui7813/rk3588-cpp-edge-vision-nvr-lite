FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    ca-certificates \
    bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN cmake -S . -B build && cmake --build build -j"$(nproc)"

EXPOSE 8080
CMD ["./build/edge_vision_nvr", "--config", "config.yaml", "--mock"]
