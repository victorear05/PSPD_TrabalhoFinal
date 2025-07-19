#!/bin/bash
# Script ULTRA ROBUSTO de compilação e deploy com correções do Spark
# Este script força limpeza e é resistente a ambientes sujos

set -e # O script irá parar se qualquer comando falhar.

IMAGE_TAG="v2.0.0-ultrafix-$(date +%Y%m%d-%H%M%S)"
IMAGE_NAME="gameoflife/socket-server:${IMAGE_TAG}"

echo "🚀 COMPILAÇÃO E DEPLOY ULTRA ROBUSTO - Game of Life"
echo "===================================================="
echo "Versão: ${IMAGE_TAG}"
echo "🔧 Esta versão corrige o problema do Spark E força limpeza!"
echo ""

# ============================================================================
# VERIFICAÇÃO PRÉ-DEPLOY AUTOMÁTICA
# ============================================================================
echo "🔍 EXECUTANDO VERIFICAÇÃO PRÉ-DEPLOY AUTOMÁTICA..."
echo ""

# Verificar se há clusters Kind conflitantes
KIND_CLUSTERS=$(kind get clusters 2>/dev/null || true)
if [ ! -z "$KIND_CLUSTERS" ]; then
    echo "⚠️  Clusters Kind detectados: $KIND_CLUSTERS"
    echo "🧹 Removendo automaticamente para evitar conflitos..."
    for cluster in $KIND_CLUSTERS; do
        kind delete cluster --name "$cluster" 2>/dev/null || true
    done
    echo "✅ Clusters removidos"
fi

# Verificar e matar processos em portas conflitantes
PORTS_TO_CLEAR="8080 30080 30200 31502 7077"
for port in $PORTS_TO_CLEAR; do
    PID=$(lsof -t -i:$port 2>/dev/null || true)
    if [ ! -z "$PID" ]; then
        echo "🔫 Matando processo na porta $port (PID: $PID)"
        kill -9 $PID 2>/dev/null || true
    fi
done

# Limpeza de imagens Docker antigas do projeto
echo "🧹 Limpando imagens Docker antigas do projeto..."
docker images --format "table {{.Repository}}:{{.Tag}}" | grep "gameoflife" | grep -v REPOSITORY | awk '{print $1":"$2}' | xargs -r docker rmi -f 2>/dev/null || true

echo "✅ Pré-limpeza concluída"
echo ""

# ============================================================================
# PASSO 1: COMPILAR CLIENTE DE TESTE LOCAL
# ============================================================================
echo "1

# Aqui você deve colar o conteúdo do socket_server_corrigido.c no arquivo src/core/socket_server.c
echo "⚠️  ATENÇÃO: Substituindo socket_server.c pela versão corrigida..."

# Criar nova versão corrigida
cat > src/core/socket_server.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <sys/wait.h>
#include <sys/time.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 4096
#define RESULT_BUFFER_SIZE 8192
#define RESPONSE_BUFFER_SIZE 12288
#define PIPE_BUFFER_SIZE 16384

typedef struct {
    int socket;
    struct sockaddr_in address;
    char ip_str[INET_ADDRSTRLEN];
} client_info_t;

typedef struct {
    int powmin;
    int powmax;
    char engine_type[32];
} gameoflife_request_t;

typedef struct {
    int request_id;
    char status[32];
    double execution_time;
    double total_time;
    char engine_used[32];
    char results[RESULT_BUFFER_SIZE];
    char error_message[512];
} gameoflife_response_t;

static int request_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

const char* ELASTICSEARCH_URL = "http://elasticsearch:9200";
const char* INDEX_NAME = "gameoflife-requests";

struct elasticsearch_response {
    char* data;
    size_t size;
};

double wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, struct elasticsearch_response* response) {
    size_t total_size = size * nmemb;
    
    response->data = realloc(response->data, response->size + total_size + 1);
    if (response->data == NULL) {
        printf("Erro: falha ao alocar memória\n");
        return 0;
    }
    
    memcpy(&(response->data[response->size]), contents, total_size);
    response->size += total_size;
    response->data[response->size] = 0;
    
    return total_size;
}

