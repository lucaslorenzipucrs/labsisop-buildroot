/*
 * sched_profiler - TP3 de Sistemas Operacionais
 *
 * Cria N threads que escrevem a sua letra (A, B, C...) num buffer global,
 * uma posição por vez, protegido por um semáforo binário. No fim o buffer
 * mostra a ordem em que as threads rodaram.
 *
 * uso: ./sched_profiler <tam_buffer> <num_threads> <politica>
 * politica: SCHED_LOW_IDLE, SCHED_IDLE, SCHED_FIFO, SCHED_RR (ou SCHED_OTHER)
 * também dá pra passar uma lista separada por vírgula, uma política por
 * thread (ex: SCHED_RR,SCHED_FIFO)
 *
 * compilar: gcc -O2 -pthread sched_profiler.c -o sched_profiler
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif
/* política criada no tutorial do SCHED_LOW_IDLE (valor 7) */
#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

#define MAX_THREADS   26     /* uma letra por thread, de A até Z */
#define RT_PRIORITY   10     /* prioridade do FIFO e do RR */
#define LINE_WIDTH    64
#define RAW_PRINT_MAX 20000  /* limite pra imprimir o buffer, senão enche o
                                disco do qemu quando o buffer é grande */

static char  *buffer;
static char  *buf_end;
static char  *ptr;           /* ponteiro global, posição atual do buffer */
static sem_t  mutex;         /* semáforo binário */
static sem_t  ready;         /* a thread avisa aqui que já está pronta */

struct thread_arg {
    int       id;
    char      ch;
    int       policy;
    int       err;
    sem_t     start;         /* a main libera a thread por aqui */
    pthread_t tid;
};

static struct thread_arg targs[MAX_THREADS];

struct policy_name { const char *name; int value; };

static const struct policy_name policies[] = {
    { "SCHED_LOW_IDLE", SCHED_LOW_IDLE },
    { "SCHED_IDLE",     SCHED_IDLE     },
    { "SCHED_FIFO",     SCHED_FIFO     },
    { "SCHED_RR",       SCHED_RR       },
    { "SCHED_OTHER",    SCHED_OTHER    },
};
#define NPOLICIES ((int)(sizeof(policies) / sizeof(policies[0])))

/* aceita com ou sem o "SCHED_" na frente */
static int parse_policy(const char *s)
{
    int i;
    for (i = 0; i < NPOLICIES; i++) {
        const char *short_name = policies[i].name + 6;
        if (strcasecmp(s, policies[i].name) == 0 ||
            strcasecmp(s, short_name) == 0)
            return policies[i].value;
    }
    return -1;
}

static const char *policy_str(int policy)
{
    int i;
    for (i = 0; i < NPOLICIES; i++)
        if (policies[i].value == policy)
            return policies[i].name;
    return "?";
}

/* só FIFO e RR usam prioridade, as outras têm que ser 0 */
static int priority_for(int policy)
{
    return (policy == SCHED_FIFO || policy == SCHED_RR) ? RT_PRIORITY : 0;
}

static void *worker(void *arg)
{
    struct thread_arg *t = arg;

    /* avisa a main e espera ela liberar */
    sem_post(&ready);
    sem_wait(&t->start);

    if (t->err != 0)
        return NULL;

    for (;;) {
        sem_wait(&mutex);
        if (ptr >= buf_end) {    /* buffer cheio, acabou */
            sem_post(&mutex);
            break;
        }
        *ptr = t->ch;
        ptr++;
        sem_post(&mutex);
    }
    return NULL;
}

/* imprime quebrando a linha de 64 em 64 caracteres */
static void print_wrapped(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        putchar(s[i]);
        if ((i + 1) % LINE_WIDTH == 0)
            putchar('\n');
    }
    if (n % LINE_WIDTH != 0)
        putchar('\n');
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Uso: %s <tam_buffer> <num_threads> <politica>\n"
        "  politica: SCHED_LOW_IDLE | SCHED_IDLE | SCHED_FIFO | SCHED_RR | SCHED_OTHER\n"
        "            ou lista separada por virgula (uma por thread), ex.: SCHED_RR,SCHED_FIFO\n"
        "Exemplo: %s 10000 4 SCHED_RR\n", prog, prog);
}

