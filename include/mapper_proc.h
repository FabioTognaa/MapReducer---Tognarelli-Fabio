#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "io.h"
#include "queue.h"
#include "mr.h"

//struct di contesto per tutto il mapper
typedef struct{
    mr_mapper_t mapper;    //funzione mapper
    void *user_arg;         //argomenti passati a mapper
    size_t n_workers;       //n di workers per il mapper
    mr_queue_t queue;       //coda delle righe
    mtx_t out_mtx;          //mtx per scrivere e gesstire serializzazione in out
    mr_log_t *log;          //file di log per salvare logs
    int error;              //caso ci sia errore
} mapper_ctx_t;



//rinomino la struct della riga passata in input per comodità
typedef mr_file_line_t mapper_line_t;

int mapper_process_main(mr_t mr);