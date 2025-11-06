#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>       
#include <pthread.h>    

#define DIM 3

//  Variáveis Globais de Sincronização e Log 
pthread_mutex_t barrier_mutex;
pthread_cond_t barrier_cond;
int barrier_counter = 0;
int num_threads_global;
pthread_mutex_t log_mutex; // Mutex para proteger a escrita no arquivo de log
FILE *log_file = NULL;     

//  Estrutura de Dados para Threads 
typedef struct thread_data_t {
    int id;
    int n, k;
    int start_n, end_n;

    int *flips_global_ptr;
    int flips_local;

    // Ponteiros para os dados GLOBAIS
    double *x, *mean, *sum;
    int *cluster, *count;

    // Ponteiros para dados LOCAIS da thread
    double *sum_local;
    int *count_local;

    struct thread_data_t *all_thread_data;

} thread_data_t;


void manual_barrier_wait(int id, int iter, int barrier_num) {
    // Log antes de esperar
    pthread_mutex_lock(&log_mutex);
    if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: \t-- Chegou na Barreira %d --\n", id, iter, barrier_num);
    pthread_mutex_unlock(&log_mutex);

    pthread_mutex_lock(&barrier_mutex);
    barrier_counter++;
    if (barrier_counter == num_threads_global) {
        barrier_counter = 0;
        pthread_cond_broadcast(&barrier_cond);
    } else {
        pthread_cond_wait(&barrier_cond, &barrier_mutex);
    }
    pthread_mutex_unlock(&barrier_mutex);

    // Log depois de passar
    pthread_mutex_lock(&log_mutex);
     if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: \t-- Passou da Barreira %d --\n", id, iter, barrier_num); 
    pthread_mutex_unlock(&log_mutex);
}


/**
 * @brief Função de trabalho (worker) de cada thread.
 */
