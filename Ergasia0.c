#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cblas.h>
#include <omp.h>
#include <sys/time.h>

#define BLOCK_SIZE 1000 // Το μέγιστο μέγεθος μπλοκ

// swap συναρτηση
void swap_double(double *a, double *b) {
    double temp = *a; *a = *b; *b = temp;
}
void swap_int(int *a, int *b) {
    int temp = *a; *a = *b; *b = temp;
}

// Για να βρίσκουμε τους K καλύτερους
int partition(double *dist, int *idx, int left, int right) {
    double pivot = dist[right];
    int i = left - 1;
    for (int j = left; j < right; j++) {
        if (dist[j] <= pivot) {
            i++;
            swap_double(&dist[i], &dist[j]);
            swap_int(&idx[i], &idx[j]);
        }
    }
    swap_double(&dist[i + 1], &dist[right]);
    swap_int(&idx[i + 1], &idx[right]);
    return i + 1;
}

void quickselect(double *dist, int *idx, int left, int right, int k) {
    if (left >= right) return;
    int pivot_index = partition(dist, idx, left, right);
    if (k == pivot_index) return;
    else if (k < pivot_index) quickselect(dist, idx, left, pivot_index - 1, k);
    else quickselect(dist, idx, pivot_index + 1, right, k);
}

// χρηση της cblas για μετρηση της αποστασης
void compute_distances(double *C, double *Q, double *D, int N, int M, int d) {
    // D = -2 * C * Q^T
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                N, M, d, -2.0, C, d, Q, d, 0.0, D, M);

    double *sumC = (double *)malloc(N * sizeof(double));
    double *sumQ = (double *)malloc(M * sizeof(double));

    for(int i=0; i<N; i++) {
        sumC[i] = 0;
        for(int j=0; j<d; j++) sumC[i] += C[i*d + j] * C[i*d + j];
    }
    for(int i=0; i<M; i++) {
        sumQ[i] = 0;
        for(int j=0; j<d; j++) sumQ[i] += Q[i*d + j] * Q[i*d + j];
    }

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            double val = D[i * M + j] + sumC[i] + sumQ[j];
            if (val < 0) val = 0; // Αποφυγή αρνητικών λόγω σφαλμάτων κινητής υποδιαστολής
            D[i * M + j] = sqrt(val); 
        }
    }
    free(sumC); free(sumQ);
}

// για ένα  μπλοκ πινακα που είναι μικρο
void exact_knn(double *C, int *global_idx, double *dist_out, int N, int d, int k, int offset) {
    double *D = (double *)malloc(N * N * sizeof(double));
    
    compute_distances(C, C, D, N, N, d); // Υπολογίζουμε C με C αφού corpus == queries

    for (int i = 0; i < N; i++) {
        int *local_idx = (int *)malloc(N * sizeof(int));
        for(int j=0; j<N; j++) local_idx[j] = j;

        // Βρίσκουμε τους K κοντινότερους
        int search_k = (k < N) ? k : N;
        quickselect(&D[i * N], local_idx, 0, N - 1, search_k);

        // Τους αποθηκεύουμε στους κεντρικούς πίνακες
        for (int j = 0; j < search_k; j++) {
            global_idx[(offset + i) * k + j] = local_idx[j] + offset; // Προσθέτουμε το offset για να βρούμε το πραγματικό ID
            dist_out[(offset + i) * k + j] = D[i * N + j];
        }
        free(local_idx);
    }
    free(D);
}

// αναδρομικη συναρτηση
void approx_knn(double *C, int *global_idx, double *dist_out, int N, int d, int k, int offset) {
    // Αν τα δεδομένα χωράνε στη μνήμη, τα τρέχουμε κανονικά
    if (N <= BLOCK_SIZE) {
        exact_knn(C, global_idx, dist_out, N, d, k, offset);
        return;
    }

    // Αλλιώς τα κόβουμε στη μέση
    int half = N / 2;

    // Δίνουμε το πρώτο μισό σε ένα thread
    #pragma omp task shared(C, global_idx, dist_out)
    approx_knn(C, global_idx, dist_out, half, d, k, offset);

    // Δίνουμε το δεύτερο μισό σε άλλο thread
    #pragma omp task shared(C, global_idx, dist_out)
    approx_knn(C + half * d, global_idx, dist_out, N - half, d, k, offset + half);

    // Περιμένουμε να τελειώσουν και τα δύο
    #pragma omp taskwait
}

int main() {
    int N = 20000; // 20.000 σημεία
    int d = 10; // 10 διαστάσεις
    int K = 5; // 5 πιο κοντινούς γείτονες

    printf("Αρχικοποίηση δεδομένων (N=%d, d=%d, K=%d)...\n", N, d, K);
    
    double *C = (double *)malloc(N * d * sizeof(double));
    int *global_idx = (int *)malloc(N * K * sizeof(int));
    double *dist_out = (double *)malloc(N * K * sizeof(double));

    // Γεμίζουμε με τυχαία δεδομένα
    for (int i = 0; i < N * d; i++) {
        C[i] = (double)rand() / RAND_MAX;
    }

    printf("Ξεκινάει ο υπολογισμός KNN με OpenMP...\n");
    
    double start_time = omp_get_wtime();

    // Ξεκινάμε την παράλληλη περιοχή
    #pragma omp parallel
    {
        // Μόνο ΕΝΑ thread ξεκινάει την αναδρομή, και μετά αυτή  δημηουργει tasks για τα άλλα
        #pragma omp single
        {
            approx_knn(C, global_idx, dist_out, N, d, K, 0);
        }
    }

    double end_time = omp_get_wtime();

    printf("Ο χρόνος εκτέλεσης ήταν: %f\n", end_time - start_time);

    free(C);
    free(global_idx);
    free(dist_out);

    return 0;
}
