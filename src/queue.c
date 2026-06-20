#include "io.h"
#include "mr.h"
#include <threads.h>
#include <stdio.h>

//struttura della coda thrd safe
typedef struct {
    void **items;                   //puntatore della coda
    size_t cap, head, tail, count;  //testa,coda,n elementi attuali
    mtx_t mtx;                      //mutex lock
    cnd_t not_full, not_empty;      //cond variables
    int closed;                     // flag per chiudere la coda 
} mr_queue_t;


//inizializza una coda
int mr_queue_init(mr_queue_t *q, ssize_t cap){

    //se la coda non fosse vuota
    if(q->items != NULL){
        printf("Errore: coda passata in input non vuota");
        return -1;
    }

    //setto cap max
    if(cap == 0){
        printf("Errore: capacita' max della coda selezionata non valida");
        return -1;
    }
    //alloco memoria ed inizializzo campi di q
    if(cap == -1)
    q->cap = (size_t)DEFAULT_QUEUE_SIZE;
    else q->cap = (size_t)cap;
    if((q->items = calloc(q->cap, sizeof(void*))) == NULL){
        perror("Errore: allocazione di memoria per una coda fallita");
        return -1;
    };

    
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    
    if(mtx_init(&q->mtx, mtx_plain) != thrd_success){ perror("Errore: mutex");return -1;}
    if(cnd_init(&q->not_full) != thrd_success){ perror("Errore: mutex");return -1;}
    if(cnd_init(&q->not_empty) != thrd_success){ perror("Errore: mutex");return -1;}
    
    q->closed = 0;

    return 0;
}

//elimina una coda
int mr_queue_destroy(mr_queue_t *q){

    //coda vuota
    if(q == NULL) return 0;

    mtx_destroy(&q->mtx);
    cnd_destroy(&q->not_full);
    cnd_destroy(&q->not_empty);
    free(q->items);
    q->items = NULL;

    return 0;
}

//aggiunge un elemento in coda
int mr_queue_push(mr_queue_t *q, void* item){

    //se coda piena attende su empty
    mtx_lock(&q->mtx);

    while(q->count == q->cap && !q->closed){
        cnd_wait(&q->not_full, &q->mtx);
    }

    if(q->closed){
        mtx_unlock(&q->mtx);
        return -1;
    }
    //sez critica
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->cap;
    q->count ++;

    cnd_signal(&q->not_empty);
    mtx_unlock(&q->mtx);

    return 0;
}

//elimina un elemento in coda
int mr_queue_pop(mr_queue_t *q, void **item){

    //se coda piena attende su empty
    mtx_lock(&q->mtx);

    while(q->count == 0 && !q->closed){
        cnd_wait(&q->not_empty, &q->mtx);
    }

    if (q->count == 0) {
        mtx_unlock(&q->mtx);
        printf("coda chiusa");
        return -1;  
    }

    //sez critica
    *item = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count --;

    cnd_signal(&q->not_full);
    mtx_unlock(&q->mtx);

    return 0;
}

//chiude la coda
int mr_queue_close(mr_queue_t *q){
    
    if(q == NULL){ perror("Errore: si vuole chiudere una coda vuota"); return -1;}

    mtx_lock(&q->mtx);
    
    //setta il flag e risveglia gli worker che devono finire di rimuovere le cose in coda
    q->closed = 1;
    cnd_broadcast(&q->not_empty);
    cnd_broadcast(&q->not_full);
    mtx_unlock(&q->mtx);
    return 0;
}