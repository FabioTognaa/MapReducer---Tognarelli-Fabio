#include "mr.h"
#include "io.h"
#include "mapper_proc.h"
#include "reducer_proc.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

//macro per syscall con assegnamento
#define CHECK_ASSIGN(result, expression, message) \
do { \
	if ((result = (expression)) == -1) { \
		perror(message); \
		return -1;	\
	} \
} while (0)

//macro per syscall senza assegnamento
#define CHECK_SYSCALL(expression, message) \
do { \
	if ((expression) == -1) { \
		perror(message); \
		return -1;\
	} \
} while (0)

//tipo record: impacchetta le info dal reducer da scrivere sul file di output
typedef struct{
	char *token;
	void *res;
	size_t res_len;
	size_t token_len;
} record_from_reducer_t;


//FA CLEANUP DEI RECORD SALVATI NEL MAIN
static void free_records(record_from_reducer_t *records, size_t n)
{
    if (records == NULL)
        return;
    for (size_t i = 0; i < n; i++) {
        free(records[i].token);
        free(records[i].res);
    }
    free(records);
}

//FA LA WAITPID COMPLETA PER I PROCESSI FIGLI
static int wait_children(pid_t pid_mapper, pid_t pid_reducer)
{
    int sts_map, sts_red;
    if (waitpid(pid_mapper, &sts_map, 0) == -1 ||
        waitpid(pid_reducer, &sts_red, 0) == -1) {
        perror("waitpid");
        return -1;
    }
    if (!WIFEXITED(sts_map) || !WIFEXITED(sts_red) ||
        WEXITSTATUS(sts_map) != 0 || WEXITSTATUS(sts_red) != 0)

        return -1;
    return 0;
}



//FUNZIONE PER RESTITUIRE ERRORE DI DEFAULT
static int mr_fail_inval(void)
{
	errno = EINVAL;
	return -1;
}

//ORDINA LESSICOGRAFICAMENTE GLI ELEMENTI NELL'ARRAY DI RECORD
static int cmp_records(const void *a, const void *b){
	record_from_reducer_t *ra = a;
	record_from_reducer_t *rb = b;
	
	return strcmp(ra->token, rb->token);
}
//INIZIALIZZO GLI ATTRIBUTI DEL MAPPER REDUCER
int mr_attr_init(mr_attr_t *attr)
{
	if (attr == NULL)
		return mr_fail_inval();

	attr->mapper_threads = 1;
	attr->reducer_threads = 1;
	attr->queue_size = DEFAULT_QUEUE_SIZE;
	attr->log_file = NULL;
	return 0;
}

//DISTRUGGE GLI ATTRIBUTI DI UN MAPPER REDUCER
int mr_attr_destroy(mr_attr_t *attr)
{
	if (attr == NULL)
		return mr_fail_inval();
	return 0;
}

//SETTA IL NUMERO DI WORKER THRDS PER IL MAPPER
int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n)
{
	if (attr == NULL || n == 0)
		return mr_fail_inval();

	attr->mapper_threads = n;
	return 0;
}

//SETTA IL NUMERO DI WORKER THRDS PER IL REDUCER
int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n)
{
	if (attr == NULL || n == 0)
		return mr_fail_inval();

	attr->reducer_threads = n;
	return 0;
}

//SETTA LA GRANDEZZA DELLA CODA PER IL MAPPER
int mr_attr_set_queue_size(mr_attr_t *attr, size_t n)
{
	if (attr == NULL || n == 0)
		return mr_fail_inval();

	attr->queue_size = n;
	return 0;
}

//SETTA IL PATH PER IDENTIFICARE IL FILE DI LOG
int mr_attr_set_log_file(mr_attr_t *attr, const char *path)
{
	if (attr == NULL)
		return mr_fail_inval();

	attr->log_file = path;
	return 0;
}


