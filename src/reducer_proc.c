#define _GNU_SOURCE
#include "reducer_proc.h"


//ORDINAMENTO ALFANUMERICO DEI TOKEN NELLA LISTA DA PROCESSARE
static int cmp_tkn(const void *a, const void *b){
    const token_group_t *ta = a;
    const token_group_t *tb = b;

    return strcmp(ta->token, tb->token);
}

//AGGIUNGE UNA COPPIA <TOKEN,TOKEN_VALUE> ALL'ARRAY DI TOKEN DOPO LA LETTURA IN INPUT
static int add_or_create_group(token_group_t **groups, size_t *groups_len, const char *token, const void *value, size_t value_size){

    //scorre la lista di gruppi e cerca il token di riferimento
    for(size_t i = 0; i < *groups_len; i++){

        //confronto del token in ingresso con quelli nella lista
        if(strcmp(token, (*groups)[i].token) == 0){
            
            //uso un puntatore alla struct in questione per comodità
            token_group_t *g = &(*groups)[i];

            //inserisce in coda al gruppo del token
            if(g->count == g->cap){

                //eventualmente rialloco memoria per l'array di dati del token e per le relative sizes 
                size_t new_cap = g->cap ? g->cap * 2 : 4;
                void **nd = realloc(g->data, new_cap * sizeof(void *));
                
                size_t *ns = realloc(g->sizes, new_cap * sizeof(size_t));

                //cleanup se qualcosa fallisce
                if(!nd || !ns){
                    for(size_t j = 0; j < *groups_len; j++){
                        //scorro ogni gruppo
                        for(size_t k = 0; k < (*groups)[j].count; k++){
                            free((*groups)[j].data[k]); //elementi dell'array di dati di ogni token
                        }
                        free((*groups)[j].data);        //array di ogni token
                        free((*groups)[j].sizes);       //lunghezze dei dati
                        free((*groups)[j].token);       //token
                    }
                    free(*groups);                      //array di gruppi
                    return -1;
                }

                //aggiornamento dei valori del gruppo
                g->data = nd;
                g->sizes = ns;
                g->cap = new_cap;
            }

            //copio per valore in nuovo dato <token;token_value>
            //se il valore non e' vuoto
            if(value_size != 0){
                //utilizzo una var per passare i riferimenti per valore
                void *copy = malloc(value_size);
                if(!copy){
                    //cleanup se fallisce
                    for(size_t j = 0; j < *groups_len; j++){
                        for(size_t k = 0; k < (*groups)[j].count; k++){
                            free((*groups)[j].data[k]);
                        }
                        free((*groups)[j].data);
                        free((*groups)[j].sizes);
                        free((*groups)[j].token);
                    }
                        free(*groups);
                    return -1;
                }
                //copio per valori i valori in copy
                memcpy(copy, value, value_size);

                //aggiorno il gruppo
                g->data[g->count] = copy;
            }
            else{
                //se il valore e' vuoto
                g->data[g->count] = NULL;
            }

            //aggiorno i restanti valori
            g->sizes[g->count] = value_size;
            g->count++;
            return 0;
        }
    }

    //se non lo trova: crea il gruppo nell'array di token

    //rialloco l'arrray di gruppi per evere uno slot in memoria in piu'
    token_group_t *ng = realloc(*groups, (*groups_len + 1) * sizeof(token_group_t));
    
    //cleanup se fallisce
    if(!ng){
        for(size_t j = 0; j < *groups_len; j++){
            for(size_t k = 0; k < (*groups)[j].count; k++){
            free((*groups)[j].data[k]);
            }
            free((*groups)[j].data);
            free((*groups)[j].sizes);
            free((*groups)[j].token);
        }
        free(*groups);
        return -1;
    }


    //completamento allocazione
    *groups = ng;

    //creo un puntatore per semplicita'
    token_group_t *g = &(*groups)[*groups_len];

    g->token = strdup(token);

    //cleanup se fallisce
    if(!g->token){
        for(size_t j = 0; j < *groups_len; j++){
            for(size_t k = 0; k < (*groups)[j].count; k++){
            free((*groups)[j].data[k]);
            }
            free((*groups)[j].data);
            free((*groups)[j].sizes);
            free((*groups)[j].token);
        }
        free(*groups);
        return -1;
    }

    //inizializzo i valori del nuovo gruppo
    g->cap = 1;
    g->data = malloc(g->cap * sizeof(void*)); 
    g->sizes = malloc(g->cap * sizeof(size_t));

    //cleanup se malloc fallisce
    if(!g->data || !g->sizes){
        for(size_t j = 0; j < *groups_len; j++){
            for(size_t k = 0; k < (*groups)[j].count; k++){
            free((*groups)[j].data[k]);
            }
            free((*groups)[j].data);
            free((*groups)[j].sizes);
            free((*groups)[j].token);
        }
        free(*groups);
        return -1;
    }
    
    //copio per valore il value_size come prima
    if(value_size != 0){
        void *copy = malloc(value_size);

        //cleanup se fallisce
        if(!copy) {
            for(size_t j = 0; j < *groups_len; j++){
                for(size_t k = 0; k < (*groups)[j].count; k++){
                free((*groups)[j].data[k]);
                }
                free((*groups)[j].data);
                free((*groups)[j].sizes);
                free((*groups)[j].token);
            }
            free(*groups);
            return -1;
        }

        memcpy(copy, value, value_size);
        g->data[0] = copy;
    }
    else{
        g->data[0] = NULL;
    }
    g->sizes[0] = value_size;
    g->count = 1;
    (*groups_len)++;

    return 0;
}


