#include "mr.h"
#include "io.h"
#include "mapper_proc.h"
#include "reducer_proc.h"
#include "log.h"
#include "mr_err.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>


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
static int wait_children(mr_t mr, pid_t pid_mapper, pid_t pid_reducer)
{
    int sts_map, sts_red;
    if (waitpid(pid_mapper, &sts_map, 0) == -1 ||
        waitpid(pid_reducer, &sts_red, 0) == -1) {
		
        return -1;
    }
    if (!WIFEXITED(sts_map) || !WIFEXITED(sts_red) ||
        WEXITSTATUS(sts_map) != 0 || WEXITSTATUS(sts_red) != 0){
			errno = EIO;
			if (WIFSIGNALED(sts_map))
    		mr_log_write(&mr->log, "main", 0, "error", "mapper terminato dal segnale");
			

			if (WIFSIGNALED(sts_red))
    			mr_log_write(&mr->log, "main", 0, "error", "reducer terminato dal segnale");
			return -1;
		}
		
    return 0;
}

//GESTIONE ERRORE DI UN PROCESSO FIGLIO
void mr_child_fail(mr_log_t *log, const char *proc, const char *msg){

	//scrittura dell'errore sul file di log
	mr_log_write(log, proc, 0, "error", msg);
	_exit(1);
}

