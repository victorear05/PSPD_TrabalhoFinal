import sys
import time
from pyspark.sql import SparkSession

def wall_time():
    return time.time()

def ind2d(i, j, tam):
    return i * (tam + 2) + j

def UmaVida(tabulIn, tabulOut, tam):
    for i in range(1, tam + 1):
        for j in range(1, tam + 1):
            vizviv = (tabulIn[ind2d(i-1, j-1, tam)] + tabulIn[ind2d(i-1, j, tam)] +
                     tabulIn[ind2d(i-1, j+1, tam)] + tabulIn[ind2d(i, j-1, tam)] +
                     tabulIn[ind2d(i, j+1, tam)] + tabulIn[ind2d(i+1, j-1, tam)] +
                     tabulIn[ind2d(i+1, j, tam)] + tabulIn[ind2d(i+1, j+1, tam)])

            if tabulIn[ind2d(i, j, tam)] and vizviv < 2:
                tabulOut[ind2d(i, j, tam)] = 0
            elif tabulIn[ind2d(i, j, tam)] and vizviv > 3:
                tabulOut[ind2d(i, j, tam)] = 0
            elif not tabulIn[ind2d(i, j, tam)] and vizviv == 3:
                tabulOut[ind2d(i, j, tam)] = 1
            else:
                tabulOut[ind2d(i, j, tam)] = tabulIn[ind2d(i, j, tam)]

def InitTabul(tabulIn, tabulOut, tam):
    for ij in range((tam + 2) * (tam + 2)):
        tabulIn[ij] = 0
        tabulOut[ij] = 0
    
    tabulIn[ind2d(1, 2, tam)] = 1
    tabulIn[ind2d(2, 3, tam)] = 1
    tabulIn[ind2d(3, 1, tam)] = 1
    tabulIn[ind2d(3, 2, tam)] = 1
    tabulIn[ind2d(3, 3, tam)] = 1

def Correto(tabul, tam):
    cnt = 0
    
    for ij in range((tam + 2) * (tam + 2)):
        cnt = cnt + tabul[ij]
    
    return (cnt == 5 and tabul[ind2d(tam-2, tam-1, tam)] and
            tabul[ind2d(tam-1, tam, tam)] and tabul[ind2d(tam, tam-2, tam)] and
            tabul[ind2d(tam, tam-1, tam)] and tabul[ind2d(tam, tam, tam)])

def main(powmin, powmax):
    spark = SparkSession.builder \
        .appName("JogoDaVidaSparkDireto") \
        .master("local[*]") \
        .getOrCreate()
    spark.sparkContext.setLogLevel("ERROR")
    
    print("Executando Jogo da Vida Spark")
    print("==========================================================")
    
    try:
        for pow in range(powmin, powmax + 1):
            tam = 1 << pow

            t0 = wall_time()
            tabulIn = [0] * ((tam + 2) * (tam + 2))
            tabulOut = [0] * ((tam + 2) * (tam + 2))
            InitTabul(tabulIn, tabulOut, tam)
            t1 = wall_time()
            
            for i in range(2 * (tam - 3)):
                UmaVida(tabulIn, tabulOut, tam)
                UmaVida(tabulOut, tabulIn, tam)
            
            t2 = wall_time()
            
            if Correto(tabulIn, tam):
                print("**Ok, RESULTADO CORRETO**")
            else:
                print("**Nok, RESULTADO ERRADO**")
            
            t3 = wall_time()
            
            print(f"tam={tam}; tempos: init={t1-t0:.7f}, comp={t2-t1:.7f}, fim={t3-t2:.7f}, tot={t3-t0:.7f}")
    
    finally:
        spark.stop()

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Uso: jogodavida_spark.py <powmin> <powmax>", file=sys.stderr)
        sys.exit(-1)
    main(int(sys.argv[1]), int(sys.argv[2]))