//LEGGE IN INPUT LE COPIE <TOKEN,TOKEN_VALUE> IN ARRIVO DAL MAPPER
static int reader_mapper(void *args){
    
    reducer_ctx_t *ctx = args;
    ctx->groups = NULL;
    ctx->groups_len = 0;
    
    
    //in loop finche' ci sono coppie mandate in input
    while(1){
        
        void *in_value = NULL;
        char *in_token = NULL;
        size_t in_value_size = 0;

        //leggo la coppia
        int nr = mr_read_pair(STDIN_FILENO, (char **)&in_token, (void**)&in_value, &in_value_size);
        
        //read fallito
        if(nr == ERROR_SYSTEM){
            fprintf(stderr, "Errore: lettura della coppia in arrivo dal mapper non riuscita");
            return -1;
        }

        //se e' stato raggiunto EOF
        if(nr == EOF_REACHED){
            close(STDIN_FILENO);
            free((void*)in_token);
            free((void*)in_value);
            break;
        }

        //raggruppamento della coppia per token
        int n_aggr;
        if((n_aggr = add_or_create_group(&ctx->groups, &ctx->groups_len, in_token, in_value, in_value_size)) == -1){
            free(in_token);
            free(in_value);
            fprintf(stderr, "Errore: aggiunta della coppia al raggruppamento fallita");
            return -1;
        }

        free((void*)in_token);
        free((void*)in_value);
    }


    //sort finale per ordinare alfanumericamente la lista dei token
    if(ctx->groups_len)
        qsort(ctx->groups, ctx->groups_len, sizeof(token_group_t), cmp_tkn);

   
    return 0;
}

//SCRIVE IN OUTPUT IL RISULTATO CON LA MTX
static int reducer_emit_result(const char *token, const void *result, size_t result_size, void *emit_arg){
    reducer_ctx_t *ctx = emit_arg;

    //lock del mutex
    if(mtx_lock(&ctx->out_mtx) == thrd_error){
        ctx->error = 1;
        fprintf(stderr, "Errore: mtx_lock fallita in scrittura reducer -> main");
        return -1;
    }

    //scrivo in output ogni coppia
    int rc;
    if((rc = mr_write_result(STDOUT_FILENO, (char*)token, (void*)result, result_size, strlen(token))) == ERROR_SYSTEM){
        ctx->error = 1;
        mtx_unlock(&ctx->out_mtx);
        fprintf(stderr, "Errore: mr_write_result recuder -> main");
        return -1;
    }

    //sblocco del mutex e ritorno positivo
    mtx_unlock(&ctx->out_mtx);
    return 0;
}

