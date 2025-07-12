#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define SERVIDOR_IP "172.17.0.1"
#define SERVIDOR_PORTA 8080
#define BUFFER_SIZE 1024
#define MAX_ENGINES 10

// Estrutura para dados de cada thread cliente (CORRIGIDA)
typedef struct {
    int cliente_id;
    int num_requisicoes;
    int num_engines;
    double tempo_total;
    int requisicoes_ok;
    int requisicoes_erro;
    char engines[MAX_ENGINES][16];
} thread_cliente_t;

// Estatísticas globais
typedef struct {
    double tempo_min;
    double tempo_max;
    double tempo_total;
    int total_requisicoes;
    int total_ok;
    int total_erro;
    pthread_mutex_t mutex;
} estatisticas_t;

estatisticas_t stats_globais = {
    .tempo_min = 999999.0,
    .tempo_max = 0.0,
    .tempo_total = 0.0,
    .total_requisicoes = 0,
    .total_ok = 0,
    .total_erro = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

double wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

// Função para enviar uma requisição e medir tempo
double enviar_requisicao(int socket_fd, const char* engine, int powmin, int powmax) {
    char buffer[BUFFER_SIZE];
    double t0, t1;
    
    // Preparar requisição
    sprintf(buffer, "ENGINE=%s,POWMIN=%d,POWMAX=%d", engine, powmin, powmax);
    
    t0 = wall_time();
    
    // Enviar requisição
    if (send(socket_fd, buffer, strlen(buffer), 0) < 0) {
        printf("❌ Erro ao enviar requisição\n");
        return -1.0;
    }
    
    // Receber resposta
    memset(buffer, 0, BUFFER_SIZE);
    if (recv(socket_fd, buffer, BUFFER_SIZE - 1, 0) < 0) {
        printf("❌ Erro ao receber resposta\n");
        return -1.0;
    }
    
    t1 = wall_time();
    
    // Verificar se a resposta indica sucesso
    if (strstr(buffer, "STATUS=OK")) {
        return t1 - t0;
    } else {
        printf("❌ Resposta de erro: %s\n", buffer);
        return -1.0;
    }
}

// Thread que simula um cliente
void* thread_cliente(void* arg) {
    thread_cliente_t* dados = (thread_cliente_t*)arg;
    int socket_fd;
    struct sockaddr_in endereco_servidor;
    double t0, t1, tempo_requisicao;
    
    printf("🔄 Cliente %d iniciado - %d requisições\n", 
           dados->cliente_id, dados->num_requisicoes);
    
    // Criar socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        printf("❌ Cliente %d: erro ao criar socket\n", dados->cliente_id);
        return NULL;
    }
    
    // Configurar endereço do servidor
    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_port = htons(SERVIDOR_PORTA);
    inet_pton(AF_INET, SERVIDOR_IP, &endereco_servidor.sin_addr);
    
    // Conectar ao servidor
    if (connect(socket_fd, (struct sockaddr*)&endereco_servidor, sizeof(endereco_servidor)) < 0) {
        printf("❌ Cliente %d: erro ao conectar\n", dados->cliente_id);
        close(socket_fd);
        return NULL;
    }
    
    printf("✅ Cliente %d conectado\n", dados->cliente_id);
    
    t0 = wall_time();
    
    // Enviar requisições
    for (int i = 0; i < dados->num_requisicoes; i++) {
        // Escolher engine de forma rotativa
        const char* engine = dados->engines[i % dados->num_engines];
        
        // Variar parâmetros para teste
        int powmin = 3 + (i % 3);      // 3, 4, 5
        int powmax = powmin + 2 + (i % 3); // powmin+2 até powmin+4
        
        printf("📨 Cliente %d req %d: %s (pow %d-%d)\n", 
               dados->cliente_id, i+1, engine, powmin, powmax);
        
        tempo_requisicao = enviar_requisicao(socket_fd, engine, powmin, powmax);
        
        if (tempo_requisicao > 0) {
            dados->requisicoes_ok++;
            printf("✅ Cliente %d req %d: %.3fs\n", 
                   dados->cliente_id, i+1, tempo_requisicao);
        } else {
            dados->requisicoes_erro++;
            printf("❌ Cliente %d req %d: ERRO\n", dados->cliente_id, i+1);
        }
        
        // Pequeno delay entre requisições
        usleep(100000); // 0.1 segundo
    }
    
    t1 = wall_time();
    dados->tempo_total = t1 - t0;
    
    // Enviar comando de desconexão
    send(socket_fd, "QUIT", 4, 0);
    
    // Receber confirmação
    char buffer[BUFFER_SIZE];
    recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
    
    close(socket_fd);
    
    printf("🏁 Cliente %d finalizado: %.3fs total, %d OK, %d ERRO\n",
           dados->cliente_id, dados->tempo_total, dados->requisicoes_ok, dados->requisicoes_erro);
    
    // Atualizar estatísticas globais
    pthread_mutex_lock(&stats_globais.mutex);
    
    if (dados->tempo_total < stats_globais.tempo_min) {
        stats_globais.tempo_min = dados->tempo_total;
    }
    if (dados->tempo_total > stats_globais.tempo_max) {
        stats_globais.tempo_max = dados->tempo_total;
    }
    
    stats_globais.tempo_total += dados->tempo_total;
    stats_globais.total_requisicoes += dados->num_requisicoes;
    stats_globais.total_ok += dados->requisicoes_ok;
    stats_globais.total_erro += dados->requisicoes_erro;
    
    pthread_mutex_unlock(&stats_globais.mutex);
    
    return NULL;
}

