#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>

#define PORTA_SERVIDOR 8080
#define MAX_CLIENTES 10
#define BUFFER_SIZE 1024
#define MAX_CMD_SIZE 512

// Estrutura para dados do cliente
typedef struct {
    int socket_cliente;
    struct sockaddr_in endereco_cliente;
    int cliente_id;
} cliente_data_t;

// Estrutura para requisição
typedef struct {
    char engine[32];
    int powmin;
    int powmax;
} requisicao_t;

// Estrutura para resposta
typedef struct {
    char status[16];
    double tempo_total;
    char engine[32];
    char resultados[512];
} resposta_t;

// Contadores globais para estatísticas
static int total_requisicoes = 0;
static int clientes_ativos = 0;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

double wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

// Função para executar engine e capturar resultado
int executar_engine(const char* engine, int powmin, int powmax, char* resultado, double* tempo) {
    char comando[MAX_CMD_SIZE];
    char temp_file[64];
    FILE* fp;
    double t0, t1;
    
    // Criar arquivo temporário para resultados
    sprintf(temp_file, "/tmp/engine_result_%d.txt", getpid());
    
    t0 = wall_time();
    
    if (strcmp(engine, "serial") == 0) {
        sprintf(comando, "../core/jogodavida > %s 2>&1", temp_file);
    } 
    else if (strcmp(engine, "openmp") == 0) {
        sprintf(comando, "OMP_NUM_THREADS=6 ../core/jogodavida_openmp > %s 2>&1", temp_file);
    }
    else if (strcmp(engine, "mpi") == 0) {
        sprintf(comando, "mpirun --allow-run-as-root -np 6 ../core/jogodavida_mpi > %s 2>&1", temp_file);
    }
    else if (strcmp(engine, "hibrido") == 0) {
        sprintf(comando, "OMP_NUM_THREADS=2 mpirun -n 2 ../core/jogodavida_hibrido > %s 2>&1", temp_file);
    }
    else if (strcmp(engine, "spark") == 0) {
        sprintf(comando, "python3 ../core/jogodavida_spark.py > %s 2>&1", temp_file);
    }
    else {
        strcpy(resultado, "ENGINE_UNKNOWN");
        return -1;
    }
    
    int status = system(comando);
    t1 = wall_time();
    *tempo = t1 - t0;
    
    // Ler resultado do arquivo
    fp = fopen(temp_file, "r");
    if (fp) {
        char linha[256];
        char resultados_temp[512] = "";
        
        while (fgets(linha, sizeof(linha), fp)) {
            if (strstr(linha, "**Ok") || strstr(linha, "**Nok")) {
                if (strstr(linha, "CORRETO")) {
                    strcat(resultados_temp, "OK;");
                } else {
                    strcat(resultados_temp, "ERRO;");
                }
            }
        }
        fclose(fp);
        
        if (strlen(resultados_temp) > 0) {
            strcpy(resultado, resultados_temp);
        } else {
            strcpy(resultado, "SEM_RESULTADOS");
        }
    } else {
        strcpy(resultado, "ERRO_ARQUIVO");
    }
    
    // Limpar arquivo temporário
    unlink(temp_file);
    
    return (status == 0) ? 0 : -1;
}

// Parser da requisição
int parse_requisicao(const char* buffer, requisicao_t* req) {
    char* token;
    char buffer_copia[BUFFER_SIZE];
    strcpy(buffer_copia, buffer);
    
    // Inicializar valores padrão
    strcpy(req->engine, "serial");
    req->powmin = 3;
    req->powmax = 8;
    
    // Formato: ENGINE=mpi,POWMIN=3,POWMAX=8
    token = strtok(buffer_copia, ",");
    while (token != NULL) {
        if (strncmp(token, "ENGINE=", 7) == 0) {
            strcpy(req->engine, token + 7);
        }
        else if (strncmp(token, "POWMIN=", 7) == 0) {
            req->powmin = atoi(token + 7);
        }
        else if (strncmp(token, "POWMAX=", 7) == 0) {
            req->powmax = atoi(token + 7);
        }
        token = strtok(NULL, ",");
    }
    
    // Validar parâmetros
    if (req->powmin < 3 || req->powmin > 15 || 
        req->powmax < req->powmin || req->powmax > 15) {
        return -1;
    }
    
    return 0;
}

// Formatar resposta
void formatar_resposta(const resposta_t* resp, char* buffer) {
    sprintf(buffer, "STATUS=%s,TIME=%.6f,ENGINE=%s,RESULTS=%s\n",
            resp->status, resp->tempo_total, resp->engine, resp->resultados);
}