//FA REDUCER SU UN GRUPPO DI UN TOKEN E CHIAMA LA FUNZIONE PER SCRIVERE IN OUTPUT I RISULTATI
static int reducer_worker_main(void *arg){

    reducer_work_t *work = (reducer_work_t*)arg;

    reducer_ctx_t *ctx = work->ctx;
    token_group_t *g = work->g;

    //creazione array di valori opachi da fornire in input al reducer
    mr_value_t *values = malloc(g->count * sizeof(mr_value_t));

    //cleanup se fallisce
    if(values == NULL){
        fprintf(stderr, "Errore: malloc di values in scrittura reducer -> main");
        for(size_t i = 0; i < g->count; i++){
            free(g->data[i]);
        }

        free(g->data);
        free(g->token);
        free(g->sizes);
        return -1;
    }

    //popolo values
    for(size_t i = 0; i < g->count; i++){
        values[i].data = g->data[i];
        values[i].size = g->sizes[i];
    }

    //chiamo reducer sull'array di valori estrapolati dal token_group
    if(ctx->reducer(g->token, values, g->count,reducer_emit_result, ctx, ctx->user_arg) == -1){
        ctx->error = 1;     //un worker ha avuto un errore
    }

    //cleanup finale
    free(values);
    for(size_t i = 0; i < g->count; i++){
        free(g->data[i]);
    }
    free(g->data);
    free(g->token);
    free(g->sizes);


    return 0;
}


//FUNZIONE DI ORCHESTRAZIONE DELLE OPERAZIONI DEL REDUCER
int reducer_process_main(mr_t mr){

    //popolo il contesto
    reducer_ctx_t ctx;
    ctx.reducer = mr->reducer;
    ctx.user_arg = mr->user_arg;
    ctx.n_workers = mr->attr.reducer_threads;
    ctx.log = &mr->log;
    ctx.error = 0;
    ctx.groups_len = 0;
    ctx.groups = NULL;

    //inizializzo mtx
    if(mtx_init(&ctx.out_mtx, mtx_plain) == thrd_error)
        return -1;


    //leggo con un thread tutte le coppie in entrata
    thrd_t reader;
    int res;
    if(thrd_create(&reader, reader_mapper, &ctx) != thrd_success){
        mtx_destroy(&ctx.out_mtx);
        return -1;
    }

    //attendo che il reader abbia finito di leggere dal main
    if(thrd_join(reader, &res) != thrd_success || res != 0){
        mtx_destroy(&ctx.out_mtx);
        return -1;
    }

    //fase di raggruppamento

    //loop strutturato, per far lavorare tutti gli worker su uno o piu' token, a seconda se il numero di token sia maggiore del numero di thread

    for(size_t start = 0; start < ctx.groups_len; start += ctx.n_workers){

        //variabile che conta il numero di token rimanenti da processare
        size_t batch = ctx.groups_len - start;

        //se batch supera il numero di workers
        if(batch > ctx.n_workers)
            batch = ctx.n_workers;

        //inizializzo i thrds
        thrd_t workers[ctx.n_workers];
        reducer_work_t works[batch];
        
        //creazione degli worker 
        for(size_t i = 0; i < batch; i++){
            //popolo il work da passare alla funzione dello worker
            works[i].ctx = &ctx;
            works[i].g = &ctx.groups[start + i];
            if(thrd_create(&workers[i], reducer_worker_main, &works[i]) != thrd_success)
                return -1;
        }

        //join degli worker
        for(int j = 0; j < batch; j++){
            if(thrd_join(workers[j], &res) != thrd_success || res != 0)
                ctx.error = 1;
        }

    }

    //eventuale cleanup dell'array di gruppi
    if(ctx.groups)
        free(ctx.groups);
    

    //clenup del mutex e chiusura dello std input in entrata dal mapper
    mtx_destroy(&ctx.out_mtx);
    close(STDOUT_FILENO);

    return ctx.error ? -1 : 0;
}