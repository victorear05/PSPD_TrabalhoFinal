# 🎮 Game of Life Distribuído - Documentação Completa

## 📋 Visão Geral

Este projeto implementa o "Jogo da Vida" de Conway como uma aplicação distribuída de larga escala, atendendo aos requisitos de **performance** (computação paralela) e **elasticidade** (adaptação automática à carga) usando tecnologias modernas como Kubernetes, OpenMP e ElasticSearch.

### 🎯 Objetivos

- **Performance**: Implementação paralela usando OpenMP/MPI ou Apache Spark
- **Elasticidade**: Orquestração com Kubernetes para escalonamento automático
- **Monitoramento**: Coleta de métricas com ElasticSearch/Kibana
- **Interface**: Acesso via Socket Server para múltiplos clientes

## 🏗️ Arquitetura

```
┌─────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Clients   │───▶│  Socket Server   │───▶│   Game Engine   │
│ (múltiplos) │    │   (Port 8080)    │    │ (OpenMP/Spark)  │
└─────────────┘    └──────────────────┘    └─────────────────┘
                           │                         │
                           ▼                         ▼
                   ┌──────────────────┐    ┌─────────────────┐
                   │  ElasticSearch   │    │   Kubernetes    │
                   │   + Kibana       │    │    Cluster      │
                   │  (Monitoring)    │    │ (Orquestração)  │
                   └──────────────────┘    └─────────────────┘
```

## 🛠️ Pré-requisitos

### Software Necessário

```bash
# Docker e Kind para Kubernetes
sudo apt-get update
sudo apt-get install -y docker.io
curl -Lo ./kind https://kind.sigs.k8s.io/dl/v0.20.0/kind-linux-amd64
chmod +x ./kind
sudo mv ./kind /usr/local/bin/kind

# kubectl
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
chmod +x kubectl
sudo mv kubectl /usr/local/bin/

# Compilador C com OpenMP
sudo apt-get install -y gcc libc6-dev libgomp1 libcurl4-openssl-dev libjson-c-dev
```

### Estrutura do Projeto

```
projeto/
├── src/
│   ├── core/
│   │   ├── jogodavida.c              # Versão sequencial original
│   │   ├── jogodavida_openmp.c       # Versão paralela com OpenMP
│   │   └── Dockerfile.openmp-engine  # Container para engine OpenMP
│   ├── socket/
│   │   ├── socket_server.c           # Servidor de conexões
│   │   ├── test_client.c             # Cliente de teste
│   │   └── Dockerfile                # Container para socket server
│   └── kubernetes/
│       ├── kind-cluster-config.yaml  # Configuração do cluster
│       ├── elasticsearch.yaml        # Deploy do ElasticSearch
│       ├── kibana.yaml               # Deploy do Kibana
│       ├── socket-server.yaml        # Deploy do socket server
│       └── openmp-engine.yaml        # Deploy do engine OpenMP
└── binarios/                         # Executáveis compilados (ignorado pelo git)
```

## 🚀 Compilação e Execução

### 1. Verificar Dependências

```bash
# Verificar compilador e bibliotecas
gcc --version
dpkg -l | grep -E "(libcurl4|libjson-c|libgomp)"

# Instalar dependências se necessário
sudo apt-get update
sudo apt-get install -y gcc libc6-dev libgomp1 libcurl4-openssl-dev libjson-c-dev
```

### 2. Compilação Local

```bash
# Criar diretório para binários
mkdir -p binarios

# Compilar versão sequencial
gcc -o binarios/jogodavida src/core/jogodavida.c -lm

# Compilar versão OpenMP
gcc -o binarios/jogodavida_openmp src/core/jogodavida_openmp.c \
    -fopenmp -lm

# Compilar socket server (com detecção automática de ambiente)
gcc -o binarios/socket_server src/socket/socket_server.c \
    -lcurl -ljson-c -lpthread

# Compilar cliente de teste
gcc -o binarios/test_client src/socket/test_client.c
```

### 2. Teste Local Rápido

```bash
# Executar versão sequencial
./binarios/jogodavida

# Executar versão OpenMP (4 threads)
export OMP_NUM_THREADS=4
./binarios/jogodavida_openmp

# Comparar performances
echo "=== Sequencial ==="
time ./binarios/jogodavida
echo "=== OpenMP (4 threads) ==="
time ./binarios/jogodavida_openmp
```

### 3. Setup do Cluster Kubernetes

```bash
# 1. Criar cluster Kind (1 master + 2 workers)
kind create cluster --config=src/kubernetes/kind-cluster-config.yaml

# 2. Verificar cluster
kubectl get nodes

# 3. Criar namespace
kubectl create namespace gameoflife

# 4. Construir imagens Docker
docker build -t gameoflife/socket-server:latest -f src/socket/Dockerfile src/socket/
docker build -t gameoflife/openmp-engine:latest -f src/core/Dockerfile.openmp-engine src/core/

# 5. Carregar imagens no Kind
kind load docker-image gameoflife/socket-server:latest
kind load docker-image gameoflife/openmp-engine:latest
```

