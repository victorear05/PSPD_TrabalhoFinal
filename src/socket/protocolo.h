#ifndef PROTOCOLO_H
#define PROTOCOLO_H

// Configurações do servidor
#define PORTA_SERVIDOR 8080
#define MAX_CLIENTES 10
#define BUFFER_SIZE 1024
#define MAX_CMD_SIZE 512

// Engines disponíveis
#define ENGINE_SERIAL "serial"
#define ENGINE_OPENMP "openmp"
#define ENGINE_MPI "mpi"
#define ENGINE_SPARK "spark"

// Limites dos parâmetros
#define POWMIN_MIN 3
#define POWMIN_MAX 15
#define POWMAX_MIN 3
#define POWMAX_MAX 15

// Status de resposta
#define STATUS_OK "OK"
#define STATUS_ERRO "ERRO"
#define STATUS_ENGINE_UNKNOWN "ENGINE_UNKNOWN"
#define STATUS_PARAMS_INVALID "PARAMS_INVALID"

// Comandos especiais
#define CMD_QUIT "QUIT"
#define CMD_STATS "STATS"
#define RESP_BYE "BYE"

/**
 * PROTOCOLO DE COMUNICAÇÃO
 * ========================
 * 
 * REQUISIÇÃO do cliente:
 * ENGINE={engine},POWMIN={min},POWMAX={max}
 * 
 * Exemplos:
 * - ENGINE=mpi,POWMIN=3,POWMAX=8
 * - ENGINE=openmp,POWMIN=4,POWMAX=6
 * - ENGINE=spark,POWMIN=3,POWMAX=10
 * 
 * RESPOSTA do servidor:
 * STATUS={status},TIME={tempo},ENGINE={engine},RESULTS={resultados}
 * 
 * Exemplos:
 * - STATUS=OK,TIME=8.123456,ENGINE=mpi,RESULTS=OK;OK;OK;OK;OK;OK;
 * - STATUS=ERRO,TIME=0.000000,ENGINE=invalid,RESULTS=ENGINE_UNKNOWN
 * 
 * COMANDOS ESPECIAIS:
 * - QUIT: desconectar cliente
 * - STATS: obter estatísticas do servidor (implementação futura)
 */

// Estrutura para requisição parseada
typedef struct {
    char engine[32];
    int powmin;
    int powmax;
} requisicao_t;

// Estrutura para resposta formatada
typedef struct {
    char status[32];
    double tempo_total;
    char engine[32];
    char resultados[512];
} resposta_t;

// Estrutura para dados do cliente (usado nas threads)
typedef struct {
    int socket_cliente;
    struct sockaddr_in endereco_cliente;
    int cliente_id;
} cliente_data_t;

// Estrutura para estatísticas do servidor
typedef struct {
    int total_requisicoes;
    int clientes_ativos;
    int requisicoes_ok;
    int requisicoes_erro;
    double tempo_total_processamento;
    double throughput_medio;
} estatisticas_servidor_t;

// Funções de utilidade do protocolo (podem ser implementadas em protocolo.c)
int parse_requisicao(const char* buffer, requisicao_t* req);
void formatar_resposta(const resposta_t* resp, char* buffer);
int validar_engine(const char* engine);
int validar_parametros(int powmin, int powmax);

// Códigos de erro
enum {
    PROTOCOLO_OK = 0,
    PROTOCOLO_ERRO_FORMAT = -1,
    PROTOCOLO_ERRO_ENGINE = -2,
    PROTOCOLO_ERRO_PARAMS = -3,
    PROTOCOLO_ERRO_NETWORK = -4
};

#endif // PROTOCOLO_H