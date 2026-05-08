# ── Build stage ──────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DMCM_BUILD_CLIENT=OFF \
    && cmake --build build --parallel "$(nproc)"

# ── Runtime stage ─────────────────────────────────────────────────────────────
FROM ubuntu:24.04

COPY --from=builder /src/build/mcm /usr/local/bin/mcm

EXPOSE 9275

VOLUME ["/data"]

ENTRYPOINT ["mcm", "server"]
# Default: port 9275, persist to /data/collection.json
CMD ["9275", "/data/collection.json"]
