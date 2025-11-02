# Paralelização do K-Means em C com PThreads

Este repositório contém as implementações sequencial e concorrente (paralela) do algoritmo de clusterização K-Means, desenvolvidas em C. O objetivo deste projeto é analisar e comparar o desempenho (Aceleração e Eficiência) da versão paralela, que utiliza PThreads, em relação à versão sequencial de referência.

O algoritmo processa um conjunto de $N$ pontos de dados em 3 dimensões, agrupando-os em $K$ clusters.

## Conteúdo do Repositório

Este projeto é dividido em 5 arquivos principais:

* **`geninput.py`**
    * Um script em Python 3 para gerar os dados de entrada. Ele cria um arquivo de texto formatado com $K$ centróides iniciais ("chutes") e $N$ pontos de dados aleatórios.

* **`kmeans_sequencial.c`**
    * A implementação de referência (gabarito) do K-Means, executada em uma única thread.

* **`kmeans_concorrente.c`**
    * A implementação paralela (Opção 2: Redução Local) do K-Means, que utiliza **PThreads** e um mecanismo de barreira manual (com mutex e variáveis de condição) para sincronização.

* **`kmeans_sequencial_log.c`**
    * Uma versão de depuração do código sequencial. Além de executar o algoritmo, ela redireciona `stderr` para um arquivo `logseq.txt`, salvando o tempo de execução e logs detalhados de cada iteração.

* **`kmeans_concorrente_log.c`**
    * Uma versão de depuração do código concorrente. Ela redireciona `stderr` para `log.txt`, salvando o tempo de execução e um log detalhado que mostra o trabalho de cada thread em cada etapa (Atribuição, Sincronização, Soma Local, Redução Global).

## Como Compilar e Executar

O programa lê os dados da entrada padrão (`stdin`) e imprime os centróides finais na saída padrão (`stdout`). Os logs (nas versões `_log`) são enviados para a saída de erro (`stderr`).

### 1. Gerar Dados de Entrada

Use o script Python para gerar um arquivo `input.txt`. O formato é: `python geninput.py <K_clusters> <N_pontos>`.

```bash
# Exemplo: 50 clusters e 1.000.000 de pontos
python geninput.py 50 1000000 > input.txt
```

### 2. Compilar os Programas

Você precisará do `gcc` e da biblioteca `pthreads`.

```bash
# Compilar a versão Sequencial
gcc kmeans_sequencial.c -o sequencial.exe -O3 -lm

# Compilar a versão Concorrente
# (As flags -lpthread e -mconsole são necessárias no MinGW/Windows)
gcc kmeans_concorrente.c -o concorrente.exe -O3 -lm -lpthread -mconsole
```
*(Compile as versões `_log` da mesma forma, se necessário).*

### 3. Executar e Medir o Desempenho

Use o `cat` e o *pipe* (`|`) para enviar o `input.txt` ao programa e redirecione a saída (`>`) para um arquivo de resultados.

**Execução Sequencial:**
```bash
cat input.txt | ./sequencial.exe > output_seq.txt
```

**Execução Concorrente:**
O programa concorrente espera um argumento: o número de threads (T).

```bash
# Exemplo de execução com 4 threads
cat input.txt | ./concorrente.exe 4 > output_conc.txt

# Exemplo de execução com 8 threads
cat input.txt | ./concorrente.exe 8 > output_conc.txt
```

##  Estratégia de Paralelização (Opção 2: Redução Local)

A versão concorrente (`kmeans_concorrente.c`) é otimizada para minimizar a contenção e os gargalos seriais, seguindo a Lei de Amdahl.

* **Etapa de Atribuição ($O(N \cdot K)$):** Totalmente paralelizada. Cada thread calcula as distâncias para sua própria fatia de $N$ pontos, sem qualquer conflito de escrita.
* **Etapa de Atualização ($O(N)$):** Paralelizada usando **Redução Local**:
    * **Soma Local (Paralela):** Cada thread acumula as somas e contagens em seus próprios arrays `sum_local` e `count_local`. Esta etapa é 100% paralela e não usa mutexes.
    * **Redução Global (Serial):** A Thread 0 (mestre) agrega os $T$ arrays locais nos arrays `sum` e `count` globais. Este é o novo gargalo serial, mas é muito rápido ($O(T \cdot K)$).
* **Sincronização:** O código usa 4 barreiras manuais (implementadas com `pthread_mutex_t` e `pthread_cond_t`) para garantir que as fases de Atribuição, Contabilidade, Soma Local e Redução Global sejam executadas na ordem correta.