//GESTIONE DEL CLEANUP PER MR_START
int mr_start_cleanup(mr_t mr, mr_start_state_t *st, int saved_errno){
	//chiusura delle pipe rimanenti
	if(st->mapper_write_open)
		close(st->mapper_write_fd);
	if(st->reducer_read_open)
		close(st->reducer_read_fd);

	//chiudo il file di output se aperto
	if((st->output_fd) >= 0)
		close(st->output_fd);

	//faccio free dei records da usare in scrittura sul file di output
	free_records(st->records, st->n_records);

	//faccio le waitpid in caso ci siano stati dei fork
	if(st->children_forked)
		if(wait_children(mr, st->pid_mapper, st->pid_reducer) != 0)
			saved_errno = EIO;


	//setto il nuovo errno
	errno = saved_errno;
	return -1;
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
int mr_attr_set_log_file(mr_attr_t *attr, const char *path){
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
		errno = ENOMEM;
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
int mr_destroy(mr_t mr){
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
		return mr_fail_inval();		


	//setto correttamente lo start
	if(mr->started)
		return mr_fail_inval();
	mr->started = 1;

	//creazione delle pipe per la comunicazione tra processi
	int main_to_mapper [2];			//main -> mapper
	int mapper_to_reducer [2];	//mapper -> reducer
	int reducer_to_main [2];		//reducer -> main

	//setup dello stato inziale per gestire gli errori
	mr_start_state_t st;
	st.mapper_write_open = 0;
	st.reducer_read_open = 0;
	st.output_fd = -1;
	st.records = NULL;
	st.n_records = 0;
	st.children_forked = 0;



	//inizializzazione delle pipe
	if(pipe(main_to_mapper) == -1){
		int saved = errno;
		mr_log_write(&mr->log, "main", 0, "error", "pipe main_to_mapper");
		errno = saved;
		return -1;
	};

	if(pipe(mapper_to_reducer) == -1){
		int saved = errno;
		mr_log_write(&mr->log, "main", 0, "error", "pipe mapper_to_reducer");
		close(main_to_mapper[0]);
		close(main_to_mapper[1]);
		errno = saved;
		return -1;
	};
	
	if(pipe(reducer_to_main) == -1){
		int saved = errno;
		mr_log_write(&mr->log, "main", 0, "error", "pipe reducer_to_main");
		close(main_to_mapper[0]);
		close(main_to_mapper[1]);
		close(mapper_to_reducer[0]);
		close(mapper_to_reducer[1]);

		errno = saved;
		return -1;
	};
	
	//aggiorno i valori dello stato iniziale
	st.mapper_write_fd = main_to_mapper[1];
	st.mapper_write_open = 1;
	st.reducer_read_fd = reducer_to_main[0];
	st.reducer_read_open = 1;
	
	//log per la creazione con successo delle 3 pipe
	mr_log_write(&mr->log, "main", 0, "pipe", "created 3 pipes");

	//pid dei processi figli
	pid_t pid_mapper;
	pid_t pid_reducer;

	//fork del processo mapper
	pid_mapper = fork();
	if(pid_mapper == -1){
		int saved = errno;
		mr_log_write(&mr->log, "main", 0, "error", "fork del mapper fallita");
		close(main_to_mapper[0]);
		close(main_to_mapper[1]);
		close(mapper_to_reducer[0]);
		close(mapper_to_reducer[1]);
		close(reducer_to_main[0]);
		close(reducer_to_main[1]);
		errno = saved;
		return -1;
	}
	

	mr_log_write(&mr->log, "main", 0, "fork", "mapper process created");

	//dentro al proc. mapper
	if(!pid_mapper){
		if(dup2(main_to_mapper[0], STDIN_FILENO) == -1)
			mr_child_fail(&mr->log, "mapper", "dup2 main_to_mapper stdin fallita");
		if(dup2(mapper_to_reducer[1], STDOUT_FILENO) == -1)
			mr_child_fail(&mr->log, "mapper", "dup2 mapper_to_reducer stdout fallita");

		if(close(main_to_mapper[0]) == -1)
			mr_child_fail(&mr->log, "mapper", "close main_to_mapper in input fallita");
		if(close(main_to_mapper[1]) == -1)
			mr_child_fail(&mr->log, "mapper", "close main_to_mapper in output fallita");
		if(close(mapper_to_reducer[0]) == -1)
			mr_child_fail(&mr->log, "mapper", "close mapper_to_reducer in input fallita");
		if(close(mapper_to_reducer[1]) == -1)
			mr_child_fail(&mr->log, "mapper", "close mapper_to_reducer in output fallita");
		if(close(reducer_to_main[0]) == -1)
			mr_child_fail(&mr->log, "mapper", "close reducer_to_main in input fallita");
		if(close(reducer_to_main[1]) == -1)
			mr_child_fail(&mr->log, "mapper", "close reducer_to_main in output fallita");

		_exit(mapper_process_main(mr) != 0);
	}

	//fork del reducer con cleanup parziale del mapper
	pid_reducer = fork();
	if (pid_reducer == -1) {
		int saved = errno;
		mr_log_write(&mr->log, "main", 0, "error", "fork reducer failed");
		close(main_to_mapper[0]);
		close(main_to_mapper[1]);
		close(mapper_to_reducer[0]);
		close(mapper_to_reducer[1]);
		close(reducer_to_main[0]);
		close(reducer_to_main[1]);
		(void)waitpid(pid_mapper, NULL, 0);   /* mapper già avviato */
		errno = saved;
		return -1;
	}

	

	mr_log_write(&mr->log, "main", 0, "fork", "reducer process created");

	//dentro al proc. reducer
	if(!pid_reducer){
		if(dup2(mapper_to_reducer[0], STDIN_FILENO) == -1)
			mr_child_fail(&mr->log, "reducer", "dup2 mapper_to_reducer stdin fallita");
		if(dup2(reducer_to_main[1], STDOUT_FILENO) == -1)
			mr_child_fail(&mr->log, "reducer", "dup2 reducer_to_main stdout fallita");

		if(close(mapper_to_reducer[0]) == -1)
			mr_child_fail(&mr->log, "reducer", "close mapper_to_reducer in input fallita");
		if(close(mapper_to_reducer[1]) == -1)
			mr_child_fail(&mr->log, "reducer", "close mapper_to_reducer in output fallita");
		if(close(reducer_to_main[0]) == -1)
			mr_child_fail(&mr->log, "reducer", "close reducer_to_main in input fallita");
		if(close(reducer_to_main[1]) == -1)
			mr_child_fail(&mr->log, "reducer", "close reducer_to_main in output fallita");
		if(close(main_to_mapper[0]) == -1)
			mr_child_fail(&mr->log, "reducer", "close main_to_mapper in input fallita");
		if(close(main_to_mapper[1]) == -1)
			mr_child_fail(&mr->log, "reducer", "close main_to_mapper in output fallita");

		_exit(reducer_process_main(mr) != 0);
	}
	else{	//processo main

		//aggiorno i valori dello starte value x gestione errori
		st.pid_mapper = pid_mapper;
		st.pid_reducer = pid_reducer; 
		st.children_forked = 1;

		if(close(mapper_to_reducer[0]) == -1){
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "error", "close mapper_to_reducer in input fallita");
			errno = saved;
			return mr_start_cleanup(mr, &st, saved);
		}

		if(close(mapper_to_reducer[1]) == -1){
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "close", "close mapper_to_reducer in output fallita");
			errno = saved;
			return mr_start_cleanup(mr, &st, errno);
		}

		if(close(reducer_to_main[1]) == -1){
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "close", "close reducer_to_main in output fallita");
			errno = saved;
			return mr_start_cleanup(mr, &st, errno);
		}
		if(close(main_to_mapper[0]) == -1){
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "close", "close main_to_mapper in input fallita");
			errno = saved;
			return mr_start_cleanup(mr, &st, errno);
		}

		//log per apertura file/dir di input
		mr_log_write(&mr->log, "main", 0, "apertura file di input", NULL);

		size_t lines = 0;
		//leggo le righe dai file in input
		if(mr_send_input(input_path, main_to_mapper[1], &lines) == -1){
			//cleanup con funzione e messaggio di log
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "error", "mr_send_input nel main fallito");
			return mr_start_cleanup(mr, &st, saved);
		}
		st.mapper_write_open = 0;

		//log per chiusura file/dir di input
		mr_log_write(&mr->log, "main", 0, "chiusura file di input", NULL);

		//log del numero di linee in input
		char msg[64];
		snprintf(msg, sizeof(msg), "lines sent to mapper: %zu", lines);
		mr_log_write(&mr->log, "main", 0, "stats", msg);


		//struttura per salvare i record in output dal reducer
		size_t dim = 0, cap = 4;
		record_from_reducer_t *record = malloc(cap * sizeof(record_from_reducer_t));
		if(!record){
			//cleanup con funzione e messaggio di log
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "error", "malloc iniziale dell'array di record fallita");
			return mr_start_cleanup(mr, &st, saved);
		}

		//log lettura record
		mr_log_write(&mr->log, "main", 0, "read", "lettura dei risultati finali dei record");

		//leggo i risultati del reducer
		while(1){

			char *token = NULL;
			void *result = NULL;
			size_t result_size = 0;

			//uso funzione mr_read_result
			int rr = mr_read_result(st.reducer_read_fd, &token, &result, &result_size);

			if(rr == EOF_REACHED)
				break;

			if(rr == -1){
				//cleanup con funzione e messaggio di log
				int saved = errno;
				mr_log_write(&mr->log, "main", 0, "error", "mr_read_result nel main fallita");
				return mr_start_cleanup(mr, &st, saved);
			}

			//accumulo in un array di record per il file di output
			if(dim == cap){
				int new_cap = cap * 2;
				record_from_reducer_t *nr = realloc(record, new_cap * sizeof(record_from_reducer_t));

				//eventuale cleanup
				if(!nr){
					int saved = ENOMEM;
					mr_log_write(&mr->log, "main", 0, "error", "realloc di array di record");
					return mr_start_cleanup(mr, &st, saved);
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

			//aggiorno st
			st.records = record;
			st.n_records = dim;
		}
		st.reducer_read_open = 0;

		//log per il numero totale di record prodotti
		char msgg[64];
		snprintf(msgg, sizeof(msgg), "numero di record prodotti: %zu", dim);

		mr_log_write(&mr->log, "main", 0, "stats", msgg);

		//chiudo comunicazione verso il reducer
		if(close(st.reducer_read_fd) != 0){ 
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "error", "close di reducer_to_main fallita");
			return mr_start_cleanup(mr, &st, saved);
		}

		//ordinamento lessicografico dei record per token
		qsort(record, dim, sizeof(record_from_reducer_t), cmp_records);

		//log per l'apertura del file di output
		mr_log_write(&mr->log, "main", 0, "output", "opening output file");

		//apro file di output
		int f_out;
		if((f_out = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1){ 
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "error", "open del file di output fallita");
			return mr_start_cleanup(mr, &st, saved);
		}

		st.output_fd = f_out;
		//scrivo sul file tutti i record in ordine lessicografico
		for(size_t i = 0; i < dim; i++){
			int cr;
			if((cr = mr_write_result(f_out, record[i].token, record[i].res, record[i].res_len, record[i].token_len)) == ERROR_SYSTEM){
				int saved = errno;
				mr_log_write(&mr->log, "main", 0, "error", "mr_write_result fallita");
				return mr_start_cleanup(mr, &st, saved);
			}
		}


		//log per la chiusura del file di output
		mr_log_write(&mr->log, "main", 0, "output", "closing output file");

		//chiudere il file di output
		if(close(f_out) != 0){ 
			int saved = errno;
			mr_log_write(&mr->log, "main", 0, "error", "close del file di output fallita");
			return mr_start_cleanup(mr, &st, saved);
		}

		//cleanup del processo
		free_records(record, dim);
		
		//waitpid sui figli
		if(wait_children(mr, st.pid_mapper, st.pid_reducer) != 0)
			return -1;
	}
	return 0;
}
