#include "io.h"
#include "mr.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <threads.h>
#include <stdio.h>

//struttura dati di contesto
typedef struct {
    mr_reducer_t reducer;
    void *user_arg;         //argomenti del reducer
    size_t n_workers;       /* mr->attr.reducer_threads */
    mr_log_t *log;          //file di log
    mtx_t out_mtx;          //mtx in scrittura su stdout
    int error;
    /* struttura raggruppamento + lista risultati */
} reducer_ctx_t;



typedef struct {
    char *token;    //token di riferimento del gruppo
    void **data;    //array di puntatori ad ogni coppia del token
    size_t *sizes;  //lunghezza di ogni valore dell'array byte
    size_t count;   //n di elementi assocciati al token
    size_t cap;     //grandezza max array, utile per la riallocazione dinamica
} token_group_t;


//ORDINAMENTO ALFANUMERICO DEI TOKEN NELLA LISTA
static int cmp_tkn(const void *a, const void *b){
    const token_group_t *ta = a;
    const token_group_t *tb = b;

    return strcmp(ta->token, tb->token);
}

//AGGIUNGE UNA COPPIA <TOKEN,TOKEN_VALUE> ALL'ARRAY DI TOKEN DOPO LA LETTURA IN INPUT
static int add_or_create_group(token_group_t **groups, size_t *groups_len, const char *token, const void *value, size_t value_size){

    //scorre la lista di gruppi e cerca il token da aggiungere
    for(size_t i = 0; i < *groups_len; i++){

        //confronto del token in ingresso con quelli nella lista
        if(strcmp(token, (*groups)[i].token) == 0){
            
            //uso un puntatore alla struct in questione per comodità
            token_group_t *g = &(*groups)[i];

            //inserisce in coda
            if(g->count == g->cap){

                //eventualmente rialloco memoria per l'array di dati del token e per le relative sizes 
                size_t new_cap = g->cap ? g->cap * 2 : 4;
                void **nd = realloc(g->data, new_cap * sizeof(void *));
                
                size_t *ns = realloc(g->sizes, new_cap * sizeof(size_t));

                //libera mem se qualcosa fallisce
                if(!nd || !ns){
                    for(size_t j = 0; j < *groups_len; j++){
                        //scorro ogni gruppo
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

                g->data = nd;
                g->sizes = ns;
                g->cap = new_cap;
            }

            void *copy = malloc(value_size);
            if(!copy) return -1;
            memcpy(copy, value, value_size);

            g->data[g->count] = copy;
            g->sizes[g->count] = value_size;
            g->count++;
            return 0;
        }
    }

    //se non lo trova
    //crea il gruppo nell'array
    token_group_t *ng = realloc(*groups, (*groups_len + 1) * sizeof(token_group_t));
        
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
    token_group_t *g = &(*groups)[*groups_len];
    g->token = strdup(token);
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
    g->cap = 1;
    g->data = malloc(g->cap * sizeof(void*)); 
    g->sizes = malloc(g->cap * sizeof(size_t));
    //malloc non andata a buon fine
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
    
    void *copy = malloc(value_size);
    if(!copy) return -1;

    memcpy(copy, value, value_size);
    g->data[0] = copy;
    g->sizes[0] = value_size;
    g->count = 1;
    (*groups_len)++;


    return 0;
}


//LEGGE IN INPUT LE COPIE <TOKEN,TOKEN_VALUE>
static int reader_mapper(token_group_t **groups, size_t *groups_len){
    
    *groups = NULL;
    *groups_len = 0;
    
    //in loop
    while(1){
        
        void *in_value = NULL;
        char *in_token = NULL;
        size_t in_value_size = 0;

        //leggo la coppia
        int nr = mr_read_pair(STDIN_FILENO, (char **)&in_token, (void**)&in_value, &in_value_size);
        
        if(nr == ERROR_SYSTEM){
            fprintf(stderr, "Errore: lettura della coppia in arrivo dal mapper non riuscita");
            return -1;
        }

        //se e' stato letto tutto
        if(nr == EOF_REACHED){
            close(STDIN_FILENO);
            free((void*)in_token);
            free((void*)in_value);
            break;
        }

        //raggruppamento della coppia 
        int n_aggr;
        if((n_aggr = add_or_create_group(groups, groups_len, in_token, in_value, in_value_size)) == -1){
            free(in_token);
            free(in_value);
            fprintf(stderr, "Errore: aggiunta della coppia al raggruppamento fallita");
            return -1;
        }

        free((void*)in_token);
        free((void*)in_value);
    }


    //sort finale per ordinare la lista dei token
    qsort(*groups, *groups_len, sizeof(token_group_t), cmp_tkn);

   
    
    return 0;
}


int reducer_process_main(mr_t mr){

    //popolo il contesto
    reducer_ctx_t ctx;
    ctx.reducer = mr->reducer;
    ctx.user_arg = mr->user_arg;
    ctx.n_workers = mr->attr.reducer_threads;
    ctx.log = &mr->log;
    ctx.error = 0;

    //inizializzo mtx
    if(mtx_init(&ctx.out_mtx, mtx_plain) == thrd_error)
        return -1;

    

}
//creazione dei worker thrds per chiamare la reducer


//mr_write_result() con mtx se multithread per scrivere risultati verso il main