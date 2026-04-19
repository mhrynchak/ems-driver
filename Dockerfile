FROM debian:bookworm-slim AS builder
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libmodbus-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY . .
RUN make

FROM debian:bookworm-slim AS runner
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends \
    libmodbus5 \
    libsqlite3-0 \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/ems-mvp ./ems-mvp
COPY --from=builder /app/config.docker.json ./config.docker.json
CMD ["./ems-mvp", "config.docker.json"]