int main(int argc, char **argv)
{
    long   size, nthreads;
    int    list[MAX_THREADS], nlist = 0;
    int    i, rc, failed = 0;
    char  *plist, *tok, *save = NULL;
    char  *summary;
    size_t slen = 0;
    long   counts[MAX_THREADS];
    char   last;
    struct sched_param sp;
    int    main_rt = 0;

    if (argc != 4) {
        usage(argv[0]);
        return 1;
    }

    size     = atol(argv[1]);
    nthreads = atol(argv[2]);
    if (size <= 0 || nthreads <= 0 || nthreads > MAX_THREADS) {
        fprintf(stderr, "Erro: tam_buffer > 0 e 1 <= num_threads <= %d\n", MAX_THREADS);
        return 1;
    }

    /* separa a lista de políticas */
    plist = strdup(argv[3]);
    for (tok = strtok_r(plist, ",", &save); tok && nlist < MAX_THREADS;
         tok = strtok_r(NULL, ",", &save)) {
        int p = parse_policy(tok);
        if (p < 0) {
            fprintf(stderr, "Erro: politica desconhecida '%s'\n", tok);
            usage(argv[0]);
            return 1;
        }
        list[nlist++] = p;
    }
    free(plist);
    if (nlist == 0) {
        usage(argv[0]);
        return 1;
    }

    buffer = calloc((size_t)size, 1);
    if (!buffer) {
        perror("calloc");
        return 1;
    }
    ptr     = buffer;
    buf_end = buffer + size;

    sem_init(&mutex, 0, 1);
    sem_init(&ready, 0, 0);

    /* cria as threads, todas ficam esperando a largada */
    for (i = 0; i < nthreads; i++) {
        targs[i].id     = i;
        targs[i].ch     = 'A' + i;
        targs[i].policy = list[i % nlist];
        targs[i].err    = 0;
        sem_init(&targs[i].start, 0, 0);
        rc = pthread_create(&targs[i].tid, NULL, worker, &targs[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            return 1;
        }
    }

    /* espera todas ficarem prontas. o usleep é pra dar tempo delas
       dormirem de verdade no semáforo antes de mexer na política */
    for (i = 0; i < nthreads; i++)
        sem_wait(&ready);
    usleep(20000);

    /* sobe a main pra FIFO com prioridade máxima, assim nenhuma thread
       de tempo real pega a CPU antes de eu terminar de configurar e
       liberar todo mundo. sem root não funciona, mas aí segue normal */
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0)
        main_rt = 1;

    /* define a política de cada thread (igual no exemplo do professor) */
    for (i = 0; i < nthreads; i++) {
        sp.sched_priority = priority_for(targs[i].policy);
        rc = pthread_setschedparam(targs[i].tid, targs[i].policy, &sp);
        if (rc != 0) {
            targs[i].err = rc;
            failed++;
            fprintf(stderr, "Thread %c: pthread_setschedparam(%s) falhou: %s\n",
                    targs[i].ch, policy_str(targs[i].policy), strerror(rc));
            if (rc == EPERM)
                fprintf(stderr, "  -> precisa de root para SCHED_FIFO/SCHED_RR\n");
            else if (rc == EINVAL && targs[i].policy == SCHED_LOW_IDLE)
                fprintf(stderr, "  -> este kernel tem suporte a SCHED_LOW_IDLE (valor 7)?\n");
        }
    }

    /* libera as threads na ordem A, B, C... */
    for (i = 0; i < nthreads; i++)
        sem_post(&targs[i].start);

    for (i = 0; i < nthreads; i++)
        pthread_join(targs[i].tid, NULL);

    if (failed) {
        fprintf(stderr, "Execucao invalida: %d thread(s) sem a politica pedida.\n", failed);
        return 1;
    }

    printf("Parametros: tam_buffer=%ld threads=%ld politica=%s CPU_main_rt=%s\n",
           size, nthreads, argv[3], main_rt ? "sim" : "nao");

    printf("\n--- Buffer (sem pos-processamento) ---\n");
    if (size <= RAW_PRINT_MAX) {
        print_wrapped(buffer, (size_t)size);
    } else {
        print_wrapped(buffer, RAW_PRINT_MAX);
        printf("[... mostrando as primeiras %d de %ld posicoes ...]\n",
               RAW_PRINT_MAX, size);
    }

    /* junta as letras repetidas e conta quantos blocos cada thread teve */
    summary = malloc((size_t)size + 1);
    memset(counts, 0, sizeof(counts));
    last = 0;
    for (i = 0; i < size; i++) {
        char c = buffer[i];
        if (c == 0)
            continue;
        if (c != last) {
            summary[slen++] = c;
            counts[c - 'A']++;
            last = c;
        }
    }
    summary[slen] = '\0';

    printf("\n--- Apos pos-processamento ---\n");
    print_wrapped(summary, slen);
    for (i = 0; i < nthreads; i++)
        printf("%c = %ld\n", targs[i].ch, counts[i]);

    free(summary);
    free(buffer);
    return 0;
}