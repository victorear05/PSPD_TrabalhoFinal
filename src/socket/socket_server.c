// socket_server.c - Versão com integração Game Engine
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

// Estrutura para dados do cliente
typedef struct {
    int socket;
    struct sockaddr_in address;
    char ip_str[INET_ADDRSTRLEN];
} client_info_t;

// Estrutura para requisição do jogo da vida
typedef struct {
    int powmin;
    int powmax;
    char engine_type[32];  // "openmp" ou "spark"
    int num_threads;
} gameoflife_request_t;

// Estrutura para resposta do jogo da vida
typedef struct {
    int request_id;
    char status[32];
    double execution_time;
    double total_time;
    char engine_used[32];
    int threads_used;
    char results[RESULT_BUFFER_SIZE];
    char error_message[512];
} gameoflife_response_t;

// Contador global de requests (thread-safe)
static int request_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Configuração ElasticSearch
const char* ELASTICSEARCH_URL = "http://elasticsearch:9200";
const char* INDEX_NAME = "gameoflife-requests";

// Estrutura para response do ElasticSearch
struct elasticsearch_response {
    char* data;
    size_t size;
};

// Função para obter timestamp em double (wall_time)
double wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

// Callback para receber resposta do ElasticSearch
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

// Função para enviar dados detalhados para ElasticSearch
int send_detailed_metrics_to_elasticsearch(int request_id, const char* client_ip, 
                                         time_t timestamp, gameoflife_response_t* response) {
    CURL* curl;
    CURLcode res;
    struct elasticsearch_response es_response = {0};
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Erro: falha ao inicializar CURL\n");
        return -1;
    }
    
    // Converter timestamp para ISO format
    struct tm* tm_info = gmtime(&timestamp);
    char iso_buffer[64];
    strftime(iso_buffer, sizeof(iso_buffer), "%Y-%m-%dT%H:%M:%S.000Z", tm_info);
    
    // Montar JSON do documento
    json_object* doc = json_object_new_object();
    json_object_object_add(doc, "request_id", json_object_new_int(request_id));
    json_object_object_add(doc, "client_ip", json_object_new_string(client_ip));
    json_object_object_add(doc, "timestamp", json_object_new_int64(timestamp));
    json_object_object_add(doc, "@timestamp", json_object_new_string(iso_buffer));
    json_object_object_add(doc, "server", json_object_new_string("socket-server"));
    json_object_object_add(doc, "status", json_object_new_string(response->status));
    json_object_object_add(doc, "engine_type", json_object_new_string(response->engine_used));
    json_object_object_add(doc, "threads_used", json_object_new_int(response->threads_used));
    json_object_object_add(doc, "execution_time_seconds", json_object_new_double(response->execution_time));
    json_object_object_add(doc, "total_time_seconds", json_object_new_double(response->total_time));
    
    // Adicionar mensagem de erro se houver
    if (strlen(response->error_message) > 0) {
        json_object_object_add(doc, "error_message", json_object_new_string(response->error_message));
    }
    
    const char* json_string = json_object_to_json_string(doc);
    
    // Montar URL do ElasticSearch
    char url[512];
    snprintf(url, sizeof(url), "%s/%s/_doc/%d", ELASTICSEARCH_URL, INDEX_NAME, request_id);
    
    // Configurar CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &es_response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    // Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Executar request
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("Erro ao enviar para ElasticSearch: %s\n", curl_easy_strerror(res));
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        printf("ElasticSearch response: HTTP %ld\n", response_code);
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    json_object_put(doc);
    
    if (es_response.data) {
        free(es_response.data);
    }
    
    return (res == CURLE_OK) ? 0 : -1;
}