//CRAZIONE DI UN MAP REDUCER E SETUP STANDARD
int mr_create(mr_t *mr, const mr_attr_t *attr, mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg){
	
	//mi appoggio ad unsa struct handle per la popolazione
	struct mr *handle;

	//se un attributo necessario non e' stato correttamente passato
	if (mr == NULL || attr == NULL || mapper == NULL || reducer == NULL)
		return mr_fail_inval();

	//popolo l'handle con i valori in ingresso
	handle = malloc(sizeof(*handle));
	if (handle == NULL) {
		return -1;
	}

	handle->attr = *attr;
	handle->mapper = mapper;
	handle->reducer = reducer;
	handle->user_arg = user_arg;
	handle->started = 0;
	handle->error = 0;
	handle->log.fd = -1;

	//chiamo la funzione per aprire/creare il file di log
	if (mr_create_log(&handle->log, (char *)handle->attr.log_file) < 0) {
		//eventuale cleanup
		free(handle);
		return -1;
	}

	//trasferisco i dati di setup e ritorno
	*mr = handle;
	return 0;
}

//ELIMINA UN MAP REDUCER
int mr_destroy(mr_t mr)
{
	if (mr == NULL)
		return mr_fail_inval();

	//chiudo il file di log
	if (mr->log.fd >= 0)
		mr_log_close(&mr->log);
	free(mr);
	return 0;
}


