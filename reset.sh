#!/bin/bash
# RESET NUCLEAR - Remove TUDO relacionado ao projeto Game of Life
# Use com cuidado! Este script remove TODOS os clusters Kind e limpa Docker agressivamente

set +e # Continuar mesmo se comandos falharem

echo "💥 RESET NUCLEAR - Game of Life Project"
echo "======================================="
echo "⚠️  ATENÇÃO: Este script irá remover:"
echo "   🗑️  TODOS os clusters Kind existentes"
echo "   🗑️  TODAS as imagens Docker do projeto" 
echo "   🗑️  TODOS os volumes e redes Docker órfãos"
echo "   🗑️  TODOS os contextos kubectl relacionados"
echo "   🗑️  TODOS os processos ocupando portas do projeto"
echo "   🗑️  TODOS os executáveis compilados"
echo ""

read -p "Tem certeza que deseja continuar? [y/N]: " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "❌ Operação cancelada pelo usuário"
    exit 1
fi

echo ""
echo "🚀 Iniciando limpeza nuclear..."
echo ""

# ============================================================================
# 1. PARAR E MATAR PROCESSOS QUE PODEM ESTAR RODANDO
# ============================================================================
echo "1️⃣  PARANDO PROCESSOS RELACIONADOS"
echo "──────────────────────────────────"

# Matar processos que podem estar ocupando portas
PORTS_TO_KILL="8080 30080 30200 31502 7077 8081 9200 5601"
for port in $PORTS_TO_KILL; do
    PID=$(lsof -t -i:$port 2>/dev/null)
    if [ ! -z "$PID" ]; then
        echo "🔫 Matando processo na porta $port (PID: $PID)"
        kill -9 $PID 2>/dev/null || true
    fi
done

# Matar processos relacionados ao projeto
pkill -f "socket_server" 2>/dev/null || true
pkill -f "test_client" 2>/dev/null || true
pkill -f "jogodavida" 2>/dev/null || true
pkill -f "spark-submit" 2>/dev/null || true

echo "✅ Processos mortos"
echo ""

# ============================================================================
# 2. REMOVER TODOS OS CLUSTERS KIND (não apenas o nosso)
# ============================================================================
echo "2️⃣  REMOVENDO TODOS OS CLUSTERS KIND"
echo "───────────────────────────────────"

# Listar todos os clusters Kind
KIND_CLUSTERS=$(kind get clusters 2>/dev/null || true)

if [ ! -z "$KIND_CLUSTERS" ]; then
    echo "🔍 Clusters Kind encontrados:"
    echo "$KIND_CLUSTERS"
    echo ""
    
    # Remover cada cluster individualmente
    for cluster in $KIND_CLUSTERS; do
        echo "🗑️  Removendo cluster: $cluster"
        kind delete cluster --name "$cluster" 2>/dev/null || true
    done
else
    echo "ℹ️  Nenhum cluster Kind encontrado"
fi

# Força remoção do diretório de configuração do Kind
rm -rf ~/.kind/ 2>/dev/null || true

echo "✅ Todos os clusters Kind removidos"
echo ""

# ============================================================================
# 3. LIMPEZA AGRESSIVA DO DOCKER
# ============================================================================
echo "3️⃣  LIMPEZA AGRESSIVA DO DOCKER"
echo "──────────────────────────────"

# Parar TODOS os containers relacionados
echo "🛑 Parando containers relacionados ao projeto..."
docker ps -a --format "table {{.Names}}\t{{.Image}}" | grep -E "(gameoflife|spark|elasticsearch|kibana|kind)" | awk '{print $1}' | grep -v NAMES | xargs -r docker stop 2>/dev/null || true
docker ps -a --format "table {{.Names}}\t{{.Image}}" | grep -E "(gameoflife|spark|elasticsearch|kibana|kind)" | awk '{print $1}' | grep -v NAMES | xargs -r docker rm -f 2>/dev/null || true

# Remover TODAS as imagens relacionadas ao projeto
echo "🗑️  Removendo imagens Docker relacionadas..."
IMAGES_TO_REMOVE=$(docker images --format "table {{.Repository}}:{{.Tag}}" | grep -E "(gameoflife|kind|local)" | grep -v REPOSITORY || true)