// Função para executar o Game of Life engine
int execute_gameoflife_engine(gameoflife_request_t* request, gameoflife_response_t* response) {
    char command[512];
    char temp_filename[64];
    FILE* temp_file;
    char buffer[256];
    double start_time, end_time;
    
    // Gerar nome de arquivo temporário único
    snprintf(temp_filename, sizeof(temp_filename), "/tmp/gameoflife_output_%d_%ld.txt", 
             response->request_id, time(NULL));
    
    // Determinar comando baseado no engine type
    if (strcmp(request->engine_type, "openmp") == 0) {
        // Engine OpenMP - detectar ambiente
        if (access("/app/jogodavida_openmp", F_OK) == 0) {
            // Estamos no container Docker
            snprintf(command, sizeof(command), 
                    "cd /app && export OMP_NUM_THREADS=%d && ./jogodavida_openmp > %s 2>&1",
                    request->num_threads, temp_filename);
            strcpy(response->engine_used, "openmp-container");
            printf("Executando OpenMP no container: /app/\n");
        } else if (access("binarios/jogodavida_openmp", F_OK) == 0) {
            // Estamos rodando localmente
            char current_dir[256];
            getcwd(current_dir, sizeof(current_dir));
            snprintf(command, sizeof(command), 
                    "cd %s/binarios && export OMP_NUM_THREADS=%d && ./jogodavida_openmp > %s 2>&1",
                    current_dir, request->num_threads, temp_filename);
            strcpy(response->engine_used, "openmp-local");
            printf("Executando OpenMP localmente: %s/binarios/\n", current_dir);
        } else {
            strcpy(response->status, "ERROR");
            snprintf(response->error_message, sizeof(response->error_message), 
                    "OpenMP engine não encontrado nem em /app/ nem em binarios/");
            return -1;
        }
        response->threads_used = request->num_threads;
        
    } else if (strcmp(request->engine_type, "spark") == 0) {

    if (access("/opt/bitnami/spark/bin/spark-submit", F_OK) == 0) {
        snprintf(command, sizeof(command),
                "/opt/bitnami/spark/bin/spark-submit --class JogoDaVidaSpark --master local[%d] /app/target/scala-2.12/jogo-da-vida-spark_2.12-1.0.jar %d %d > %s 2>&1",
                request->num_threads, request->powmin, request->powmax, temp_filename);
        strcpy(response->engine_used, "spark-container");
        printf("Executando Spark no container\n");
    } else {

        snprintf(command, sizeof(command),
                "echo 'Spark engine não encontrado. Parâmetros: threads=%d, powmin=%d, powmax=%d' > %s",
                request->num_threads, request->powmin, request->powmax, temp_filename);
        strcpy(response->engine_used, "spark-placeholder");
        printf("Executando Spark placeholder (spark-submit não encontrado)\n");
    }
    response->threads_used = request->num_threads;

        
    } else {
        strcpy(response->status, "ERROR");
        snprintf(response->error_message, sizeof(response->error_message), 
                "Engine type '%s' não suportado. Use 'openmp' ou 'spark'", request->engine_type);
        return -1;
    }
    
    printf("Executando comando: %s\n", command);
    fflush(stdout);
    
    // Medir tempo de execução
    start_time = wall_time();
    int exit_code = system(command);
    fflush(stdout);
    end_time = wall_time();
    
    response->execution_time = end_time - start_time;
    
    // Verificar se comando foi executado com sucesso
    if (exit_code != 0) {
        strcpy(response->status, "ERROR");
        snprintf(response->error_message, sizeof(response->error_message), 
                "Comando falhou com código de saída: %d", exit_code);
        return -1;
    }
    
    // Ler arquivo de saída
    temp_file = fopen(temp_filename, "r");
    if (temp_file == NULL) {
        strcpy(response->status, "ERROR");
        snprintf(response->error_message, sizeof(response->error_message), 
                "Não foi possível abrir arquivo de resultado: %s", temp_filename);
        return -1;
    }
    
    // Concatenar todas as linhas do resultado
    response->results[0] = '\0';
    while (fgets(buffer, sizeof(buffer), temp_file) != NULL) {
        if (strlen(response->results) + strlen(buffer) < RESULT_BUFFER_SIZE - 1) {
            strcat(response->results, buffer);
        }
    }
    
    fclose(temp_file);
    
    // Remover arquivo temporário
    unlink(temp_filename);
    
    strcpy(response->status, "SUCCESS");
    return 0;
}

// Função para parsear requisição do cliente
int parse_client_request(const char* request_str, gameoflife_request_t* request) {
    // Formato esperado: "ENGINE:openmp;POWMIN:3;POWMAX:10;THREADS:4"
    char* request_copy = strdup(request_str);
    char* token;
    char* saveptr;
    
    // Valores padrão
    strcpy(request->engine_type, "openmp");
    request->powmin = 3;
    request->powmax = 10;
    request->num_threads = 6;
    
    token = strtok_r(request_copy, ";", &saveptr);
    while (token != NULL) {
        char* key_value = token;
        char* colon_pos = strchr(key_value, ':');
        
        if (colon_pos != NULL) {
            *colon_pos = '\0';
            char* key = key_value;
            char* value = colon_pos + 1;
            
            if (strcmp(key, "ENGINE") == 0) {
                strncpy(request->engine_type, value, sizeof(request->engine_type) - 1);
            } else if (strcmp(key, "POWMIN") == 0) {
                request->powmin = atoi(value);
            } else if (strcmp(key, "POWMAX") == 0) {
                request->powmax = atoi(value);
            } else if (strcmp(key, "THREADS") == 0) {
                request->num_threads = atoi(value);
            }
        }
        
        token = strtok_r(NULL, ";", &saveptr);
    }
    
    free(request_copy);
    
    // Validação básica
    if (request->powmin < 1 || request->powmin > 15) request->powmin = 3;
    if (request->powmax < request->powmin || request->powmax > 15) request->powmax = 10;
    if (request->num_threads < 1 || request->num_threads > 32) request->num_threads = 4;
    
    return 0;
}

