
//struttura della coda thrd safe
typedef struct {
    void **items;                   //puntatore della coda
    size_t cap, head, tail, count;  //testa,coda,n elementi attuali
    mtx_t mtx;                      //mutex lock
    cnd_t not_full, not_empty;      //cond variables
    int closed;                     // flag per chiudere la coda 
} mr_queue_t;


int mr_queue_init(mr_queue_t *q, ssize_t cap);
int mr_queue_destroy(mr_queue_t *q);
int mr_queue_push(mr_queue_t *q, void* item);
int mr_queue_pop(mr_queue_t *q, void **item);
int mr_queue_close(mr_queue_t *q);