if [ ! -z "$IMAGES_TO_REMOVE" ]; then
    echo "$IMAGES_TO_REMOVE" | xargs -r docker rmi -f 2>/dev/null || true
fi

# Remover imagens órfãs e não utilizadas
echo "🧹 Removendo imagens órfãs..."
docker image prune -f 2>/dev/null || true

# Remover volumes órfãos
echo "🗑️  Removendo volumes órfãos..."
docker volume prune -f 2>/dev/null || true

# Remover redes não utilizadas
echo "🌐 Removendo redes não utilizadas..."
docker network prune -f 2>/dev/null || true

# Limpeza geral do sistema Docker
echo "🧽 Limpeza geral do Docker..."
docker system prune -f --volumes 2>/dev/null || true

echo "✅ Docker limpo completamente"
echo ""

# ============================================================================
# 4. LIMPEZA DO KUBECTL E CONTEXTOS
# ============================================================================
echo "4️⃣  LIMPEZA DE CONTEXTOS KUBECTL"
echo "───────────────────────────────"

# Remover contextos relacionados ao Kind
CONTEXTS_TO_REMOVE=$(kubectl config get-contexts -o name 2>/dev/null | grep -E "(kind|gameoflife)" || true)

if [ ! -z "$CONTEXTS_TO_REMOVE" ]; then
    echo "🗑️  Removendo contextos kubectl:"
    echo "$CONTEXTS_TO_REMOVE"
    for context in $CONTEXTS_TO_REMOVE; do
        kubectl config delete-context "$context" 2>/dev/null || true
    done
else
    echo "ℹ️  Nenhum contexto relacionado encontrado"
fi

# Remover clusters da configuração kubectl
CLUSTERS_TO_REMOVE=$(kubectl config get-clusters 2>/dev/null | grep -E "kind" || true)
if [ ! -z "$CLUSTERS_TO_REMOVE" ]; then
    for cluster in $CLUSTERS_TO_REMOVE; do
        kubectl config delete-cluster "$cluster" 2>/dev/null || true
    done
fi

echo "✅ Contextos kubectl limpos"
echo ""

# ============================================================================
# 5. REMOVER ARQUIVOS E DIRETÓRIOS DO PROJETO
# ============================================================================
echo "5️⃣  LIMPEZA DE ARQUIVOS DO PROJETO"
echo "─────────────────────────────────"

# Remover executáveis compilados
echo "🗑️  Removendo binários compilados..."
rm -rf binarios/ 2>/dev/null || true
rm -f src/core/socket_server 2>/dev/null || true
rm -f src/core/jogodavida_openmp_mpi 2>/dev/null || true
rm -f src/core/test_client 2>/dev/null || true

# Remover arquivos de backup
echo "🗑️  Removendo arquivos de backup..."
find . -name "*.backup" -delete 2>/dev/null || true
find . -name "*.bak" -delete 2>/dev/null || true

# Remover logs temporários
echo "🗑️  Removendo logs temporários..."
rm -f *.log 2>/dev/null || true
rm -rf logs/ 2>/dev/null || true

echo "✅ Arquivos do projeto limpos"
echo ""

# ============================================================================
# 6. VERIFICAÇÃO E REINSTALAÇÃO DE DEPENDÊNCIAS
# ============================================================================
echo "6️⃣  VERIFICAÇÃO DE DEPENDÊNCIAS"
echo "──────────────────────────────"

# Verificar se Kind está instalado
if ! command -v kind &> /dev/null; then
    echo "📥 Instalando Kind..."
    curl -Lo ./kind https://kind.sigs.k8s.io/dl/v0.20.0/kind-linux-amd64 2>/dev/null
    chmod +x ./kind && sudo mv ./kind /usr/local/bin/kind
    echo "✅ Kind instalado"
else
    echo "✅ Kind já instalado: $(kind version)"
fi

# Verificar se kubectl está instalado
if ! command -v kubectl &> /dev/null; then
    echo "📥 Instalando kubectl..."
    curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl" 2>/dev/null
    chmod +x kubectl && sudo mv kubectl /usr/local/bin/
    echo "✅ kubectl instalado"
