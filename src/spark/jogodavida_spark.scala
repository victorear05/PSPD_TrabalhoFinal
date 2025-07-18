import org.apache.spark.sql.SparkSession

object JogoDaVidaSpark {

  // Função para calcular a próxima geração de uma única célula
  def computeNextState(currentState: Int, neighbors: Int): Int = {
    if (currentState == 1 && (neighbors < 2 || neighbors > 3)) 0
    else if (currentState == 0 && neighbors == 3) 1
    else currentState
  }

  // Função para calcular o número de vizinhos vivos para uma célula (i, j)
  def countNeighbors(grid: Array[Array[Int]], i: Int, j: Int, tam: Int): Int = {
    var neighbors = 0
    for (y <- i - 1 to i + 1) {
      for (x <- j - 1 to j + 1) {
        if ((y != i || x != j) && y >= 0 && y < tam && x >= 0 && x < tam) {
          neighbors += grid(y)(x)
        }
      }
    }
    neighbors
  }

  def main(args: Array[String]): Unit = {
    if (args.length < 2) {
      println("Uso: Jogo da Vida Spark <POWMIN> <POWMAX>")
      sys.exit(1)
    }

    val powMin = args(0).toInt
    val powMax = args(1).toInt
    
    val spark = SparkSession.builder.appName("Jogo da Vida Spark").getOrCreate()
    val sc = spark.sparkContext
    
    println(s"Executando Spark Engine para POWMIN=$powMin até POWMAX=$powMax")
    println("========================================")

    for (pow <- powMin to powMax) {
      val tam = 1 << pow
      val generations = 2 * (tam - 3)
      
      // Inicializa o tabuleiro (Grid)
      var grid = Array.fill(tam, tam)(0)
      grid(0)(1) = 1 // Padrão para validação
      grid(1)(2) = 1
      grid(2)(0) = 1
      grid(2)(1) = 1
      grid(2)(2) = 1

      val startTime = System.nanoTime()

      // Loop principal de gerações
      for (gen <- 1 to generations) {
        val gridRdd = sc.parallelize(0 until tam)

        // Mapeia cada linha para seu novo estado
        val nextGridRdd = gridRdd.map { i =>
          val newRow = Array.fill(tam)(0)
          for (j <- 0 until tam) {
            val neighbors = countNeighbors(grid, i, j, tam)
            newRow(j) = computeNextState(grid(i)(j), neighbors)
          }
          (i, newRow)
        }
        
        // Coleta os resultados e atualiza o grid principal
        val nextGridMap = nextGridRdd.collectAsMap()
        for ((i, row) <- nextGridMap) {
          grid(i) = row
        }
      }

      val endTime = System.nanoTime()
      val durationSeconds = (endTime - startTime) / 1e9d
      
      // Validação e Impressão de Resultados (similar ao código C)
      val finalLiveCells = grid.map(_.sum).sum
      println(s"tam=$tam; cells=$finalLiveCells; comp=${"%.7f".format(durationSeconds)} s")
    }

    spark.stop()
  }
}
