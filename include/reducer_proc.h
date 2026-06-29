#include "io.h"
#include "mr.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <threads.h>
#include <stdio.h>

typedef struct {
    char *token;    //token di riferimento del gruppo
    void **data;    //array di puntatori ad ogni coppia del token
    size_t *sizes;  //lunghezza di ogni valore dell'array byte
    size_t count;   //n di elementi assocciati al token
    size_t cap;     //grandezza max array, utile per la riallocazione dinamica
} token_group_t;


//struttura dati di contesto
typedef struct {
    mr_reducer_t reducer;
    void *user_arg;         //argomenti del reducer
    size_t n_workers;       /* mr->attr.reducer_threads */
    mr_log_t *log;          //file di log
    mtx_t out_mtx;          //mtx in scrittura su stdout
    int error;
    /* struttura raggruppamento + lista risultati */
    token_group_t *groups;
    size_t groups_len;
} reducer_ctx_t;

//struttura dati per il thread da passare a reducer_worker_main
typedef struct {
    reducer_ctx_t *ctx;
    token_group_t *g;
} reducer_work_t;


int reducer_process_main(mr_t mr);