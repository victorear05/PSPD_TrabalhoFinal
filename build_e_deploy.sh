#!/bin/bash
# Este script faz o build e deploy completo da aplicação a partir de um ambiente limpo.
set -e # O script irá parar se qualquer comando falhar.

# --- PASSO 1: Definir a Versão da Imagem ---
# Usamos uma tag específica para evitar problemas de cache com a tag ':latest'.
# Se você fizer novas alterações, pode mudar para "v1.0.1", "v1.0.2", etc.
IMAGE_TAG="v1.0.0"
IMAGE_NAME="gameoflife/socket-server:${IMAGE_TAG}"

echo "🚀 Iniciando o processo de build e deploy para a versão: ${IMAGE_TAG}"

# --- PASSO 2: Compilar o Cliente de Teste Local ---
echo "🛠️  Compilando o cliente de teste local..."
mkdir -p binarios
gcc -o binarios/test_client src/core/test_client.c -O3

# --- PASSO 3: Criar o Cluster Kubernetes com Kind ---
echo "📦 Criando o cluster Kubernetes local 'gameoflife-cluster-optimized'..."
kind create cluster --config=src/kubernetes/kind-cluster-config.yaml
kubectl create namespace gameoflife

# --- PASSO 4: Construir a Imagem Docker ---
echo "🐳 Construindo a imagem Docker '${IMAGE_NAME}'..."
# A flag --no-cache garante que o código C será recompilado.
docker build --no-cache -t "${IMAGE_NAME}" -f src/core/Dockerfile src/core/

# --- PASSO 5: Carregar a Imagem no Cluster ---
echo "🚚 Carregando a imagem Docker para dentro do cluster Kind..."
kind load docker-image "${IMAGE_NAME}" --name gameoflife-cluster-optimized

# --- PASSO 6: Atualizar e Aplicar os Arquivos de Deploy ---
echo "📄 Atualizando o arquivo de deploy para usar a nova tag e aplicando no cluster..."

# Este comando 'sed' atualiza a tag da imagem no arquivo de configuração.
# Ele cria um backup (.bak) que será removido no final.
sed -i.bak "s|image:.*|image: ${IMAGE_NAME}|g" src/kubernetes/socket-server.yaml

# Aplicando todas as configurações do Kubernetes
kubectl apply -f src/kubernetes/elasticsearch.yaml
kubectl apply -f src/kubernetes/kibana.yaml
kubectl apply -f src/kubernetes/spark-master.yaml
kubectl apply -f src/kubernetes/spark-worker.yaml
kubectl apply -f src/kubernetes/socket-server.yaml

# --- PASSO 7: Aguardar Todos os Pods Ficarem Prontos ---
echo "⏳ Aguardando todos os pods na namespace 'gameoflife' estarem prontos (isso pode levar alguns minutos)..."
kubectl wait --for=condition=ready pod --all -n gameoflife --timeout=5m

# --- PASSO 8: Verificação Final e Testes ---
echo "✅ Ambiente construído com sucesso!"
echo "🔍 Verificando os logs do novo pod para confirmar a versão..."
kubectl logs deployment/socket-server -n gameoflife

echo -e "\n\n--- 🚀 EXECUTANDO TESTES ---"

echo "--- Testando engine OpenMP+MPI ---"
./binarios/test_client -e openmp_mpi -min 3 -max 5

echo -e "\n--- Testando engine Spark ---"
./binarios/test_client -e spark -min 3 -max 5

# --- Limpeza Final ---
# Remove o arquivo de backup criado pelo 'sed'
rm src/kubernetes/socket-server.yaml.bak

echo -e "\n🎉 Processo concluído!"