#include "mr_internal.h"

typedef struct { 
    int mapper_write_fd;    //file descriptor della pipe di write del mapper
    int mapper_write_open; //se la pipe di write del mapper è stata aperta
    int reducer_read_fd; //file descriptor della pipe di read del reducer
    int reducer_read_open; //se la pipe di read del reducer è stata aperta
    int output_fd; //file descriptor del file di output
    record_from_reducer_t *records; //array di record prodotti dal reducer
    size_t n_records; //numero di record prodotti dal reducer
    pid_t pid_mapper, pid_reducer; //pid dei processi mapper e reducer
    int children_forked; //se i processi mapper e reducer sono stati forkati

} mr_start_state_t;

//funzione per il cleanup di mr_start
int mr_start_cleanup(mr_t mr, mr_start_state_t *st, int saved_errno);

//funzione per il fail di un processo figlio
void mr_child_fail(mr_log_t *log, const char *proc, const char *msg);  /* log + _exit(1) */