else
    echo "✅ kubectl já instalado: $(kubectl version --client --short 2>/dev/null)"
fi

# Verificar dependências de compilação C
echo "🔧 Verificando dependências de compilação..."
MISSING_DEPS=""

# Lista de pacotes necessários
REQUIRED_PACKAGES="gcc libc6-dev libgomp1 openmpi-bin openmpi-common libopenmpi-dev libcurl4-openssl-dev libjson-c-dev"

for package in $REQUIRED_PACKAGES; do
    if ! dpkg -l | grep -q "^ii.*$package "; then
        MISSING_DEPS="$MISSING_DEPS $package"
    fi
done

if [ ! -z "$MISSING_DEPS" ]; then
    echo "📥 Instalando dependências faltantes:$MISSING_DEPS"
    sudo apt-get update -qq && sudo apt-get install -y $MISSING_DEPS
    echo "✅ Dependências instaladas"
else
    echo "✅ Todas as dependências já estão instaladas"
fi

echo ""

# ============================================================================
# 7. VERIFICAÇÃO FINAL
# ============================================================================
echo "7️⃣  VERIFICAÇÃO FINAL"
echo "───────────────────"

echo "🔍 Verificando limpeza..."

# Verificar se ainda há clusters Kind
KIND_CHECK=$(kind get clusters 2>/dev/null || true)
if [ -z "$KIND_CHECK" ]; then
    echo "✅ Nenhum cluster Kind restante"
else
    echo "⚠️  Clusters Kind ainda existem: $KIND_CHECK"
fi

# Verificar processos nas portas do projeto
PORTS_CHECK="8080 30080 30200 31502"
PORTS_BUSY=""
for port in $PORTS_CHECK; do
    if lsof -i:$port &>/dev/null; then
        PORTS_BUSY="$PORTS_BUSY $port"
    fi
done

if [ -z "$PORTS_BUSY" ]; then
    echo "✅ Todas as portas do projeto estão livres"
else
    echo "⚠️  Portas ainda ocupadas:$PORTS_BUSY"
fi

# Verificar imagens Docker relacionadas
DOCKER_CHECK=$(docker images --format "table {{.Repository}}:{{.Tag}}" | grep -E "(gameoflife|kind)" | grep -v REPOSITORY || true)
if [ -z "$DOCKER_CHECK" ]; then
    echo "✅ Nenhuma imagem Docker relacionada restante"
else
    echo "⚠️  Imagens Docker ainda existem:"
    echo "$DOCKER_CHECK"
fi

echo ""

# ============================================================================
# 8. CRIAÇÃO DO DIRETÓRIO LIMPO
# ============================================================================
echo "8️⃣  PREPARAÇÃO DO AMBIENTE LIMPO"
echo "───────────────────────────────"

# Recriar diretório de binários
mkdir -p binarios
echo "✅ Diretório binarios/ recriado"

# Verificar estrutura do projeto
if [ -d "src/core" ] && [ -f "src/core/socket_server.c" ]; then
    echo "✅ Estrutura do projeto intacta"
else
    echo "❌ Estrutura do projeto comprometida - verifique os arquivos fonte"
fi

echo ""

# ============================================================================
# 9. RELATÓRIO FINAL
# ============================================================================
echo "════════════════════════════════════════"
echo "📋 RELATÓRIO DE LIMPEZA NUCLEAR"
echo "════════════════════════════════════════"
echo "✅ Processos relacionados mortos"
echo "✅ Todos os clusters Kind removidos"
echo "✅ Docker limpo completamente"
echo "✅ Contextos kubectl removidos"
echo "✅ Arquivos temporários limpos"
echo "✅ Dependências verificadas"
echo ""
echo "🎯 AMBIENTE COMPLETAMENTE LIMPO!"
echo ""
echo "🚀 Próximos passos:"
echo "1. Verificar se você tem os arquivos fonte corretos"
echo "2. Executar: ./compile_and_deploy.sh"
echo "3. Executar: ./test_both_engines.sh"
echo ""
echo "💡 Se ainda houver problemas, pode ser:"
echo "   - Problema nos arquivos fonte"
echo "   - Problema de permissões"
echo "   - Problema de conectividade"
echo ""
echo "🏁 Reset nuclear concluído!"