// Teste simples - um cliente, uma requisição
void teste_simples() {
    printf("\n🔬 TESTE SIMPLES\n");
    printf("═══════════════\n");
    
    thread_cliente_t dados;
    dados.cliente_id = 1;
    dados.num_requisicoes = 1;
    strcpy(dados.engines[0], "mpi");
    dados.num_engines = 1;
    dados.tempo_total = 0.0;
    dados.requisicoes_ok = 0;
    dados.requisicoes_erro = 0;
    
    pthread_t thread;
    pthread_create(&thread, NULL, thread_cliente, &dados);
    pthread_join(thread, NULL);
    
    printf("✅ Teste simples concluído\n");
}

// Teste de performance - um cliente, múltiplas requisições
void teste_performance(int num_requisicoes) {
    printf("\n⚡ TESTE DE PERFORMANCE\n");
    printf("═══════════════════════\n");
    printf("Requisições por cliente: %d\n", num_requisicoes);
    
    thread_cliente_t dados;
    dados.cliente_id = 1;
    dados.num_requisicoes = num_requisicoes;
    strcpy(dados.engines[0], "serial");
    strcpy(dados.engines[1], "openmp");
    strcpy(dados.engines[2], "mpi");
    strcpy(dados.engines[3], "spark");
    dados.num_engines = 4;
    dados.tempo_total = 0.0;
    dados.requisicoes_ok = 0;
    dados.requisicoes_erro = 0;
    
    pthread_t thread;
    pthread_create(&thread, NULL, thread_cliente, &dados);
    pthread_join(thread, NULL);
    
    printf("✅ Teste de performance concluído\n");
    printf("📊 Throughput: %.2f req/s\n", dados.num_requisicoes / dados.tempo_total);
}

// Teste de stress - múltiplos clientes simultâneos
void teste_stress(int num_clientes, int req_por_cliente) {
    printf("\n🔥 TESTE DE STRESS\n");
    printf("══════════════════\n");
    printf("Clientes simultâneos: %d\n", num_clientes);
    printf("Requisições por cliente: %d\n", req_por_cliente);
    printf("Total de requisições: %d\n", num_clientes * req_por_cliente);
    
    pthread_t threads[num_clientes];
    thread_cliente_t dados[num_clientes];
    
    double t0 = wall_time();
    
    // Criar threads clientes
    for (int i = 0; i < num_clientes; i++) {
        dados[i].cliente_id = i + 1;
        dados[i].num_requisicoes = req_por_cliente;
        
        // Distribuir engines
        strcpy(dados[i].engines[0], "openmp");
        strcpy(dados[i].engines[1], "mpi");
        dados[i].num_engines = 2; // Usar só os mais rápidos no stress test
        
        dados[i].tempo_total = 0.0;
        dados[i].requisicoes_ok = 0;
        dados[i].requisicoes_erro = 0;
        
        pthread_create(&threads[i], NULL, thread_cliente, &dados[i]);
        
        // Pequeno delay para evitar sobrecarga inicial
        usleep(50000); // 0.05 segundo
    }
    
    // Aguardar todas as threads
    for (int i = 0; i < num_clientes; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double t1 = wall_time();
    double tempo_total_teste = t1 - t0;
    
    printf("✅ Teste de stress concluído\n");
    printf("📊 Tempo total: %.3fs\n", tempo_total_teste);
    printf("📊 Throughput geral: %.2f req/s\n", 
           (num_clientes * req_por_cliente) / tempo_total_teste);
}

void imprimir_estatisticas_finais() {
    pthread_mutex_lock(&stats_globais.mutex);
    
    printf("\n📈 ESTATÍSTICAS FINAIS\n");
    printf("══════════════════════\n");
    printf("Total de requisições: %d\n", stats_globais.total_requisicoes);
    if (stats_globais.total_requisicoes > 0) {
        printf("Requisições OK: %d (%.1f%%)\n", 
               stats_globais.total_ok, 
               (100.0 * stats_globais.total_ok) / stats_globais.total_requisicoes);
        printf("Requisições ERRO: %d (%.1f%%)\n", 
               stats_globais.total_erro,
               (100.0 * stats_globais.total_erro) / stats_globais.total_requisicoes);
        printf("Tempo mínimo por cliente: %.3fs\n", stats_globais.tempo_min);
        printf("Tempo máximo por cliente: %.3fs\n", stats_globais.tempo_max);
        printf("Tempo médio por cliente: %.3fs\n", 
               stats_globais.tempo_total / (stats_globais.total_ok > 0 ? stats_globais.total_ok : 1));
    }
    
    pthread_mutex_unlock(&stats_globais.mutex);
}

int main(int argc, char* argv[]) {
    printf("🧪 CLIENTE DE TESTE - Jogo da Vida Distribuído\n");
    printf("🎯 Servidor: %s:%d\n", SERVIDOR_IP, SERVIDOR_PORTA);
    printf("═══════════════════════════════════════════════\n");
    
    if (argc > 1) {
        if (strcmp(argv[1], "simples") == 0) {
            teste_simples();
        }
        else if (strcmp(argv[1], "performance") == 0) {
            int req = (argc > 2) ? atoi(argv[2]) : 8;
            teste_performance(req);
        }
        else if (strcmp(argv[1], "stress") == 0) {
            int clientes = (argc > 2) ? atoi(argv[2]) : 3;
            int req = (argc > 3) ? atoi(argv[3]) : 4;
            teste_stress(clientes, req);
        }
        else {
            printf("❌ Modo inválido. Use: simples, performance, stress\n");
            return 1;
        }
    } else {
        // Executar todos os testes por padrão
        teste_simples();
        sleep(2);
        teste_performance(4);
        sleep(2);
        teste_stress(2, 3);
    }
    
    imprimir_estatisticas_finais();
    
    printf("\n🎯 Teste concluído!\n");
    return 0;
}