int send_metrics_to_elasticsearch(int request_id, const char* client_ip, time_t timestamp, gameoflife_response_t* response) {
    CURL* curl;
    CURLcode res;
    struct elasticsearch_response es_response = {0};
    
    curl = curl_easy_init();
    if (!curl) return -1;
    
    struct tm* tm_info = gmtime(&timestamp);
    char iso_buffer[64];
    strftime(iso_buffer, sizeof(iso_buffer), "%Y-%m-%dT%H:%M:%S.000Z", tm_info);
    
    json_object* doc = json_object_new_object();
    json_object_object_add(doc, "request_id", json_object_new_int(request_id));
    json_object_object_add(doc, "client_ip", json_object_new_string(client_ip));
    json_object_object_add(doc, "timestamp", json_object_new_int64(timestamp));
    json_object_object_add(doc, "@timestamp", json_object_new_string(iso_buffer));
    json_object_object_add(doc, "server", json_object_new_string("socket-server-optimized"));
    json_object_object_add(doc, "status", json_object_new_string(response->status));
    json_object_object_add(doc, "engine_type", json_object_new_string(response->engine_used));
    json_object_object_add(doc, "execution_time_seconds", json_object_new_double(response->execution_time));
    json_object_object_add(doc, "total_time_seconds", json_object_new_double(response->total_time));

    if (strlen(response->error_message) > 0) {
        json_object_object_add(doc, "error_message", json_object_new_string(response->error_message));
    }

    const char* json_string = json_object_to_json_string(doc);

    char url[512];
    snprintf(url, sizeof(url), "%s/%s/_doc/%d", ELASTICSEARCH_URL, INDEX_NAME, request_id);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &es_response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        printf("ElasticSearch response: HTTP %ld\n", response_code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    json_object_put(doc);

    if (es_response.data) {
        free(es_response.data);
    }
    
    return (res == CURLE_OK) ? 0 : -1;
}

int execute_engine(gameoflife_request_t* request, gameoflife_response_t* response) {
    int pipefd[2];
    pid_t pid;
    char buffer[PIPE_BUFFER_SIZE];
    int status;
    double start_time, end_time;

    // VALIDAÇÃO INICIAL: verificar se engine é suportada
    if (strcmp(request->engine_type, "spark") != 0 && strcmp(request->engine_type, "openmp_mpi") != 0) {
        strcpy(response->status, "ERROR");
        snprintf(response->error_message, sizeof(response->error_message), 
                 "Engine '%s' não suportada. Use 'openmp_mpi' ou 'spark'", request->engine_type);
        return -1;
    }

    if (pipe(pipefd) == -1) {
        strcpy(response->status, "ERROR");
        strcpy(response->error_message, "Falha ao criar pipe");
        return -1;
    }

    // EXECUTAR ENGINE SPARK
    if (strcmp(request->engine_type, "spark") == 0) {
        printf("Executando engine Spark: POWMIN=%d, POWMAX=%d\n", request->powmin, request->powmax);
        printf("Comando: spark-submit --master spark://spark-master:7077 /app/jogodavida_spark.py %d %d\n", 
               request->powmin, request->powmax);
        fflush(stdout);

        start_time = wall_time();
        pid = fork();

        if (pid == -1) {
            strcpy(response->status, "ERROR");
            strcpy(response->error_message, "Falha ao criar processo para Spark");
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }

        if (pid == 0) { // Processo filho
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            char powmin_str[16], powmax_str[16];
            snprintf(powmin_str, sizeof(powmin_str), "%d", request->powmin);
            snprintf(powmax_str, sizeof(powmax_str), "%d", request->powmax);

            // Tentar executar spark-submit
            execl("/opt/spark/bin/spark-submit",
                  "spark-submit",
                  "--master", "spark://spark-master:7077",
                  "--deploy-mode", "client",
                  "--conf", "spark.sql.adaptive.enabled=false",
                  "/app/jogodavida_spark.py",
                  powmin_str,
                  powmax_str,
                  NULL);
            
            // Se execl retornar, houve um erro
            printf("ERRO: Falha ao executar spark-submit\n");
            exit(1);
        }
    }
    // EXECUTAR ENGINE OPENMP+MPI
    else if (strcmp(request->engine_type, "openmp_mpi") == 0) {
        printf("Executando engine OpenMP+MPI: POWMIN=%d, POWMAX=%d\n", request->powmin, request->powmax);
        printf("Comando: mpirun -np 2 ./jogodavida_openmp_mpi --powmin %d --powmax %d\n", 
               request->powmin, request->powmax);
        fflush(stdout);

        start_time = wall_time();
        pid = fork();

        if (pid == -1) {
            strcpy(response->status, "ERROR");
            strcpy(response->error_message, "Falha ao criar processo para OpenMP+MPI");
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }

        if (pid == 0) { // Processo filho
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            
            char powmin_str[16], powmax_str[16];
            snprintf(powmin_str, sizeof(powmin_str), "%d", request->powmin);
            snprintf(powmax_str, sizeof(powmax_str), "%d", request->powmax);
            
            if (access("/app/jogodavida_openmp_mpi", F_OK) == 0) {
                execl("/usr/bin/mpirun", "mpirun", "-np", "2", "--allow-run-as-root", 
                      "/app/jogodavida_openmp_mpi", "--powmin", powmin_str, "--powmax", powmax_str, NULL);
            } else if (access("binarios/jogodavida_openmp_mpi", F_OK) == 0) {
                chdir("binarios");
                execl("/usr/bin/mpirun", "mpirun", "-np", "2", 
                      "./jogodavida_openmp_mpi", "--powmin", powmin_str, "--powmax", powmax_str, NULL);
            } else {
                printf("ERRO: Engine híbrida não encontrada em /app/ ou binarios/\n");
                exit(1);
            }
            
            printf("ERRO: Falha ao executar engine híbrida\n");
            exit(1);
        }
    }

    // CÓDIGO COMUM: ler output e aguardar término
    close(pipefd[1]);
    response->results[0] = '\0';
    ssize_t bytes_read;
    size_t total_read = 0;
    
    while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        
        if (total_read + bytes_read < RESULT_BUFFER_SIZE - 1) {
            strcat(response->results, buffer);
            total_read += bytes_read;
        }
    }
    
    close(pipefd[0]);
    waitpid(pid, &status, 0);
    
    end_time = wall_time();
    response->execution_time = end_time - start_time;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        strcpy(response->status, "SUCCESS");
        if (strcmp(request->engine_type, "spark") == 0) {
            strcpy(response->engine_used, "spark-cluster");
        } else {
            strcpy(response->engine_used, "openmp-mpi-container");
        }
    } else {
        strcpy(response->status, "ERROR");
        snprintf(response->error_message, sizeof(response->error_message), 
                 "%s terminou com erro (código: %d)", 
                 request->engine_type, WEXITSTATUS(status));
    }
    
    return 0;
}

