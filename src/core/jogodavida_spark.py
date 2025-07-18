import sys
import time
from pyspark.sql import SparkSession

def get_neighbors(cell):
    """
    Para uma célula (linha, coluna), gera as coordenadas de seus 8 vizinhos.
    """
    r, c = cell
    neighbors = []
    for i in range(r - 1, r + 2):
        for j in range(c - 1, c + 2):
            if (i, j) != (r, c):
                neighbors.append(((i, j), 1))
    return neighbors

def apply_rules(cell_data):
    """
    Aplica as regras do Jogo da Vida.
    """
    coord, (is_alive, num_neighbors) = cell_data
    is_alive = is_alive is not None
    num_neighbors = num_neighbors or 0

    if is_alive and (num_neighbors < 2 or num_neighbors > 3):
        return False
    if not is_alive and num_neighbors == 3:
        return True
    return is_alive

def run_game_of_life(spark, pow_val):
    tam = 1 << pow_val
    num_generations = 2 * (tam - 3)
    t0 = time.time()

    initial_glider = [(1, 2), (2, 3), (3, 1), (3, 2), (3, 3)]
    live_cells_rdd = spark.sparkContext.parallelize(initial_glider).map(lambda c: (c, 1))
    live_cells_rdd.cache()
    t1 = time.time()

    for gen in range(num_generations):
        neighbor_counts_rdd = live_cells_rdd.flatMap(lambda cell: get_neighbors(cell[0])) \
                                            .reduceByKey(lambda a, b: a + b)
        
        board_state_rdd = live_cells_rdd.cogroup(neighbor_counts_rdd)
        
        live_cells_rdd = board_state_rdd.filter(apply_rules) \
                                        .map(lambda data: (data[0], 1))
        live_cells_rdd.cache()
    
    t2 = time.time()
    final_live_cells = {cell[0] for cell in live_cells_rdd.collect()}

    expected_final_cells = {
        (tam - 2, tam - 1), (tam - 1, tam), (tam, tam - 2),
        (tam, tam - 1), (tam, tam)
    }
    
    is_correct = (len(final_live_cells) == 5 and final_live_cells == expected_final_cells)
    t3 = time.time()
    
    if is_correct:
        print("**Ok, RESULTADO CORRETO**")
    else:
        print("**Nok, RESULTADO ERRADO**")
    
    print(f"tam={tam}; tempos: init={t1 - t0:7.7f}, comp={t2 - t1:7.7f}, fim={t3 - t2:7.7f}, tot={t3 - t0:7.7f}")

def main(powmin, powmax):
    spark = SparkSession.builder.appName(f"GameOfLife_Spark_P{powmin}_P{powmax}").getOrCreate()
    sc = spark.sparkContext
    print("================================================================")
    print(f"Spark App-Name: {sc.appName}")
    print(f"Spark Version: {sc.version}")
    print(f"Executando Jogo da Vida com Spark para POWMIN={powmin}, POWMAX={powmax}")
    print("================================================================")
    
    for pow_val in range(int(powmin), int(powmax) + 1):
        run_game_of_life(spark, pow_val)
    
    spark.stop()

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Uso: jogodavida_spark.py <powmin> <powmax>", file=sys.stderr)
        sys.exit(-1)
    main(sys.argv[1], sys.argv[2])