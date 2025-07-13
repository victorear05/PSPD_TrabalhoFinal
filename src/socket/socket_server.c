// socket_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

// Estrutura para dados do cliente
typedef struct {
    int socket;
    struct sockaddr_in address;
    char ip_str[INET_ADDRSTRLEN];
} client_info_t;

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

// Função para enviar dados para ElasticSearch
int send_to_elasticsearch(int request_id, const char* client_ip, time_t timestamp) {
    CURL* curl;
    CURLcode res;
    struct elasticsearch_response response = {0};
    
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
    json_object* id_obj = json_object_new_int(request_id);
    json_object* ip_obj = json_object_new_string(client_ip);
    json_object* timestamp_obj = json_object_new_int64(timestamp);
    json_object* iso_timestamp = json_object_new_string(iso_buffer);  // Criar já com valor correto
    
    json_object_object_add(doc, "request_id", id_obj);
    json_object_object_add(doc, "client_ip", ip_obj);
    json_object_object_add(doc, "timestamp", timestamp_obj);
    json_object_object_add(doc, "@timestamp", iso_timestamp);
    json_object_object_add(doc, "server", json_object_new_string("socket-server"));
    json_object_object_add(doc, "status", json_object_new_string("success"));
    
    const char* json_string = json_object_to_json_string(doc);
    
    // Montar URL do ElasticSearch
    char url[512];
    snprintf(url, sizeof(url), "%s/%s/_doc/%d", ELASTICSEARCH_URL, INDEX_NAME, request_id);
    
    // Configurar CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
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
        
        if (response.data) {
            printf("ElasticSearch body: %s\n", response.data);
        }
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    json_object_put(doc);
    
    if (response.data) {
        free(response.data);
    }
    
    return (res == CURLE_OK) ? 0 : -1;
}

// Thread function para lidar com cada cliente
void* handle_client(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    
    // Obter timestamp
    time_t now = time(NULL);
    
    // Incrementar contador thread-safe
    pthread_mutex_lock(&counter_mutex);
    int current_id = request_counter++;
    pthread_mutex_unlock(&counter_mutex);
    
    printf("Cliente conectado: %s, ID: %d\n", client->ip_str, current_id);
    
    // Ler dados do cliente (opcional)
    int bytes_read = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Cliente %s enviou: %s\n", client->ip_str, buffer);
    }
    
    // Preparar resposta
    char response[256];
    snprintf(response, sizeof(response), 
             "REQUEST_ID:%d\nTIMESTAMP:%ld\nCLIENT_IP:%s\nSTATUS:SUCCESS\n", 
             current_id, now, client->ip_str);
    
    // Enviar resposta para cliente
    send(client->socket, response, strlen(response), 0);
    
    // Enviar dados para ElasticSearch
    int elastic_result = send_to_elasticsearch(current_id, client->ip_str, now);
    if (elastic_result == 0) {
        printf("Dados enviados para ElasticSearch com sucesso (ID: %d)\n", current_id);
    } else {
        printf("Erro ao enviar dados para ElasticSearch (ID: %d)\n", current_id);
    }
    
    // Fechar conexão
    close(client->socket);
    free(client);
    
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;
    
    printf("🚀 Iniciando Socket Server na porta %d\n", PORT);
    printf("📊 ElasticSearch URL: %s\n", ELASTICSEARCH_URL);
    printf("📋 Index: %s\n", INDEX_NAME);
    
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