int parse_simplified_request(const char* request_str, gameoflife_request_t* request) {
    char* request_copy = strdup(request_str);
    char* token;
    char* saveptr;
    
    int has_engine = 0, has_powmin = 0, has_powmax = 0;
    
    strcpy(request->engine_type, "");
    request->powmin = -1;
    request->powmax = -1;
    
    token = strtok_r(request_copy, ";", &saveptr);
    while (token != NULL) {
        char* colon_pos = strchr(token, ':');
        
        if (colon_pos != NULL) {
            *colon_pos = '\0';
            char* key = token;
            char* value = colon_pos + 1;
            
            if (strcmp(key, "ENGINE") == 0) {
                strncpy(request->engine_type, value, sizeof(request->engine_type) - 1);
                has_engine = 1;
            } else if (strcmp(key, "POWMIN") == 0) {
                request->powmin = atoi(value);
                has_powmin = 1;
            } else if (strcmp(key, "POWMAX") == 0) {
                request->powmax = atoi(value);
                has_powmax = 1;
            }
        }
        
        token = strtok_r(NULL, ";", &saveptr);
    }
    
    free(request_copy);
    
    if (!has_engine) return -1;
    if (!has_powmin) return -2;
    if (!has_powmax) return -3;
    if (request->powmin < 1) return -4;
    if (request->powmax < 1) return -5;
    if (request->powmax < request->powmin) return -6;
    if (strcmp(request->engine_type, "openmp_mpi") != 0 && strcmp(request->engine_type, "spark") != 0) return -7;
    
    return 0;
}