//DA IL VIA AD UN MAP REDUCER
int mr_start(mr_t mr, const char *input_path, const char *output_path){
	//controllo validita' dati in input
	if (mr == NULL || input_path == NULL || output_path == NULL)
		return -1;		


	//setto correttamente lo start
	if(mr->started)
		return mr_fail_inval();
	
		mr->started = 1;

	//creazione delle pipe per la comunicazione tra processi
	int main_to_mapper [2];			//main -> mapper
	int mapper_to_reducer [2];	//mapper -> reducer
	int reducer_to_main [2];		//reducer -> main

	CHECK_SYSCALL(pipe(main_to_mapper), "pipe main_to_mapper");
	CHECK_SYSCALL(pipe(mapper_to_reducer), "pipe mapper_to_reducer");
	CHECK_SYSCALL(pipe(reducer_to_main), "pipe reducer_to_main");

	mr_log_write(&mr->log, "main", 0, "pipe", "created 3 pipes");

	//fork dei processi figli
	pid_t pid_mapper;
	pid_t pid_reducer;

	//fork del mapper
	CHECK_ASSIGN(pid_mapper, fork(), "fork su pid_mapper");

	mr_log_write(&mr->log, "main", 0, "fork", "mapper process created");

	//dentro al proc. mapper
	if(!pid_mapper){
		//riassegno input e output
		CHECK_SYSCALL(dup2(main_to_mapper[0], STDIN_FILENO), "dup2 mapper");
		CHECK_SYSCALL(dup2(mapper_to_reducer[1], STDOUT_FILENO), "dup2 mapper");
		
		//chiudo il resto
		CHECK_SYSCALL(close(main_to_mapper[0]), "close mapper");
		CHECK_SYSCALL(close(main_to_mapper[1]), "close mapper");
		CHECK_SYSCALL(close(mapper_to_reducer[0]), "close mapper");
		CHECK_SYSCALL(close(mapper_to_reducer[1]), "close mapper");
		CHECK_SYSCALL(close(reducer_to_main[0]), "close mapper");
		CHECK_SYSCALL(close(reducer_to_main[1]), "close mapper");

		//start del mapper
		_exit(mapper_process_main(mr) != 0);
	}

	//fork del reducer
	CHECK_ASSIGN(pid_reducer, fork(), "fork su pid_reducer");


	mr_log_write(&mr->log, "main", 0, "fork", "reducer process created");

	//dentro al proc. reducer
	if(!pid_reducer){
		//riassegno input e output
		CHECK_SYSCALL(dup2(mapper_to_reducer[0], STDIN_FILENO), "dup2 reducer");
		CHECK_SYSCALL(dup2(reducer_to_main[1], STDOUT_FILENO), "dup2 reducer");
		
		CHECK_SYSCALL(close(mapper_to_reducer[0]), "close reducer");
		CHECK_SYSCALL(close(mapper_to_reducer[1]), "close reducer");
		CHECK_SYSCALL(close(reducer_to_main[0]), "close reducer");
		CHECK_SYSCALL(close(reducer_to_main[1]), "close reducer");
		CHECK_SYSCALL(close(main_to_mapper[0]), "close reducer");
		CHECK_SYSCALL(close(main_to_mapper[1]), "close reducer");
		
		//start del reducer
		_exit(reducer_process_main(mr) != 0);
	}
	else{	//processo main

		CHECK_SYSCALL(close(mapper_to_reducer[0]), "close main");
		CHECK_SYSCALL(close(mapper_to_reducer[1]), "close main");
		CHECK_SYSCALL(close(reducer_to_main[1]), "close main");
		CHECK_SYSCALL(close(main_to_mapper[0]), "close main");


		
		size_t lines = 0;
		//leggo le righe dai file in input
		if(mr_send_input(input_path, main_to_mapper[1], &lines) == -1){
			//cleanup
			close(reducer_to_main[0]);
			(void)wait_children(pid_mapper, pid_reducer);
			return -1;
		}

		//log del numero di linee in input
		char msg[64];
		snprintf(msg, sizeof(msg), "lines sent to mapper: %zu", lines);
		mr_log_write(&mr->log, "main", 0, "stats", msg);


		//struttura per salvare i record in output dal reducer
		size_t dim = 0, cap = 4;
		record_from_reducer_t *record = malloc(cap * sizeof(record_from_reducer_t));
		if(!record){
			close(reducer_to_main[0]);
			free_records(record, dim);
			(void)wait_children(pid_mapper, pid_reducer);
			return -1;
		}

		//log lettura record
		mr_log_write(&mr->log, "main", 0, "read", "reading final results from reducer");

		//leggo i risultati del reducer
		while(1){

			char *token = NULL;
			void *result = NULL;
			size_t result_size = 0;
			


			

			//uso funzione mr_read_result
			int rr = mr_read_result(reducer_to_main[0], &token, &result, &result_size);

			if(rr == EOF_REACHED)
				break;

			if(rr == -1){
				if(close(reducer_to_main[0]) != 0){
					free_records(record, dim);
					perror("close reducer_to_main");
    				return -1;
				}
				free_records(record, dim);
				(void)wait_children(pid_mapper, pid_reducer);
				return -1;
			}

			//accumulo in un array di record per il file di output
			if(dim == cap){
				int new_cap = cap * 2;
				record_from_reducer_t *nr = realloc(record, new_cap * sizeof(record_from_reducer_t));

				//eventuale cleanup
				if(!nr){
					//chiudi fd rimanenti
					if(close(reducer_to_main[0]) != 0){ 
						perror("reducer_to_main");
						return -1;
					}
					free_records(record, dim);
					(void)wait_children(pid_mapper, pid_reducer);
					return -1;
				}

				//assegno i nuovi valori
				record = nr;
				cap = new_cap;
			}
				
			//salvo nell'array il record
			record[dim].token = token;  
			record[dim].res = result;  
			record[dim].res_len = result_size;  
			record[dim].token_len = strlen(token);  
			dim++;
		}

		//log per il numero totale di record prodotti
		char msgg[64];
		snprintf(msgg, sizeof(msgg), "numero di record prodotti: %zu", dim);

		mr_log_write(&mr->log, "main", 0, "stats", msgg);

		//chiudo comunicazione verso il reducer
		if(close(reducer_to_main[0]) != 0){ 
						perror("close reducer_to_main");
						return -1;
					}
		//ordinamento lessicografico dei record per token
		qsort(record, dim, sizeof(record_from_reducer_t), cmp_records);

		
		mr_log_write(&mr->log, "main", 0, "output", "opening output file");

		//apro file di output
		int f_out;
		if((f_out = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1){ 
			perror("open in mr_create");
			return -1;
		}

		//scrivo sul file tutti i record in ordine lessicografico
		for(size_t i = 0; i < dim; i++){
			int cr;
			if((cr = mr_write_result(f_out, record[i].token, record[i].res, record[i].res_len, record[i].token_len)) == ERROR_SYSTEM){
				free_records(record, dim);
				(void)wait_children(pid_mapper, pid_reducer);
				return -1;
			}
		}


		mr_log_write(&mr->log, "main", 0, "output", "closing output file");

		//chiudere il file di output
		if(close(f_out) != 0){ 
			perror("close in mr_create");
			return -1;
		}

		//cleanup del processo
		free_records(record, dim);
		
		//waitpid sui figli
		if(wait_children(pid_mapper, pid_reducer) != 0)
			mr->error = 1;
	}

	return mr->error ? -1 : 0;
}