void *kmeans_worker(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;

    int id = data->id;
    int k = data->k;
    int start_n = data->start_n;
    int end_n = data->end_n;
    double *x = data->x;
    double *mean = data->mean;
    double *sum = data->sum;
    int *cluster = data->cluster;
    int *count = data->count;
    double *sum_local = data->sum_local;
    int *count_local = data->count_local;
    int i, j, c;
    double dmin, dx;
    int color;
    int iter_local = 0;


    // Loop principal (até a convergência)
    while (1) {

        //  1. ETAPA DE ATRIBUIÇÃO 
        pthread_mutex_lock(&log_mutex);
        if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 1 (Atribuicao) Iniciada (Pontos %d a %d)\n", id, iter_local, start_n, end_n - 1);
        pthread_mutex_unlock(&log_mutex);

        data->flips_local = 0;
        for (i = start_n; i < end_n; i++) {
             dmin = -1;
             color = cluster[i];
             for (c = 0; c < k; c++) {
                 dx = 0.0;
                 for (j = 0; j < DIM; j++)
                     dx += (x[i*DIM+j] - mean[c*DIM+j])*(x[i*DIM+j] - mean[c*DIM+j]);
                 if (dx < dmin || dmin == -1) {
                     color = c;
                     dmin = dx;
                 }
             }
             if (cluster[i] != color) {
                 data->flips_local++;
                 cluster[i] = color;
             }
         }


        pthread_mutex_lock(&log_mutex);
        if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 1 (Atribuicao) Concluida. Flips locais: %d\n", id, iter_local, data->flips_local);
        pthread_mutex_unlock(&log_mutex);

        //  BARREIRA 1 
        manual_barrier_wait(id, iter_local, 1);

        //  2. ETAPA DE CONTABILIDADE (Thread 0) 
        if (id == 0) {
            pthread_mutex_lock(&log_mutex);
            if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 2 (Contabilidade) Iniciada...\n", id, iter_local);
            pthread_mutex_unlock(&log_mutex);

            int total_flips = 0;
            for (int t = 0; t < num_threads_global; t++) {
                total_flips += data->all_thread_data[t].flips_local;
            }
            *data->flips_global_ptr = total_flips;

            pthread_mutex_lock(&log_mutex);
            if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 2 (Contabilidade) - Total de Flips: %d\n", id, iter_local, total_flips);
            pthread_mutex_unlock(&log_mutex);

            if (total_flips > 0) {
                 for (c = 0; c < k; c++) {
                     count[c] = 0;
                     for (j = 0; j < DIM; j++) {
                         sum[c * DIM + j] = 0.0;
                     }
                 }
             }
         }

        //  BARREIRA 2 
        manual_barrier_wait(id, iter_local, 2);

        //  3. CHECAGEM DE SAÍDA 
        if (*data->flips_global_ptr == 0) {
            pthread_mutex_lock(&log_mutex);
            if (log_file) fprintf(log_file, "[Thread %d]: CONVERGIU! Saindo do loop.\n", id);
            pthread_mutex_unlock(&log_mutex);
            break;
        }

        //  4. ETAPA DE SOMA LOCAL 
        pthread_mutex_lock(&log_mutex);
        if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 4 (Soma Local) Iniciada...\n", id, iter_local);
        pthread_mutex_unlock(&log_mutex);

        for (c = 0; c < k; c++) {
            count_local[c] = 0;
            for (j = 0; j < DIM; j++) {
                sum_local[c * DIM + j] = 0.0;
            }
        }
        for (i = start_n; i < end_n; i++) {
            c = cluster[i];
            count_local[c]++;
            for (j = 0; j < DIM; j++) {
                sum_local[c*DIM+j] += x[i*DIM+j];
            }
        }

        //  BARREIRA 3 
        manual_barrier_wait(id, iter_local, 3);

        //  5. ETAPA DE REDUÇÃO GLOBAL E MÉDIA (Thread 0) 
        if (id == 0) {
            pthread_mutex_lock(&log_mutex);
            if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 5 (Reducao Global/Media) Iniciada...\n", id, iter_local);
            pthread_mutex_unlock(&log_mutex);

             for (int t = 0; t < num_threads_global; t++) {
                thread_data_t* other_thread = &data->all_thread_data[t];
                for (c = 0; c < k; c++) {
                    if (other_thread->count_local[c] > 0) {
                        count[c] += other_thread->count_local[c];
                        for (j = 0; j < DIM; j++) {
                            sum[c*DIM+j] += other_thread->sum_local[c*DIM+j];
                        }
                    }
                }
            }
            for (c = 0; c < k; c++) {
                if (count[c] > 0) {
                    for (j = 0; j < DIM; j++) {
                        mean[c*DIM+j] = sum[c*DIM+j] / count[c];
                    }
                }
            }


            pthread_mutex_lock(&log_mutex);
            if (log_file) fprintf(log_file, "[Thread %d, Iter %d]: Etapa 5 (Reducao Global/Media) Concluida.\n", id, iter_local);
            pthread_mutex_unlock(&log_mutex);
        }

        //  BARREIRA 4 
        manual_barrier_wait(id, iter_local, 4);

        iter_local++;

    } // Fim do while(1)

    return NULL;
}