void* handle_client(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    gameoflife_request_t game_request;
    gameoflife_response_t game_response;
    char response_buffer[RESPONSE_BUFFER_SIZE];
    
    time_t now = time(NULL);
    double total_start_time = wall_time();
    
    pthread_mutex_lock(&counter_mutex);
    int current_id = request_counter++;
    pthread_mutex_unlock(&counter_mutex);
    
    memset(&game_response, 0, sizeof(game_response));
    game_response.request_id = current_id;
    
    printf("Cliente conectado: %s, ID: %d\n", client->ip_str, current_id);
    
    int bytes_read = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        printf("Erro ao ler dados do cliente %s\n", client->ip_str);
        close(client->socket);
        free(client);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    printf("Cliente %s enviou: %s\n", client->ip_str, buffer);
    fflush(stdout);
    
    int parse_result = parse_simplified_request(buffer, &game_request);
    if (parse_result != 0) {
        strcpy(game_response.status, "ERROR");
        switch (parse_result) {
            case -1:
                strcpy(game_response.error_message, "Parâmetro ENGINE obrigatório. Use: ENGINE:openmp_mpi ou ENGINE:spark");
                break;
            case -2:
                strcpy(game_response.error_message, "Parâmetro POWMIN obrigatório. Use: POWMIN:3");
                break;
            case -3:
                strcpy(game_response.error_message, "Parâmetro POWMAX obrigatório. Use: POWMAX:10");
                break;
            case -4:
                strcpy(game_response.error_message, "POWMIN inválido. Não deve ser menor que 1");
                break;
            case -5:
                strcpy(game_response.error_message, "POWMAX inválido. Não deve ser menor que 1");
                break;
            case -6:
                strcpy(game_response.error_message, "POWMAX deve ser maior ou igual a POWMIN");
                break;
            case -7:
                strcpy(game_response.error_message, "ENGINE inválido. Use 'openmp_mpi' ou 'spark'");
                break;
            default:
                strcpy(game_response.error_message, "Formato inválido. Use: ENGINE:openmp_mpi;POWMIN:3;POWMAX:10");
                break;
        }
    } else {
        printf("Executando: engine=%s, powmin=%d, powmax=%d\n", 
               game_request.engine_type, game_request.powmin, game_request.powmax);
        execute_engine(&game_request, &game_response);
    }

    double total_end_time = wall_time();
    game_response.total_time = total_end_time - total_start_time;
    
    if (strcmp(game_response.status, "SUCCESS") == 0) {
        snprintf(response_buffer, sizeof(response_buffer),
                "REQUEST_ID:%d\n"
                "STATUS:%s\n"
                "ENGINE:%s\n"
                "PROCESSES:%s\n"
                "THREADS:%s\n"
                "EXECUTION_TIME:%.6f\n"
                "TOTAL_TIME:%.6f\n"
                "RESULTS:\n%s\n"
                "END_OF_RESPONSE\n",
                game_response.request_id,
                game_response.status,
                game_response.engine_used,
                (strcmp(game_request.engine_type, "spark") == 0) ? "cluster" : "2",
                (strcmp(game_request.engine_type, "spark") == 0) ? "distributed" : "2",
                game_response.execution_time,
                game_response.total_time,
                game_response.results);
    } else {
        snprintf(response_buffer, sizeof(response_buffer),
                "REQUEST_ID:%d\n"
                "STATUS:%s\n"
                "ERROR:%s\n"
                "END_OF_RESPONSE\n",
                game_response.request_id,
                game_response.status,
                game_response.error_message);
    }
    
    send(client->socket, response_buffer, strlen(response_buffer), 0);
    send_metrics_to_elasticsearch(current_id, client->ip_str, now, &game_response);
    
    close(client->socket);
    free(client);
    
    printf("Cliente %s processado em %.6f segundos\n", client->ip_str, game_response.total_time);
    
    return NULL;
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;
    
    int server_port = PORT;
    int is_local = 0;
    
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            server_port = atoi(argv[i + 1]);
            is_local = 1;
            break;
        }
    }
    
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    
    if (is_local) {
        printf("Game of Life Socket Server LOCAL - Porta %d\n", server_port);
        printf("Uso: ./socket_server -p %d\n", server_port);
    } else {
        printf("Game of Life Socket Server KUBERNETES - Porta %d\n", server_port);
        printf("Executando em modo container/Kubernetes\n");
    }
    
    printf("Engines suportadas: openmp_mpi (2 proc x 2 threads), spark (distribuído)\n");
    printf("Protocolo: ENGINE:tipo;POWMIN:min;POWMAX:max\n");
    printf("ElasticSearch: %s/%s\n", ELASTICSEARCH_URL, INDEX_NAME);
    fflush(stdout);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro no bind");
        printf("💡 Dica: Se porta %d estiver em uso, tente: ./socket_server -p %d\n", server_port, server_port + 1000);
        close(server_socket);
        exit(1);
    }
    
    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Erro no listen");
        close(server_socket);
        exit(1);
    }
    
    if (is_local) {
        printf("Servidor LOCAL aguardando conexões na porta %d...\n", server_port);
        printf("Teste: ./test_client localhost -p %d -e openmp_mpi -min 3 -max 5\n", server_port);
    } else {
        printf("Servidor KUBERNETES aguardando conexões na porta %d...\n", server_port);
        printf("Teste: ./test_client -e openmp_mpi -min 3 -max 5\n");
        printf("Teste: ./test_client -e spark -min 3 -max 5\n");
    }
    fflush(stdout);
    
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Erro ao aceitar conexão");
            continue;
        }
        
        client_info_t* client = malloc(sizeof(client_info_t));
        client->socket = client_socket;
        client->address = client_addr;
        inet_ntop(AF_INET, &client_addr.sin_addr, client->ip_str, INET_ADDRSTRLEN);
        
        if (pthread_create(&thread_id, NULL, handle_client, client) != 0) {
            perror("Erro ao criar thread");
            close(client_socket);
            free(client);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    close(server_socket);
    curl_global_cleanup();
    
    return 0;
}
EOF

