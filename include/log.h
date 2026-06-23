#ifndef LOG_H
#define LOG_H

#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h> 
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define LOG_TS_FMT "%Y-%m-%d %H:%M:%S"

/*
SCELTE FORMATO DEL LOG:
- formato del timestamp di un log: LOG_TS_FMT

- valori del processo nel quale viene fatto il log: "main", "mapper", "reducer"
- nome evento del log (pipe, fork, thrd_start...): "pipe", "fork", "thrd_start"
- id del thread che fa il log: 0 se main, thrd_current() se mapper o reducer
- messaggio del log: "testo del log in questione"

[2026-06-21 14:30:01] [main] [0] [pipe_create] created 3 pipes
*/

//pacchetto di informazioni globale con fd del file di log, semaforo e nome del semaforo per sem_open in mr_create_log
typedef struct{
    int fd;
    sem_t *sem;
    char sem_name[64];
} mr_log_t;

//creazione del file di log
int mr_create_log(mr_log_t *log, char *file_name);


//formatta e scrive una riga del file di log con i parametri necessari in input
int mr_log_write( mr_log_t *log, const char *process_name, size_t thrd_id, const char *event_name, const char *event_message );

//chiude il file di log
int mr_log_close(mr_log_t *log);

#endif