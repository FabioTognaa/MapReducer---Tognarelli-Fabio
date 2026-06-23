#include "mapper_proc.h"
#include "log.h"




//CONTROLLO ALFANUMERICO SU UNA STRINGA
static int check_str(const char* str){

    //stringa nulla
    if(str == NULL || *str == '\0'){
        fprintf(stderr, "Errore: token in uscita dal mapper vuoto");
        return -1;
    }

    //loop su stringa
    for(int i = 0; str[i] != '\0'; i++){

        //controllo tramite valori ASCII
        if(!(str[i] >= 'a' && str[i] <= 'z' || str[i] >= 'A' && str[i] <= 'Z' || str[i] >= '0' && str[i] <= '9')){
            fprintf(stderr, "Errore: token in uscita dal mapper non valido");
            return -1;
        }
    }
    return 0;
}


//SCRIVE LA COPPIA CREATA SU STDOUT
static int mapper_emit_pair(const char* token, const void* value, size_t value_size, void* emit_arg){

    mapper_ctx_t *ctx = (mapper_ctx_t*)emit_arg;

    //valido il token
    if(check_str(token) == -1){
        ctx->error = 1;
        return -1;
    }

    //prendo il mutex
    if(mtx_lock(&ctx->out_mtx) != thrd_success){
        perror("Errore: mtx_lock in mapper");
        return -1;
    }
    

    //scrive in stdout le informazioni di una coppia
    if((mr_write_pair(STDOUT_FILENO, (char*)token, (void*)value, value_size, strlen(token) )) == ERROR_SYSTEM){
        mtx_unlock(&ctx->out_mtx);
        ctx->error = 1;
        return -1;
    }

    //rilascia il mutex
    mtx_unlock(&ctx->out_mtx);

    return 0;
}


//PRELEVA DALLA CODA E MAPPA OGNI RIGA
static int mapper_worker_main(void *arg){

    mapper_ctx_t *ctx = (mapper_ctx_t*)arg;

    while(1){

        //pop della coda
        mapper_line_t *item;
        int ris_q = mr_queue_pop(&ctx->queue, (void**)&item);
        if(ris_q != 0)
            break;

        //chiama il mapper dal ctx
        if(ctx->mapper(item, mapper_emit_pair, ctx, ctx->user_arg) == -1){
            ctx->error = 1; 
        }
    
        
        //libera file_name e line
        free((void*)item->file_name); 
        free((void*)item->line);
        free((void*)item); 
    }

    return 0;
}

//LEGGE IN STDIN LE LINEE E LE INSERISCE IN CODA
static int reader_main(void *arg){

    mapper_ctx_t *ctx = (mapper_ctx_t*)arg;
    int isRead;
    
    //IN LOOP
    while(1){

        mapper_line_t *line = malloc(sizeof(*line));
        if(line == NULL){
            mr_queue_close(&ctx->queue);
            return -1;
        } 


        //leggo la linea
        isRead = mr_read_line(STDIN_FILENO, line);
        if(isRead == ERROR_SYSTEM){
            free(line);
            mr_queue_close(&ctx->queue);
            return -1;
        }

        //se sono arrivato alla fine del file
        if(isRead == EOF_REACHED){
            free(line);
            break;
        }

        //pusho in coda
        int ris_q = mr_queue_push(&ctx->queue, (mapper_line_t*)line);
        if(ris_q == -1){
            free((void*)line->file_name);
            free((void*)line->line);
            free(line);
            mr_queue_close(&ctx->queue);
            return -1;
        }
    }


    if(mr_queue_close(&ctx->queue) == -1)
        return -1;


    return 0;
}



//FUNZIONE DEL MAPPER DA CHIAMARE NEL SUO PROCESSO
int mapper_process_main(mr_t mr){

    
    //popolo il contesto
    mapper_ctx_t ctx = {0};
    ctx.mapper = mr->mapper;
    ctx.user_arg = mr->user_arg;
    ctx.n_workers = mr->attr.mapper_threads;
    ctx.log = &mr->log;
    ctx.error = 0;

    //creo la coda per il contesto
    if(mr_queue_init(&ctx.queue, (ssize_t)mr->attr.queue_size) == -1)
        return -1;

    //creo il mtx per mapper_emit_pair
    if(mtx_init(&ctx.out_mtx, mtx_plain)== thrd_error){
        mr_queue_destroy(&ctx.queue);
        return -1;
    }


    //thrd per leggere in ingresso
    thrd_t reader;
    if(thrd_create(&reader, reader_main, &ctx) != thrd_success){
        mr_queue_destroy(&ctx.queue);
        mtx_destroy(&ctx.out_mtx);
        return -1;
    }

    //log: creazione thrd reader
    if(mr_log_write(ctx.log, "mapper", (size_t)thrd_current(), "thrd start", "thrd reader created") == -1){

        mr_queue_close(&ctx.queue);
        thrd_join(reader, NULL);
        mr_queue_destroy(&ctx.queue);
        mtx_destroy(&ctx.out_mtx);
        return -1;
    }


    //thrds worker
    thrd_t workers[ctx.n_workers];

    int n_created = 0;
    for(int i = 0; i < ctx.n_workers; i++){

        if(thrd_create(&workers[i], mapper_worker_main, &ctx) != thrd_success){
            mr_queue_close(&ctx.queue);
            thrd_join(reader, NULL);
            for (int j = 0; j < n_created; j++)
                thrd_join(workers[j], NULL);
            mr_queue_destroy(&ctx.queue);
            mtx_destroy(&ctx.out_mtx);
            return -1;
        }

        //log: creazione thrd worker
        if(mr_log_write(ctx.log, "mapper", (size_t)thrd_current(), "thrd start", "thrd worker created") == -1){
            mr_queue_close(&ctx.queue);
            thrd_join(reader, NULL);
            for (int j = 0; j < n_created; j++)
                thrd_join(workers[j], NULL);
            mr_queue_destroy(&ctx.queue);
            mtx_destroy(&ctx.out_mtx);
            return -1;
        }
        n_created++;
    }
    
    int res;
    //prima del reader
    if(thrd_join(reader, &res) != thrd_success || res == -1){
        ctx.error = 1;
        mr_queue_close(&ctx.queue);
    }
    //poi degli workers
    for(int i = 0; i < ctx.n_workers; i++){
        if(thrd_join(workers[i], &res) != thrd_success || res == -1)
            ctx.error = 1;
    }
    mr_queue_destroy(&ctx.queue);
    mtx_destroy(&ctx.out_mtx);
    close(STDOUT_FILENO);

    if(ctx.error){
        fprintf(stderr, "Errore nel processo di mapping");
        return -1;
    }

    return 0;
}