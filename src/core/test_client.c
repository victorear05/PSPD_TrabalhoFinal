#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

void print_usage(const char* program_name) {
    printf("Uso: %s [servidor] [opções]\n", program_name);
    printf("Opções OBRIGATÓRIAS:\n");
    printf("  -e <engine>   Engine: openmp_mpi ou spark\n");
    printf("  -min <num>    POWMIN: tamanho mínimo 2^min (1-20)\n");
    printf("  -max <num>    POWMAX: tamanho máximo 2^max (1-20)\n");
    printf("Opções de ambiente:\n");
    printf("  -p <porta>    Conectar em localhost:<porta> (modo LOCAL)\n");
    printf("                Sem -p: conecta no Kubernetes (porta 30080)\n");
    printf("  -h            Mostra esta ajuda\n");
    printf("\nExemplos:\n");
    printf("  # Modo Kubernetes (padrão):\n");
    printf("  %s -e openmp_mpi -min 3 -max 8\n", program_name);
    printf("\n  # Modo LOCAL:\n");
    printf("  %s -p 8080 -e openmp_mpi -min 3 -max 8\n", program_name);
    printf("  %s localhost -p 9000 -e openmp_mpi -min 4 -max 6\n", program_name);
    printf("\n  # Teste Spark:\n");
    printf("  %s -e spark -min 3 -max 5     # Kubernetes\n", program_name);
    printf("  %s -p 8080 -e spark -min 3 -max 5  # Local\n", program_name);
    printf("\nNOTA: Engine híbrida sempre usa 2 processos MPI x 2 threads OpenMP\n");
}

int main(int argc, char* argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char request[512];
    char* server_ip = "127.0.0.1";
    int server_port = 30080;
    int is_local = 0;

    char engine[32] = "";
    int powmin = -1;
    int powmax = -1;
    
    int has_engine = 0, has_powmin = 0, has_powmax = 0;

    int i = 1;

    if (argc > 1 && strcmp(argv[1], "-h") != 0 && argv[1][0] != '-') {
        server_ip = argv[1];
        i = 2;
    }
    
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            server_port = atoi(argv[i + 1]);
            is_local = 1;
            i += 2;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            strncpy(engine, argv[i + 1], sizeof(engine) - 1);
            has_engine = 1;
            i += 2;
        } else if (strcmp(argv[i], "-min") == 0 && i + 1 < argc) {
            powmin = atoi(argv[i + 1]);
            has_powmin = 1;
            i += 2;
        } else if (strcmp(argv[i], "-max") == 0 && i + 1 < argc) {
            powmax = atoi(argv[i + 1]);
            has_powmax = 1;
            i += 2;
        } else {
            printf("Argumento desconhecido: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (!has_engine || !has_powmin || !has_powmax) {
        printf("ERRO: Parâmetros obrigatórios faltando!\n\n");
        
        if (!has_engine) {
            printf("ENGINE não especificado. Use: -e openmp_mpi ou -e spark\n");
        }
        if (!has_powmin) {
            printf("POWMIN não especificado. Use: -min <número>\n");
        }
        if (!has_powmax) {
            printf("POWMAX não especificado. Use: -max <número>\n");
        }
        
        printf("\n   Exemplo correto: %s localhost -e openmp_mpi -min 3 -max 8\n\n", argv[0]);
        return 1;
    }
    
    if (powmin < 1 || powmin > 20) {
        printf("POWMIN deve estar entre 1 e 20\n");
        return 1;
    }
    if (powmax < 1 || powmax > 20) {
        printf("POWMAX deve estar entre 1 e 20\n");
        return 1;
    }
    if (powmax < powmin) {
        printf("POWMAX deve ser maior ou igual a POWMIN\n");
        return 1;
    }
    if (strcmp(engine, "openmp_mpi") != 0 && strcmp(engine, "spark") != 0) {
        printf("Engine deve ser 'openmp_mpi' ou 'spark'\n");
        return 1;
    }
    
    if (is_local) {
        printf("MODO LOCAL - Conectando ao servidor em %s:%d\n", server_ip, server_port);
        printf("Servidor local: ./socket_server -p %d\n", server_port);
    } else {
        printf("MODO KUBERNETES - Conectando ao cluster na porta %d\n", server_port);
        printf("Para modo local, use: %s -p <porta> -e %s -min %d -max %d\n", argv[0], engine, powmin, powmax);
    }
    
    printf("Configuração:\n");
    printf("Engine: %s\n", engine);
    if (strcmp(engine, "openmp_mpi") == 0) {
        printf("   Paralelização: 2 processos MPI x 2 threads OpenMP = 4 cores\n");
    }
    printf("   POWMIN: %d (tabuleiro %dx%d)\n", powmin, 1 << powmin, 1 << powmin);
    printf("   POWMAX: %d (tabuleiro %dx%d)\n", powmax, 1 << powmax, 1 << powmax);
    printf("────────────────────────────────────────\n");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro ao conectar");
        
        if (is_local) {
            printf("Servidor local não encontrado. Inicie com:\n");
            printf("./socket_server -p %d\n", server_port);
        } else {
            printf("Cluster Kubernetes não encontrado. Verifique:\n");
            printf("kubectl get pods -n gameoflife\n");
            printf("kubectl get svc -n gameoflife\n");
            printf("Ou teste em modo local: %s -p 8080 -e %s -min %d -max %d\n", argv[0], engine, powmin, powmax);
        }
        
        close(sock);
        exit(1);
    }
    
    printf("Conectado! Enviando requisição...\n");
    
    // Montar mensagem no protocolo simplificado
    snprintf(request, sizeof(request), "ENGINE:%s;POWMIN:%d;POWMAX:%d", engine, powmin, powmax);
    
    printf("Enviando: %s\n", request);
    
    // Enviar mensagem
    if (send(sock, request, strlen(request), 0) == -1) {
        perror("Erro ao enviar dados");
        close(sock);
        exit(1);
    }
    
    printf("Aguardando resposta do servidor...\n");
    printf("────────────────────────────────────────\n");
    
    int total_bytes = 0;
    int bytes_received;
    char* response_ptr = buffer;
    int remaining_space = BUFFER_SIZE - 1;
    
    while (remaining_space > 0) {
        bytes_received = recv(sock, response_ptr, remaining_space, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        total_bytes += bytes_received;
        response_ptr += bytes_received;
        remaining_space -= bytes_received;
        
        buffer[total_bytes] = '\0';
        if (strstr(buffer, "END_OF_RESPONSE") != NULL) {
            break;
        }
    }
    
    if (total_bytes > 0) {
        buffer[total_bytes] = '\0';
        printf("Resposta do servidor:\n");
        printf("%s", buffer);
        
        if (strstr(buffer, "STATUS:SUCCESS") != NULL) {
            printf("Execução concluída com sucesso!\n");
            
            char* time_ptr = strstr(buffer, "EXECUTION_TIME:");
            if (time_ptr) {
                double exec_time;
                sscanf(time_ptr, "EXECUTION_TIME:%lf", &exec_time);
                printf("Tempo de execução: %.3f segundos\n", exec_time);
            }
        } else if (strstr(buffer, "STATUS:ERROR") != NULL) {
            printf("Erro na execução - veja detalhes acima\n");
        }
        
    } else {
        printf("Não foi possível receber resposta do servidor\n");
        printf("Servidor pode estar sobrecarregado ou ter travado\n");
    }
    
    close(sock);
    printf("Conexão encerrada\n");
    
    return 0;
}