#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>     // Para MPI
#include <omp.h>     // Para OpenMP

#define ind2d(i, j) (i) * (tam + 2) + j
#define POWMIN 3
#define POWMAX 10

double wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

typedef struct {
    int rank;
    int size;
    int start_row;
    int end_row;
    int local_rows;
} MPIInfo;

MPIInfo calcular_distribuicao(int tam, int rank, int size) {
    MPIInfo info;
    info.rank = rank;
    info.size = size;
    
    int base_rows = tam / size;
    int extra_rows = tam % size;
    
    if (rank < extra_rows) {
        info.local_rows = base_rows + 1;
        info.start_row = rank * (base_rows + 1) + 1;
    } else {
        info.local_rows = base_rows;
        info.start_row = rank * base_rows + extra_rows + 1;
    }
    
    info.end_row = info.start_row + info.local_rows - 1;
    return info;
}

void trocar_bordas(int *tabul, MPIInfo *info, int tam) {
    MPI_Status status;
    
    // Comunicação MPI (mesmo da versão anterior que funciona bem)
    if (info->rank < info->size - 1) {
        MPI_Sendrecv(&tabul[info->local_rows * (tam + 2)], tam + 2, MPI_INT, info->rank + 1, 0,
                     &tabul[(info->local_rows + 1) * (tam + 2)], tam + 2, MPI_INT, info->rank + 1, 1,
                     MPI_COMM_WORLD, &status);
    }
    
    if (info->rank > 0) {
        MPI_Sendrecv(&tabul[1 * (tam + 2)], tam + 2, MPI_INT, info->rank - 1, 1,
                     &tabul[0 * (tam + 2)], tam + 2, MPI_INT, info->rank - 1, 0,
                     MPI_COMM_WORLD, &status);
    }
}

// HÍBRIDO: MPI distribui linhas + OpenMP paraleliza dentro de cada processo
void UmaVida_Hibrida(int *tabulIn, int *tabulOut, int tam, MPIInfo *info) {
    int i, j, vizviv;
    
    // OpenMP paraleliza as linhas locais deste processo MPI
    #pragma omp parallel for private(i, j, vizviv) schedule(static)
    for (i = 1; i <= info->local_rows; i++) {
        for (j = 1; j <= tam; j++) {
            // Mesmo cálculo de vizinhos (indexação local)
            vizviv = tabulIn[(i-1) * (tam + 2) + (j-1)] + tabulIn[(i-1) * (tam + 2) + j] +
                     tabulIn[(i-1) * (tam + 2) + (j+1)] + tabulIn[i * (tam + 2) + (j-1)] +
                     tabulIn[i * (tam + 2) + (j+1)] + tabulIn[(i+1) * (tam + 2) + (j-1)] +
                     tabulIn[(i+1) * (tam + 2) + j] + tabulIn[(i+1) * (tam + 2) + (j+1)];
            
            // Aplicar regras do jogo da vida
            if (tabulIn[i * (tam + 2) + j] && vizviv < 2) {
                tabulOut[i * (tam + 2) + j] = 0;
            }
            else if (tabulIn[i * (tam + 2) + j] && vizviv > 3) {
                tabulOut[i * (tam + 2) + j] = 0;
            }
            else if (!tabulIn[i * (tam + 2) + j] && vizviv == 3) {
                tabulOut[i * (tam + 2) + j] = 1;
            }
            else {
                tabulOut[i * (tam + 2) + j] = tabulIn[i * (tam + 2) + j];
            }
        }
    }
}

void InitTabul_MPI(int *tabulIn, int *tabulOut, int tam, MPIInfo *info) {
    int local_size = (info->local_rows + 2) * (tam + 2);
    
    // Limpar tabuleiros
    for (int i = 0; i < local_size; i++) {
        tabulIn[i] = 0;
        tabulOut[i] = 0;
    }
    
    // Inicializar padrão glider (mesmo da versão MPI que funciona)
    if (info->start_row <= 3 && info->end_row >= 1) {
        if (1 >= info->start_row && 1 <= info->end_row) {
            int local_i = 1 - info->start_row + 1;
            tabulIn[local_i * (tam + 2) + 2] = 1;
        }
        
        if (2 >= info->start_row && 2 <= info->end_row) {
            int local_i = 2 - info->start_row + 1;
            tabulIn[local_i * (tam + 2) + 3] = 1;
        }
        
        if (3 >= info->start_row && 3 <= info->end_row) {
            int local_i = 3 - info->start_row + 1;
            tabulIn[local_i * (tam + 2) + 1] = 1;
            tabulIn[local_i * (tam + 2) + 2] = 1;
            tabulIn[local_i * (tam + 2) + 3] = 1;
        }
    }
}

