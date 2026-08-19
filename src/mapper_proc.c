#include "mapper_proc.h"
#include "mr_internal.h"
#include "log.h"


//SETTA L'ERRORE IN MODO SINCRONIZZATO SENZA RACE
static void set_error(mapper_ctx_t *ctx){
    mtx_lock(&ctx->out_mtx);
    ctx->error = 1;
    mtx_unlock(&ctx->out_mtx);
}


//CONTROLLO ALFANUMERICO SUL NOME DI UN TOKEN
static int check_tkn(const char* str, mr_log_t *log){

    //stringa nulla
    if(str == NULL || *str == '\0'){
        mr_log_write(log, "mapper", 0, "error", "token in uscita dal mapper vuoto");
        return -1;
    }

    //loop su stringa
    for(int i = 0; str[i] != '\0'; i++){

        //controllo tramite valori ASCII
        if(!((str[i] >= 'a' && str[i] <= 'z') ||
             (str[i] >= 'A' && str[i] <= 'Z') ||
             (str[i] >= '0' && str[i] <= '9'))){
            mr_log_write(log, "mapper", 0, "error", "token in uscita dal mapper non valido");
            return -1;
        }
    }
    return 0;
}


//SCRIVE LA COPPIA CREATA SU STDOUT
static int mapper_emit_pair(const char* token, const void* value, size_t value_size, void* emit_arg){

    mapper_ctx_t *ctx = (mapper_ctx_t*)emit_arg;

    //valido il token
    if(check_tkn(token, ctx->log) == -1){
        set_error(ctx);
        return -1;
    }

    //prendo il mutex
    if(mtx_lock(&ctx->out_mtx) != thrd_success){
        mr_log_write(ctx->log, "mapper", 0, "error", "acquisizione mtx del mapper in scrittura su reducer fallita");
        set_error(ctx);
        return -1;
    }
    

    //scrive in stdout le informazioni di una coppia
    if((mr_write_pair(STDOUT_FILENO, (char*)token, (void*)value, value_size, strlen(token) )) == ERROR_SYSTEM){
        ctx->error = 1; /* gia' sotto out_mtx: non chiamare set_error */
        mtx_unlock(&ctx->out_mtx);
        return -1;
    }

    ctx->pairs++;

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
            set_error(ctx);
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
    if(mr_log_write(ctx.log, "mapper", 0, "thrd_start", "reader thread created") == -1){

        mr_queue_close(&ctx.queue);
        thrd_join(reader, NULL);
        mr_queue_destroy(&ctx.queue);
        mtx_destroy(&ctx.out_mtx);
        return -1;
    }


    //thrds worker
    thrd_t workers[ctx.n_workers];

    size_t n_created = 0;
    for(size_t i = 0; i < ctx.n_workers; i++){

        if(thrd_create(&workers[i], mapper_worker_main, &ctx) != thrd_success){
            mr_queue_close(&ctx.queue);
            thrd_join(reader, NULL);
            for (size_t j = 0; j < n_created; j++)
                thrd_join(workers[j], NULL);
            mr_queue_destroy(&ctx.queue);
            mtx_destroy(&ctx.out_mtx);
            return -1;
        }
        n_created++;

        //log: creazione thrd worker (id 1..N)
        if(mr_log_write(ctx.log, "mapper", i + 1, "thrd_start", "worker thread created") == -1){
            mr_queue_close(&ctx.queue);
            thrd_join(reader, NULL);
            for (size_t j = 0; j < n_created; j++)
                thrd_join(workers[j], NULL);
            mr_queue_destroy(&ctx.queue);
            mtx_destroy(&ctx.out_mtx);
            return -1;
        }
    }
    
    int res;
    //prima del reader
    if(thrd_join(reader, &res) != thrd_success || res == -1){
        set_error(&ctx);
        mr_queue_close(&ctx.queue);
    }
    mr_log_write(ctx.log, "mapper", 0, "thrd_end", "reader thread joined");

    //poi degli workers
    for(size_t i = 0; i < ctx.n_workers; i++){
        if(thrd_join(workers[i], &res) != thrd_success || res == -1)
            set_error(&ctx);
        mr_log_write(ctx.log, "mapper", i + 1, "thrd_end", "worker thread joined");
    }
    mr_queue_destroy(&ctx.queue);
    mtx_destroy(&ctx.out_mtx);
    
    //log per numero di coppie emesse dal mapper
    char msg[64];
    snprintf(msg, sizeof(msg), "Numero di coppie inviate dal mapper: %zu", ctx.pairs);
    mr_log_write(ctx.log, "mapper", 0, "stats", msg);

    close(STDOUT_FILENO);

    if(ctx.error){
        mr_log_write(ctx.log, "mapper", 0, "error", "Errore nel processo di mapping");
        return -1;
    }

    
    return 0;
}