### 4. Build Docker

```bash
# Copiar engine para pasta do socket (necessário para Dockerfile)
cp src/core/jogodavida_openmp.c src/socket/

# Construir imagem do socket server
docker build -t gameoflife/socket-server:latest -f src/socket/Dockerfile src/socket/

# Limpar arquivo temporário
rm src/socket/jogodavida_openmp.c

# Construir imagem do engine OpenMP (opcional)
docker build -t gameoflife/openmp-engine:latest -f src/core/Dockerfile.openmp-engine src/core/

# Carregar imagens no Kind
kind load docker-image gameoflife/socket-server:latest --name gameoflife-cluster
kind load docker-image gameoflife/openmp-engine:latest --name gameoflife-cluster
```

### 5. Deploy da Aplicação

```bash
# Deploy em ordem (dependências)
kubectl apply -f src/kubernetes/elasticsearch.yaml
kubectl apply -f src/kubernetes/kibana.yaml
kubectl apply -f src/kubernetes/socket-server.yaml
kubectl apply -f src/kubernetes/openmp-engine.yaml

# Verificar status
kubectl get pods -n gameoflife
kubectl get services -n gameoflife

# Aguardar pods ficarem prontos (pode demorar alguns minutos)
kubectl wait --for=condition=ready pod -l app=elasticsearch -n gameoflife --timeout=300s
kubectl wait --for=condition=ready pod -l app=socket-server -n gameoflife --timeout=120s
```

## 🧪 Testes e Uso

### 1. Teste Local do Socket Server

**Terminal 1 - Servidor:**
```bash
./binarios/socket_server
```

**Terminal 2 - Cliente:**
```bash
# Teste básico (rápido - até 32x32)
./binarios/test_client localhost -e openmp -min 3 -max 5

# Teste com parâmetros customizados
./binarios/test_client localhost -e openmp -min 3 -max 6 -t 2

# ⚠️ Cuidado: valores altos de POWMAX demoram muito!
# POWMAX=10 (1024x1024) pode demorar horas
# Recomendado: use POWMAX <= 7 (128x128) para testes
```

### 2. Teste no Kubernetes

```bash
# Testar aplicação no cluster (porta 30080)
./binarios/test_client -e openmp -min 3 -max 5

# Ver logs em tempo real
kubectl logs -n gameoflife deployment/socket-server -f

# Testar diferentes engines
./binarios/test_client -e spark -min 3 -max 4  # Placeholder Spark
```

### 3. Teste de Carga (Múltiplos Clientes)

```bash
# Script para simular múltiplos clientes
for i in {1..5}; do
    ./binarios/test_client localhost -e openmp -min 3 -max 4 &
done
wait

# Verificar logs do servidor
kubectl logs -n gameoflife deployment/socket-server
```

### 3. Acessar Interfaces Web

```bash
# Kibana Dashboard
echo "Kibana: http://localhost:31502"

# ElasticSearch
echo "ElasticSearch: http://localhost:30200"

# Socket Server
echo "Socket Server: localhost:30080"
```

### 4. Verificar Métricas no Kibana

1. Acesse `http://localhost:31502`
2. Vá em **Management > Stack Management > Index Patterns**
3. Crie pattern: `gameoflife-requests*`
4. Use `@timestamp` como campo de tempo
5. Vá em **Analytics > Discover** para ver dados

## 📊 Monitoramento e Métricas

### ElasticSearch Queries

```bash
# Ver todos os requests
curl -X GET "localhost:30200/gameoflife-requests/_search?pretty"

# Contar requests por IP
curl -X GET "localhost:30200/gameoflife-requests/_search?pretty" \
-H 'Content-Type: application/json' -d'
{
  "aggs": {
    "by_ip": {
      "terms": {
        "field": "client_ip.keyword"
      }
    }
  }
}'
```

### Visualizações Recomendadas no Kibana

1. **Line Chart**: Requests ao longo do tempo
2. **Pie Chart**: Distribuição por cliente IP
3. **Metric**: Total de requests
4. **Data Table**: Lista de requests recentes

## 🐛 Troubleshooting

### Problemas Comuns

**1. Warning de truncamento na compilação**
```bash
# Se aparecer warning sobre snprintf truncation
# Edite socket_server.c e aumente RESPONSE_BUFFER_SIZE para 12288
```

**2. Container não encontra engine**
```bash
# Verificar se engine está no container
kubectl exec -it deployment/socket-server -n gameoflife -- ls -la /app/

# O código detecta automaticamente se está local (binarios/) ou container (/app/)
```

**3. Cliente conecta mas não recebe resposta**
```bash
# Verificar logs do servidor
kubectl logs -n gameoflife deployment/socket-server -f

# Problema comum: POWMAX muito alto (tabuleiros grandes demoram muito)
# Use POWMAX <= 6 para testes rápidos
```

**4. Pods não iniciam**
```bash
# Verificar recursos e status
kubectl describe pods -n gameoflife
kubectl get events -n gameoflife --sort-by='.lastTimestamp'

# Forçar recriação
kubectl rollout restart deployment/socket-server -n gameoflife
```

