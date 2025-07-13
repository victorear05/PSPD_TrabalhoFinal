#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

int main(int argc, char* argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char* server_ip = "127.0.0.1";
    int server_port = 30080;
    
    if (argc > 1) {
        server_ip = argv[1];
    }
    
    printf("🔌 Conectando ao servidor %s:%d\n", server_ip, server_port);
    
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
    
    // Enviar mensagem
    char message[] = "Hello from client!";
    send(sock, message, strlen(message), 0);
    
    // Receber resposta
    int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("📨 Resposta do servidor:\n%s\n", buffer);
    }
    
    close(sock);
    printf("🔚 Conexão encerrada\n");
    
    return 0;
}