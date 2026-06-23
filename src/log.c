
#include "io.h"

//creazione e gestione di un file di log per il monitoraggio del mapreducer


//funzione per calcolare il timestamp del sistema, da inserire poi nella riga di log 
static void log_timestamp(char *buf, size_t buflen){
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);   // ottieni ora attuale 
    localtime_r(&ts.tv_sec, &tm);         // converte in data/ora locale (thread-safe)
    strftime(buf, buflen, LOG_TS_FMT, &tm); // formatta in stringa 
}

//creazione del file di log e del semforo per gestirne la scrittura
int mr_create_log(mr_log_t *log, char *file_name){

    //puntatore nullo
    if(log == NULL){
        perror("Errore: dati del file di log  in mr_create_log vuoti");
        return -1;
    }

    //nome di default
    if(file_name == NULL)
        file_name = "mr.log";
    
    //apertura del file log
    log->fd = open(file_name, O_WRONLY | O_CREAT | O_APPEND, 0644);
    
    //apre il file di log con il nome scelto
    if(log->fd < 0){
        perror("Errore: open su log file");
        return -1;
    }

    //creo la stringa per sem_open
    snprintf(log->sem_name, sizeof(log->sem_name), "/%d", getpid());

    //inizializzo il semaforo
    if((log->sem = sem_open(log->sem_name, O_CREAT, 0644, 1)) == SEM_FAILED){
        close(log->fd);
        perror("Errore: sem_open del file log");
        return -1;
    }

    return log->fd;
}


//formatta e scrive una riga del file di log con i parametri necessari in input
int mr_log_write( mr_log_t *log, const char *process_name, size_t thrd_id, const char *event_name, const char *event_message){

    //puntatore nullo
    if(log == NULL){
        perror("Errore: dati del file di log  in mr_create_log vuoti");
        return -1;
    }

    char ts[20];
    char line[1024];

    if(!event_message)
        event_message = "";

    //calcolo il timestamp e lo scrivo in ts
    log_timestamp(ts, sizeof(ts));

    sem_wait(log->sem);
    //funzione per scrivere su un file la linea formattata efficacemente in modo automatico
    int len = snprintf(line, sizeof(line), "[%s] [%s] [%zu] [%s] %s\n", ts, process_name, thrd_id, event_name, event_message);

    if((size_t)len >= sizeof(line)){
        sem_post(log->sem);
        perror("Errore: linea scritta nel log troppo grande per il buffer");
        return -1;
    }
    if(writen(log->fd, line, (size_t)len) < 0){
        sem_post(log->sem);
        perror("Errore: scrittura su file log");
        return -1;
    }

    sem_post(log->sem);

    return 0;
}

//chiude il file di log
int mr_log_close(mr_log_t *log){
    //puntatore nullo
    if(log == NULL){
        perror("Errore: dati del file di log  in mr_create_log vuoti");
        return -1;
    }
    
    close(log->fd);
    sem_close(log->sem);
    sem_unlink(log->sem_name);

    return 0;
}


/*
EVENTI DA LOGGARE:
- Creazione della pipe
- fork per mapper e reducer
- avvio e terminazione di ogni thread
- apertura e chiusura dei file di input ed output
- conteggio delle righe inviate dal main al mapper
- conteggio delle coppie create
- token distinti
- eventuali errori
*/