int Correto_MPI(int *tabul, int tam, MPIInfo *info) {
    int local_count = 0;
    int global_count = 0;
    
    // Contar células vivas locais
    for (int i = 1; i <= info->local_rows; i++) {
        for (int j = 1; j <= tam; j++) {
            local_count += tabul[i * (tam + 2) + j];
        }
    }
    
    // Somar todas as contagens
    MPI_Allreduce(&local_count, &global_count, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    
    if (global_count != 5) return 0;
    
    // Verificar posições específicas
    int local_positions_ok = 1;
    
    if (info->end_row >= tam - 2) {
        int expected_positions[5][2] = {
            {tam-2, tam-1}, {tam-1, tam}, {tam, tam-2}, {tam, tam-1}, {tam, tam}
        };
        
        for (int p = 0; p < 5; p++) {
            int global_row = expected_positions[p][0];
            int global_col = expected_positions[p][1];
            
            if (global_row >= info->start_row && global_row <= info->end_row) {
                int local_row = global_row - info->start_row + 1;
                if (tabul[local_row * (tam + 2) + global_col] != 1) {
                    local_positions_ok = 0;
                    break;
                }
            }
        }
    }
    
    int global_positions_ok;
    MPI_Allreduce(&local_positions_ok, &global_positions_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);
    
    return global_positions_ok;
}

int main(int argc, char *argv[]) {
    int pow, i, tam, *tabulIn, *tabulOut;
    double t0, t1, t2, t3;
    MPIInfo info;
    int num_threads;
    
    // Inicializar MPI com suporte a threads
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    
    if (provided < MPI_THREAD_FUNNELED) {
        printf("Aviso: MPI não suporta threads adequadamente\n");
    }
    
    MPI_Comm_rank(MPI_COMM_WORLD, &info.rank);
    MPI_Comm_size(MPI_COMM_WORLD, &info.size);
    
    // Obter informações sobre OpenMP
    #pragma omp parallel
    {
        #pragma omp single
        {
            num_threads = omp_get_num_threads();
        }
    }
    
    // Apenas processo 0 imprime header
    if (info.rank == 0) {
        printf("Executando HÍBRIDO: %d processos MPI x %d threads OpenMP\n", info.size, num_threads);
        printf("================================================================\n");
    }
    
    for (pow = POWMIN; pow <= POWMAX; pow++) {
        tam = 1 << pow;
        
        // Calcular distribuição para este tamanho
        info = calcular_distribuicao(tam, info.rank, info.size);
        
        t0 = wall_time();
        
        // Alocar memória (mesmo da versão MPI)
        int local_size = (info.local_rows + 2) * (tam + 2);
        tabulIn = (int *)malloc(local_size * sizeof(int));
        tabulOut = (int *)malloc(local_size * sizeof(int));
        
        if (!tabulIn || !tabulOut) {
            printf("Erro de alocação no processo %d\n", info.rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        InitTabul_MPI(tabulIn, tabulOut, tam, &info);
        t1 = wall_time();
        
        // Loop principal - HÍBRIDO MPI+OpenMP
        for (i = 0; i < 2 * (tam - 3); i++) {
            trocar_bordas(tabulIn, &info, tam);           // MPI: comunicação
            UmaVida_Hibrida(tabulIn, tabulOut, tam, &info); // OpenMP: computação
            
            trocar_bordas(tabulOut, &info, tam);          // MPI: comunicação  
            UmaVida_Hibrida(tabulOut, tabulIn, tam, &info); // OpenMP: computação
        }
        t2 = wall_time();
        
        int resultado = Correto_MPI(tabulIn, tam, &info);
        t3 = wall_time();
        
        // Apenas processo 0 imprime resultados
        if (info.rank == 0) {
            if (resultado) {
                printf("**Ok, RESULTADO CORRETO**\n");
            } else {
                printf("**Nok, RESULTADO ERRADO**\n");
            }
            printf("tam=%d; procs=%d; threads=%d; total=%d; tempos: init=%7.7f, comp=%7.7f, fim=%7.7f, tot=%7.7f \n", 
                   tam, info.size, num_threads, info.size * num_threads, t1 - t0, t2 - t1, t3 - t2, t3 - t0);
        }
        
        free(tabulIn);
        free(tabulOut);
    }
    
    MPI_Finalize();
    return 0;
}