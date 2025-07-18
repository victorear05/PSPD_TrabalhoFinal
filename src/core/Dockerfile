# Usar imagem base do Ubuntu
FROM ubuntu:22.04

# Evitar prompts interativos durante o build
ENV DEBIAN_FRONTEND=noninteractive

# Instalar dependências de C, Python, Java (para Spark) e CURL
RUN apt-get update && apt-get install -y \
    gcc \
    libc6-dev \
    libgomp1 \
    libcurl4-openssl-dev \
    libjson-c-dev \
    openmpi-bin \
    openmpi-common \
    libopenmpi-dev \
    numactl \
    python3 \
    python3-pip \
    openjdk-11-jre-headless \
    ca-certificates-java \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Instalar PySpark
RUN pip3 install pyspark==3.5.0

# Baixar e instalar binários do Spark
RUN curl -o /tmp/spark.tgz https://archive.apache.org/dist/spark/spark-3.5.0/spark-3.5.0-bin-hadoop3.tgz && \
    tar -xzf /tmp/spark.tgz -C /opt/ && \
    mv /opt/spark-3.5.0-bin-hadoop3 /opt/spark && \
    rm /tmp/spark.tgz

WORKDIR /app

# Copiar todos os códigos-fonte necessários
COPY socket_server.c /app/
COPY jogodavida_openmp_mpi.c /app/
COPY jogodavida_spark.py /app/

ARG OPTIMIZATION_FLAGS="-O3 -march=native -mtune=native -flto -funroll-loops"

# Compilar os códigos C
RUN echo "Compilando com flags: $OPTIMIZATION_FLAGS" && \
    gcc $OPTIMIZATION_FLAGS -o socket_server socket_server.c \
        -lcurl -ljson-c -lpthread && \
    mpicc $OPTIMIZATION_FLAGS -o jogodavida_openmp_mpi jogodavida_openmp_mpi.c \
        -fopenmp -lm

RUN mkdir -p /tmp /dev/shm && \
    chmod 1777 /tmp /dev/shm

# Definir variáveis de ambiente para Spark e OpenMP
ENV SPARK_HOME=/opt/spark
ENV PATH=$PATH:$SPARK_HOME/bin
ENV OMP_NUM_THREADS=4
ENV OMP_PROC_BIND=true
ENV OMP_PLACES=cores
ENV MALLOC_ARENA_MAX=2
ENV MALLOC_MMAP_THRESHOLD_=131072

EXPOSE 8080

CMD ["./socket_server"]