// Thread function para lidar com cada cliente
void* handle_client(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    gameoflife_request_t game_request;
    gameoflife_response_t game_response;
    char response_buffer[RESPONSE_BUFFER_SIZE];
    
    // Obter timestamp
    time_t now = time(NULL);
    double total_start_time = wall_time();
    
    // Incrementar contador thread-safe
    pthread_mutex_lock(&counter_mutex);
    int current_id = request_counter++;
    pthread_mutex_unlock(&counter_mutex);
    
    // Inicializar resposta
    memset(&game_response, 0, sizeof(game_response));
    game_response.request_id = current_id;
    
    printf("Cliente conectado: %s, ID: %d\n", client->ip_str, current_id);
    
    // Ler dados do cliente
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
    
    // Parsear requisição
    if (parse_client_request(buffer, &game_request) != 0) {
        strcpy(game_response.status, "ERROR");
        strcpy(game_response.error_message, "Formato de requisição inválido");
    } else {
        // Executar Game of Life engine
        printf("Executando Game of Life: engine=%s, powmin=%d, powmax=%d, threads=%d\n",
               game_request.engine_type, game_request.powmin, game_request.powmax, game_request.num_threads);
        
        execute_gameoflife_engine(&game_request, &game_response);
    }
    
    // Calcular tempo total
    double total_end_time = wall_time();
    game_response.total_time = total_end_time - total_start_time;
    
    // Preparar resposta para cliente
    if (strcmp(game_response.status, "SUCCESS") == 0) {
        snprintf(response_buffer, sizeof(response_buffer),
                "REQUEST_ID:%d\n"
                "STATUS:%s\n"
                "ENGINE:%s\n"
                "THREADS:%d\n"
                "EXECUTION_TIME:%.6f\n"
                "TOTAL_TIME:%.6f\n"
                "RESULTS:\n%s\n"
                "END_OF_RESPONSE\n",
                game_response.request_id,
                game_response.status,
                game_response.engine_used,
                game_response.threads_used,
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
    
    // Enviar resposta para cliente
    send(client->socket, response_buffer, strlen(response_buffer), 0);
    
    // Enviar dados para ElasticSearch
    int elastic_result = send_detailed_metrics_to_elasticsearch(current_id, client->ip_str, now, &game_response);
    if (elastic_result == 0) {
        printf("Métricas enviadas para ElasticSearch com sucesso (ID: %d)\n", current_id);
    } else {
        printf("Erro ao enviar métricas para ElasticSearch (ID: %d)\n", current_id);
    }
    
    // Fechar conexão
    close(client->socket);
    free(client);
    
    printf("Cliente %s processado em %.6f segundos\n", client->ip_str, game_response.total_time);
    
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;
    
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    printf("🚀 Iniciando Game of Life Socket Server na porta %d\n", PORT);
    fflush(stdout);
    printf("📊 ElasticSearch URL: %s\n", ELASTICSEARCH_URL);
    printf("📋 Index: %s\n", INDEX_NAME);
    printf("🎮 Engines suportados: openmp, spark\n");
    printf("📝 Protocolo: ENGINE:tipo;POWMIN:min;POWMAX:max;THREADS:num\n");
    fflush(stdout);

    // Inicializar libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Criar socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }
    
    // Permitir reutilizar endereço
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configurar endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro no bind");
        close(server_socket);
        exit(1);
    }
    
    // Listen
    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Erro no listen");
        close(server_socket);
        exit(1);
    }
    
    printf("✅ Servidor aguardando conexões na porta %d...\n", PORT);
    
    // Loop principal
    while (1) {
        // Aceitar conexão
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Erro ao aceitar conexão");
            continue;
        }
        
        // Criar estrutura de dados do cliente
        client_info_t* client = malloc(sizeof(client_info_t));
        client->socket = client_socket;
        client->address = client_addr;
        inet_ntop(AF_INET, &client_addr.sin_addr, client->ip_str, INET_ADDRSTRLEN);
        
        // Criar thread para lidar com cliente
        if (pthread_create(&thread_id, NULL, handle_client, client) != 0) {
            perror("Erro ao criar thread");
            close(client_socket);
            free(client);
            continue;
        }
        
        // Detach thread (não precisamos esperar)
        pthread_detach(thread_id);
    }
    
    // Cleanup (nunca alcançado neste exemplo)
    close(server_socket);
    curl_global_cleanup();
    
    return 0;
}
