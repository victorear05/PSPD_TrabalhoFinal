// test_client.c - Cliente para testar Game of Life Server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

void print_usage(const char* program_name) {
    printf("Uso: %s [servidor] [opções]\n", program_name);
    printf("Opções:\n");
    printf("  -e <engine>   Engine type: openmp ou spark (padrão: openmp)\n");
    printf("  -min <num>    POWMIN: tamanho mínimo 2^min (padrão: 3)\n");
    printf("  -max <num>    POWMAX: tamanho máximo 2^max (padrão: 10)\n");
    printf("  -t <num>      Número de threads (padrão: 4)\n");
    printf("  -h            Mostra esta ajuda\n");
    printf("\nExemplos:\n");
    printf("  %s localhost                           # Teste básico\n", program_name);
    printf("  %s localhost -e openmp -min 3 -max 8   # OpenMP, tabuleiros 8x8 até 256x256\n", program_name);
    printf("  %s localhost -e spark -t 8             # Spark com 8 threads\n", program_name);
}

int main(int argc, char* argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char request[512];
    char* server_ip = "127.0.0.1";
    int server_port = 30080;
    
    // Parâmetros padrão
    char engine[32] = "openmp";
    int powmin = 3;
    int powmax = 10;
    int num_threads = 6;
    
    // Processar argumentos da linha de comando
    int i = 1;
    if (argc > 1 && strcmp(argv[1], "-h") != 0 && argv[1][0] != '-') {
        server_ip = argv[1];
        i = 2;
    }
    
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            strncpy(engine, argv[i + 1], sizeof(engine) - 1);
            i += 2;
        } else if (strcmp(argv[i], "-min") == 0 && i + 1 < argc) {
            powmin = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-max") == 0 && i + 1 < argc) {
            powmax = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[i + 1]);
            i += 2;
        } else {
            printf("Argumento desconhecido: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Validar parâmetros
    if (powmin < 1 || powmin > 15) {
        printf("POWMIN deve estar entre 1 e 15\n");
        return 1;
    }
    if (powmax < powmin || powmax > 15) {
        printf("POWMAX deve estar entre POWMIN e 15\n");
        return 1;
    }
    if (num_threads < 1 || num_threads > 32) {
        printf("Número de threads deve estar entre 1 e 32\n");
        return 1;
    }
    if (strcmp(engine, "openmp") != 0 && strcmp(engine, "spark") != 0) {
        printf("Engine deve ser 'openmp' ou 'spark'\n");
        return 1;
    }
    
    printf("🔌 Conectando ao Game of Life Server %s:%d\n", server_ip, server_port);
    printf("🎮 Configuração:\n");
    printf("   Engine: %s\n", engine);
    printf("   POWMIN: %d (tabuleiro %dx%d)\n", powmin, 1 << powmin, 1 << powmin);
    printf("   POWMAX: %d (tabuleiro %dx%d)\n", powmax, 1 << powmax, 1 << powmax);
    printf("   Threads: %d\n", num_threads);
    printf("────────────────────────────────────────\n");
    
    // Criar socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }
    
    // Configurar endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    // Conectar
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro ao conectar");
        close(sock);
        exit(1);
    }
    
    printf("✅ Conectado! Enviando requisição...\n");
    
    // Montar mensagem no formato esperado pelo servidor
    snprintf(request, sizeof(request), "ENGINE:%s;POWMIN:%d;POWMAX:%d;THREADS:%d", 
             engine, powmin, powmax, num_threads);
    
    printf("📤 Enviando: %s\n", request);
    
    // Enviar mensagem
    if (send(sock, request, strlen(request), 0) == -1) {
        perror("Erro ao enviar dados");
        close(sock);
        exit(1);
    }
    
    printf("⏳ Aguardando resposta do servidor...\n");
    printf("────────────────────────────────────────\n");
    
    // Receber resposta
    int total_bytes = 0;
    int bytes_received;
    char* response_ptr = buffer;
    int remaining_space = BUFFER_SIZE - 1;
    
    // Ler resposta até encontrar END_OF_RESPONSE
    while (remaining_space > 0) {
        bytes_received = recv(sock, response_ptr, remaining_space, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        total_bytes += bytes_received;
        response_ptr += bytes_received;
        remaining_space -= bytes_received;
        
        // Verificar se recebemos o final da resposta
        buffer[total_bytes] = '\0';
        if (strstr(buffer, "END_OF_RESPONSE") != NULL) {
            break;
        }
    }
    
    if (total_bytes > 0) {
        buffer[total_bytes] = '\0';
        printf("📨 Resposta do servidor:\n");
        printf("%s", buffer);
    } else {
        printf("❌ Não foi possível receber resposta do servidor\n");
    }
    
    close(sock);
    printf("🔚 Conexão encerrada\n");
    
    return 0;
}