//  Main 
int main(int argc, char *argv[]) {
    int i, j, k, n, c;
    double *x, *mean, *sum;
    int *cluster, *count;
    int flips_global;

    clock_t inicio, fim;
    double tempo_total;

    int num_threads;
    pthread_t *threads;
    thread_data_t *thread_data;

    // Use barras duplas \\ ou barras normais / no caminho
    const char *log_file_path = "log1.txt"; // Usei /
    log_file = fopen(log_file_path, "w");
    if (log_file == NULL) {
        // Se falhar ao abrir o log, avisa no console (stderr) e continua sem log
        perror("Aviso: Nao foi possivel criar log.txt, logs serao omitidos");
    }

    inicio = clock();

    //  1. FASE DE SETUP (Leitura + Alocação) 
    scanf("%d", &k);
    scanf("%d", &n);

    x = (double *)malloc(sizeof(double)*DIM*n);
    mean = (double *)malloc(sizeof(double)*DIM*k);
    sum= (double *)malloc(sizeof(double)*DIM*k);
    cluster = (int *)malloc(sizeof(int)*n);
    count = (int *)malloc(sizeof(int)*k);
    for (i = 0; i<n; i++) cluster[i] = 0;
    for (i = 0; i<k; i++) scanf("%lf %lf %lf", mean+i*DIM, mean+i*DIM+1, mean+i*DIM+2);
    for (i = 0; i<n; i++) scanf("%lf %lf %lf", x+i*DIM, x+i*DIM+1, x+i*DIM+2);
    flips_global = n;


    //  2. SETUP DAS THREADS 
    if (argc != 2) {
        if (log_file) fprintf(log_file, "Erro: Numero de threads nao especificado.\n");
        fprintf(stderr, "Erro: Voce deve especificar o numero de threads.\n"); // E pro console
        fprintf(stderr, "Uso: cat input.txt | %s <numero_de_threads> > output.txt\n", argv[0]);
        if (log_file) fclose(log_file);
        return 1;
    }
    num_threads = atoi(argv[1]);

    if (num_threads <= 0) {
        if (log_file) fprintf(log_file, "Erro: Numero de threads invalido (%d).\n", num_threads);
        fprintf(stderr, "Erro: Numero de threads deve ser positivo.\n"); // E pro console
         if (log_file) fclose(log_file);
        return 1;
    }
    num_threads_global = num_threads;

    if (log_file) fprintf(log_file, "Iniciando K-Means com %d threads (Opcao 2: Reducao Local)\n", num_threads);

    threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    thread_data = (thread_data_t *)malloc(num_threads * sizeof(thread_data_t));
    pthread_mutex_init(&log_mutex, NULL);
    pthread_mutex_init(&barrier_mutex, NULL);
    pthread_cond_init(&barrier_cond, NULL);
    barrier_counter = 0;


    //  3. LANÇAMENTO DAS THREADS 
     for (i = 0; i < num_threads; i++) {
        int chunk_size = n / num_threads;
        int start_n = i * chunk_size;
        int end_n = (i == num_threads - 1) ? n : start_n + chunk_size;

        thread_data[i].id = i;
        thread_data[i].k = k;
        thread_data[i].n = n;
        thread_data[i].start_n = start_n;
        thread_data[i].end_n = end_n;
        thread_data[i].flips_global_ptr = &flips_global;
        thread_data[i].flips_local = 0;
        thread_data[i].x = x;
        thread_data[i].mean = mean;
        thread_data[i].sum = sum;
        thread_data[i].cluster = cluster;
        thread_data[i].count = count;
        thread_data[i].all_thread_data = thread_data;
        thread_data[i].sum_local = (double *)malloc(sizeof(double) * k * DIM);
        thread_data[i].count_local = (int *)malloc(sizeof(int) * k);
        if (thread_data[i].sum_local == NULL || thread_data[i].count_local == NULL) {
            fprintf(stderr, "Erro: Falha ao alocar memoria local para a thread %d\n", i);
            if (log_file) fprintf(log_file, "Erro: Falha ao alocar memoria local para a thread %d\n", i);
             if (log_file) fclose(log_file);
            return 1;
        }
        pthread_create(&threads[i], NULL, kmeans_worker, (void *)&thread_data[i]);
    }


    //  4. ESPERA (Join) 
    for (i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }


    //  5. FASE DE ESCRITA (Resultados) 
    for (i = 0; i < k; i++) {
        for (j = 0; j < DIM; j++)
            printf("%5.2f ", mean[i*DIM+j]);
        printf("\n");
    }

    //  6. CÁLCULO E IMPRESSÃO DO TEMPO 
    fim = clock();
    tempo_total = (double)(fim - inicio) / CLOCKS_PER_SEC;

    
    fprintf(stderr, "Tempo de CPU total (Opcao 2): %f segundos\n", tempo_total);
    if (log_file) fprintf(log_file, "Tempo de CPU total (Opcao 2): %f segundos\n", tempo_total);


    //  7. LIMPEZA 
    free(x);
    free(mean);
    free(sum);
    free(cluster);
    free(count);
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&barrier_mutex);
    pthread_cond_destroy(&barrier_cond);
    for (i = 0; i < num_threads; i++) {
        free(thread_data[i].sum_local);
        free(thread_data[i].count_local);
    }
    free(threads);
    free(thread_data);

    
    if (log_file) {
        fclose(log_file);
    }

    return 0;
}