// Thread para atender cliente
void* atender_cliente(void* arg) {
    cliente_data_t* dados = (cliente_data_t*)arg;
    int socket_cliente = dados->socket_cliente;
    int cliente_id = dados->cliente_id;
    
    char buffer[BUFFER_SIZE];
    requisicao_t req;
    resposta_t resp;
    
    printf("🔗 Cliente %d conectado de %s\n", 
           cliente_id, inet_ntoa(dados->endereco_cliente.sin_addr));
    
    // Incrementar contador de clientes ativos
    pthread_mutex_lock(&stats_mutex);
    clientes_ativos++;
    pthread_mutex_unlock(&stats_mutex);
    
    while (1) {
        // Receber requisição
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_recebidos = recv(socket_cliente, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_recebidos <= 0) {
            printf("❌ Cliente %d desconectado\n", cliente_id);
            break;
        }
        
        // Remover quebra de linha
        buffer[strcspn(buffer, "\r\n")] = 0;
        
        if (strlen(buffer) == 0) continue;
        
        printf("📨 Cliente %d requisição: %s\n", cliente_id, buffer);
        
        // Comando especial para desconectar
        if (strcmp(buffer, "QUIT") == 0) {
            send(socket_cliente, "BYE\n", 4, 0);
            break;
        }
        
        // Parse da requisição
        if (parse_requisicao(buffer, &req) != 0) {
            sprintf(buffer, "STATUS=ERRO,MSG=Formato inválido\n");
            send(socket_cliente, buffer, strlen(buffer), 0);
            continue;
        }
        
        // Executar engine
        double tempo_execucao;
        char resultados[512];
        
        printf("⚙️  Cliente %d executando engine=%s, powmin=%d, powmax=%d\n", cliente_id, req.engine, req.powmin, req.powmax);
        
        int resultado = executar_engine(req.engine, req.powmin, req.powmax, resultados, &tempo_execucao);
        
        // Preparar resposta
        strcpy(resp.engine, req.engine);
        resp.tempo_total = tempo_execucao;
        strcpy(resp.resultados, resultados);
        
        if (resultado == 0) {
            strcpy(resp.status, "OK");
        } else {
            strcpy(resp.status, "ERRO");
        }
        
        // Enviar resposta
        formatar_resposta(&resp, buffer);
        send(socket_cliente, buffer, strlen(buffer), 0);
        
        printf("✅ Cliente %d resposta enviada (%.3fs)\n", cliente_id, tempo_execucao);
        
        // Atualizar estatísticas
        pthread_mutex_lock(&stats_mutex);
        total_requisicoes++;
        pthread_mutex_unlock(&stats_mutex);
    }
    
    // Decrementar contador de clientes ativos
    pthread_mutex_lock(&stats_mutex);
    clientes_ativos--;
    pthread_mutex_unlock(&stats_mutex);
    
    close(socket_cliente);
    free(dados);
    return NULL;
}

// Thread para imprimir estatísticas
void* thread_estatisticas(void* arg) {
    while (1) {
        sleep(10); // A cada 10 segundos
        
        pthread_mutex_lock(&stats_mutex);
        printf("\n📊 ESTATÍSTICAS: Clientes ativos: %d | Total requisições: %d\n", 
               clientes_ativos, total_requisicoes);
        pthread_mutex_unlock(&stats_mutex);
    }
    return NULL;
}

int main() {
    int socket_servidor, socket_cliente;
    struct sockaddr_in endereco_servidor, endereco_cliente;
    socklen_t tamanho_endereco = sizeof(endereco_cliente);
    pthread_t thread_cliente, thread_stats;
    int cliente_contador = 0;
    
    printf("🚀 Iniciando Servidor Socket - Jogo da Vida Distribuído\n");
    printf("📡 Porta: %d\n", PORTA_SERVIDOR);
    printf("👥 Máximo clientes simultâneos: %d\n", MAX_CLIENTES);
    printf("⚙️  Engines disponíveis: serial, openmp, mpi, spark\n");
    printf("════════════════════════════════════════════════════\n");
    
    // Criar socket
    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("❌ Erro ao criar socket");
        exit(1);
    }
    
    // Configurar reutilização de endereço
    int opt = 1;
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configurar endereço do servidor
    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = INADDR_ANY;
    endereco_servidor.sin_port = htons(PORTA_SERVIDOR);
    
    // Bind
    if (bind(socket_servidor, (struct sockaddr*)&endereco_servidor, sizeof(endereco_servidor)) < 0) {
        perror("❌ Erro no bind");
        close(socket_servidor);
        exit(1);
    }
    
    // Listen
    if (listen(socket_servidor, MAX_CLIENTES) < 0) {
        perror("❌ Erro no listen");
        close(socket_servidor);
        exit(1);
    }
    
    printf("✅ Servidor ouvindo na porta %d...\n\n", PORTA_SERVIDOR);
    
    // Criar thread de estatísticas
    pthread_create(&thread_stats, NULL, thread_estatisticas, NULL);
    pthread_detach(thread_stats);
    
    // Loop principal - aceitar conexões
    while (1) {
        socket_cliente = accept(socket_servidor, (struct sockaddr*)&endereco_cliente, &tamanho_endereco);
        
        if (socket_cliente < 0) {
            perror("❌ Erro no accept");
            continue;
        }
        
        // Criar dados para thread do cliente
        cliente_data_t* dados = malloc(sizeof(cliente_data_t));
        dados->socket_cliente = socket_cliente;
        dados->endereco_cliente = endereco_cliente;
        dados->cliente_id = ++cliente_contador;
        
        // Criar thread para atender cliente
        if (pthread_create(&thread_cliente, NULL, atender_cliente, dados) != 0) {
            perror("❌ Erro ao criar thread");
            close(socket_cliente);
            free(dados);
            continue;
        }
        
        // Detach thread para limpeza automática
        pthread_detach(thread_cliente);
    }
    
    close(socket_servidor);
    return 0;
}