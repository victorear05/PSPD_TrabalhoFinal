FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc \
    libc6-dev \
    libcurl4-openssl-dev \
    libjson-c-dev \
    && rm -rf /var/lib/apt/lists/*

COPY socket_server.c /app/socket_server.c
WORKDIR /app

RUN gcc -o socket_server socket_server.c -lcurl -ljson-c -lpthread

EXPOSE 8080

CMD ["./socket_server"]