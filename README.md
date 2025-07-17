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

### 1. Compilação Local

```bash
# Criar diretório para binários
mkdir -p binarios

# Compilar versão sequencial
gcc -o binarios/jogodavida src/core/jogodavida.c -lm

# Compilar versão OpenMP
gcc -o binarios/jogodavida_openmp src/core/jogodavida_openmp.c \
    -fopenmp -lm

# Compilar socket server
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

### 4. Deploy da Aplicação

```bash
# Deploy em ordem (dependências)
kubectl apply -f src/kubernetes/elasticsearch.yaml
kubectl apply -f src/kubernetes/kibana.yaml
kubectl apply -f src/kubernetes/socket-server.yaml
kubectl apply -f src/kubernetes/openmp-engine.yaml

# Verificar status
kubectl get pods -n gameoflife
kubectl get services -n gameoflife
```

### 5. Aguardar Inicialização

```bash
# Monitorar pods até ficarem Running
kubectl get pods -n gameoflife -w

# Verificar logs se necessário
kubectl logs -n gameoflife deployment/elasticsearch
kubectl logs -n gameoflife deployment/kibana
kubectl logs -n gameoflife deployment/socket-server
```

## 🧪 Testes e Uso

### 1. Teste Básico do Socket Server

```bash
# Compilar cliente se não feito
gcc -o binarios/test_client src/socket/test_client.c

# Testar conexão
./binarios/test_client localhost

# Resultado esperado:
# 🔌 Conectando ao servidor localhost:30080
# ✅ Conectado! Enviando requisição...
# 📨 Resposta do servidor:
# REQUEST_ID:0
# TIMESTAMP:1705123456
# CLIENT_IP:127.0.0.1
# STATUS:SUCCESS
```

### 2. Teste de Carga (Múltiplos Clientes)

```bash
# Script para simular múltiplos clientes
for i in {1..10}; do
    ./binarios/test_client localhost &
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

**1. Pods não iniciam**
```bash
# Verificar recursos
kubectl describe pods -n gameoflife

# Verificar logs
kubectl logs -n gameoflife deployment/elasticsearch
```

**2. ElasticSearch não aceita dados**
```bash
# Verificar se está rodando
curl -X GET "localhost:30200/_cluster/health?pretty"

# Verificar índices
curl -X GET "localhost:30200/_cat/indices?pretty"
```

**3. Kibana não conecta**
```bash
# Verificar variáveis de ambiente
kubectl get pods -n gameoflife -o yaml | grep -A5 -B5 ELASTICSEARCH
```

**4. Socket server não responde**
```bash
# Verificar se porta está aberta
telnet localhost 30080

# Verificar logs
kubectl logs -n gameoflife deployment/socket-server -f
```

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

### Benchmarks Esperados

| Versão | Tamanho | Tempo (aproximado) |
|--------|---------|-------------------|
| Sequencial | 1024x1024 | ~2.5s |
| OpenMP (4 cores) | 1024x1024 | ~0.8s |
| OpenMP (8 cores) | 1024x1024 | ~0.5s |

### Comparação de Escalabilidade

```bash
# Testar diferentes números de threads
for threads in 1 2 4 8; do
    echo "=== Testando com $threads threads ==="
    export OMP_NUM_THREADS=$threads
    time ./binarios/jogodavida_openmp
done
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

## 🤝 Contribuição

Para contribuir com o projeto:

1. Fork o repositório
2. Crie uma branch para sua feature (`git checkout -b feature/nova-feature`)
3. Commit suas mudanças (`git commit -am 'Adiciona nova feature'`)
4. Push para a branch (`git push origin feature/nova-feature`)
5. Abra um Pull Request

## 📄 Licença

Este projeto é desenvolvido para fins acadêmicos na disciplina PSPD - Programação para Sistemas Paralelos e Distribuídos, Universidade de Brasília.