**5. Build Docker falha**
```bash
# Erro comum: arquivo não encontrado
# Sempre copie jogodavida_openmp.c antes do build:
cp src/core/jogodavida_openmp.c src/socket/
docker build -t gameoflife/socket-server:latest -f src/socket/Dockerfile src/socket/
rm src/socket/jogodavida_openmp.c
```

**6. Cliente conecta na porta errada**
```bash
# Teste local: porta 8080
./binarios/test_client localhost -e openmp -min 3 -max 5

# Teste Kubernetes: porta 30080 (padrão)
./binarios/test_client -e openmp -min 3 -max 5
```

### Performance e Limites

**Tamanhos de Tabuleiro Recomendados:**
- **POWMAX=4** (16x16): < 1 segundo
- **POWMAX=5** (32x32): ~1-2 segundos  
- **POWMAX=6** (64x64): ~5-10 segundos
- **POWMAX=7** (128x128): ~30-60 segundos
- **POWMAX=8** (256x256): ~5-10 minutos
- **POWMAX=9** (512x512): ~20-40 minutos
- **POWMAX=10** (1024x1024): **várias horas!**

**⚠️ Para testes rápidos, use sempre POWMAX <= 6**

### Comandos de Debug

```bash
# Status geral
kubectl get all -n gameoflife

# Logs em tempo real
kubectl logs -n gameoflife deployment/socket-server -f

# Entrar no pod para debug
kubectl exec -it deployment/socket-server -n gameoflife -- /bin/bash

# Port-forward manual se necessário
kubectl port-forward -n gameoflife service/socket-server 8080:8080
```

## 🔧 Configurações Avançadas

### Ajustar Recursos do Cluster

```yaml
# Em openmp-engine.yaml, modificar:
resources:
  requests:
    memory: 1Gi      # Aumentar se necessário
    cpu: 1000m
  limits:
    memory: 4Gi
    cpu: 4000m
```

### Configurar Autoscaling

```bash
# Verificar HPA
kubectl get hpa -n gameoflife

# Simular carga para testar autoscaling
for i in {1..100}; do
    ./binarios/test_client localhost &
    sleep 0.1
done
```

### Modificar Parâmetros do Jogo da Vida

No arquivo `jogodavida_openmp.c`, alterar:
```c
#define POWMIN 3    // Tamanho mínimo: 2^3 = 8x8
#define POWMAX 10   // Tamanho máximo: 2^10 = 1024x1024
```

## 📈 Métricas de Performance

### Benchmarks Reais (baseado em testes)

| Versão | Tamanho (POWMAX) | Tempo Aproximado | Uso Recomendado |
|--------|------------------|------------------|-----------------|
| Sequencial | 32x32 (5) | ~2s | Baseline |
| OpenMP (4 cores) | 32x32 (5) | ~0.5s | Testes rápidos |
| OpenMP (4 cores) | 64x64 (6) | ~5s | Demo funcional |
| OpenMP (4 cores) | 128x128 (7) | ~30s | Teste performance |
| OpenMP (4 cores) | 1024x1024 (10) | **várias horas** | **Não recomendado** |

### Comparação de Escalabilidade

```bash
# Testar diferentes números de threads (local)
for threads in 1 2 4 8; do
    echo "=== Testando com $threads threads ==="
    export OMP_NUM_THREADS=$threads
    time ./binarios/jogodavida_openmp
done

# Testar via socket server (recomendado para testes)
./binarios/test_client localhost -e openmp -min 3 -max 5 -t 1
./binarios/test_client localhost -e openmp -min 3 -max 5 -t 4
./binarios/test_client localhost -e openmp -min 3 -max 5 -t 8
```

### Monitoramento de Recursos

```bash
# Ver uso de CPU/memória dos pods
kubectl top pods -n gameoflife

# Ver métricas no ElasticSearch
curl -X GET "localhost:30200/gameoflife-requests/_search?pretty&size=10"

# Acessar dashboard Kibana
echo "Kibana: http://localhost:31502"
```

## 📝 Próximos Passos

1. **Implementar engine Apache Spark** (segunda opção de paralelismo)
2. **Adicionar MPI** para distribuição entre nós
3. **Implementar interface REST** além do socket
4. **Criar dashboards Kibana** mais elaborados
5. **Adicionar testes automatizados**
6. **Implementar balanceamento de carga** no socket server

## 📚 Referências

- [Conway's Game of Life](http://ddi.cs.unipotsdam.de/HyFISCH/Produzieren/lis_projekt/proj_gamelife/ConwayScientificAmerican.htm)
- [OpenMP Documentation](https://www.openmp.org/)
- [Kubernetes Documentation](https://kubernetes.io/docs/)
- [ElasticSearch Guide](https://www.elastic.co/guide/)

## 📄 Licença

Este projeto é desenvolvido para fins acadêmicos na disciplina PSPD - Programação para Sistemas Paralelos e Distribuídos, Universidade de Brasília.