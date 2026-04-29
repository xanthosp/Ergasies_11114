#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#define QUEUESIZE 100
#define LOOP 50000 

struct workFunction {
  void * (*work)(void *);
  void * arg;
  struct timeval time_in; // ποτε μπηκε στην ουρα
};

typedef struct {
  struct workFunction buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
} queue;

queue *fifo;

// μεταβλητες για να βρουμε τον μεσο χρονο
long long total_wait = 0;
int count = 0;
pthread_mutex_t time_mutex = PTHREAD_MUTEX_INITIALIZER;

// Η δουλεια που κανει το καθε thread
void *do_work(void *arg) {
    int a = *(int *)arg;
    double s = 0;
    
    for (int i = 0; i < 10; i++) {
        s += sin((a + i) * 3.14159 / 180.0);
    }
    
    free(arg); 
    return NULL;
}

// Producer
void *producer (void *args) {
  for (int i = 0; i < LOOP; i++) {
    struct workFunction task;
    task.work = do_work;
    
    int *arg = malloc(sizeof(int));
    *arg = i;
    task.arg = arg;

    pthread_mutex_lock (fifo->mut);
    while (fifo->full) {
      pthread_cond_wait (fifo->notFull, fifo->mut);
    }

    // κραταμε το χρονο που μπαινει
    gettimeofday(&task.time_in, NULL);

    fifo->buf[fifo->tail] = task;
    fifo->tail++;
    if (fifo->tail == QUEUESIZE) fifo->tail = 0;
    if (fifo->tail == fifo->head) fifo->full = 1;
    fifo->empty = 0;

    pthread_mutex_unlock (fifo->mut);
    pthread_cond_signal (fifo->notEmpty);
  }
  return NULL;
}

// Consumer
void *consumer (void *args) {
  while (1) { 
    struct workFunction task;

    pthread_mutex_lock (fifo->mut);
    while (fifo->empty) {
      pthread_cond_wait (fifo->notEmpty, fifo->mut);
    }

    task = fifo->buf[fifo->head];
    fifo->head++;
    if (fifo->head == QUEUESIZE) fifo->head = 0;
    if (fifo->head == fifo->tail) fifo->empty = 1;
    fifo->full = 0;

    pthread_mutex_unlock (fifo->mut);
    pthread_cond_signal (fifo->notFull);

    // υπολογισμος του ποσο περιμενε
    struct timeval time_out;
    gettimeofday(&time_out, NULL);
    long long wait = (time_out.tv_sec - task.time_in.tv_sec) * 1000000 + (time_out.tv_usec - task.time_in.tv_usec);

    // βαζουμε mutex
    pthread_mutex_lock(&time_mutex);    
    total_wait += wait;
    count++;
    pthread_mutex_unlock(&time_mutex);

    // εκτελεση
    task.work(task.arg);
  }
  return NULL;
}

queue *queueInit (void) {
  queue *q = (queue *)malloc (sizeof (queue));
  if (q == NULL) return (NULL);
  q->empty = 1; q->full = 0; q->head = 0; q->tail = 0;
  q->mut = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (q->mut, NULL);
  q->notFull = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notFull, NULL);
  q->notEmpty = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notEmpty, NULL);
  return (q);
}

int main (int argc, char *argv[]) {
  if (argc != 3) {
    printf("Prepei na dwseis producers kai consumers!\n");
    exit(1);
  }

  int p = atoi(argv[1]); 
  int q = atoi(argv[2]); 

  fifo = queueInit ();
  if (fifo == NULL) {
    printf("Error: h oura den arxikopoihthike\n");
    exit (1);
  }

  pthread_t pro[p], con[q];

  for (int i = 0; i < p; i++) pthread_create (&pro[i], NULL, producer, NULL);
  for (int i = 0; i < q; i++) pthread_create (&con[i], NULL, consumer, NULL);

  for (int i = 0; i < p; i++) pthread_join (pro[i], NULL);

  // δινουμε λιγο χρονο να τελειωσουν τα υπολοιπα tasks
  usleep(1000000); 

  if (count > 0) {
      printf("Total tasks: %d\n", count);
      printf("Mesos xronos anamonhs: %lld us\n", total_wait / count);
  }

  return 0;
}
