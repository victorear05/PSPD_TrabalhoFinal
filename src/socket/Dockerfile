FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc \
    libc6-dev \
    libgomp1 \
    libcurl4-openssl-dev \
    libjson-c-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY socket_server.c /app/socket_server.c
COPY jogodavida_openmp.c /app/jogodavida_openmp.c

RUN gcc -o socket_server socket_server.c \
    -lcurl -ljson-c -lpthread

RUN gcc -o jogodavida_openmp jogodavida_openmp.c \
    -fopenmp -lm

RUN mkdir -p /tmp

RUN ls -la /app/

EXPOSE 8080

CMD ["./socket_server"]