echo "✅ Socket server corrigido aplicado!"

# --- PASSO 3: Criar o Cluster Kubernetes com Kind ---
echo "📦 Criando o cluster Kubernetes local 'gameoflife-cluster-optimized'..."
kind create cluster --config=src/kubernetes/kind-cluster-config.yaml
kubectl create namespace gameoflife

# --- PASSO 4: Construir a Imagem Docker ---
echo "🐳 Construindo a imagem Docker '${IMAGE_NAME}'..."
docker build --no-cache -t "${IMAGE_NAME}" -f src/core/Dockerfile src/core/

# --- PASSO 5: Carregar a Imagem no Cluster ---
echo "🚚 Carregando a imagem Docker para dentro do cluster Kind..."
kind load docker-image "${IMAGE_NAME}" --name gameoflife-cluster-optimized

# --- PASSO 6: Atualizar e Aplicar os Arquivos de Deploy ---
echo "📄 Atualizando o arquivo de deploy para usar a nova tag e aplicando no cluster..."

# Atualizar tag da imagem no arquivo de configuração
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
kubectl logs deployment/socket-server -n gameoflife --tail=20

echo -e "\n\n--- 🚀 EXECUTANDO TESTES ---"

echo "--- Testando engine OpenMP+MPI ---"
./binarios/test_client -e openmp_mpi -min 3 -max 5

echo -e "\n--- Testando engine Spark (CORRIGIDA) ---"
./binarios/test_client -e spark -min 3 -max 5

# --- Limpeza Final ---
rm src/kubernetes/socket-server.yaml.bak 2>/dev/null || echo ""

echo -e "\n🎉 Processo concluído!"
echo "🌟 Ambas as engines (OpenMP+MPI e Spark) agora devem funcionar!"
echo ""
echo "📊 Monitoramento disponível em:"
echo "- ElasticSearch: http://localhost:30200"
echo "- Kibana: http://localhost:31502"
echo ""
echo "🧪 Para mais testes:"
echo "./test_both_engines.sh"