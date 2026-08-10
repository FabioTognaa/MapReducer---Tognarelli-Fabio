#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "mr.h"
#include "io.h"
#include "reducer_proc.h"


// mapper fittizio per mr_create (il mapper non viene eseguito in questi test)
static int dummy_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){	
	(void)line;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

//nel figlio dopo la fork(), reindirizza in e out e chiama reducer_process_main() per avviarlo
static void run_reducer_child(int pipe_in[2], int pipe_out[2], mr_t mr){
	//reindirizzamento tramite pipe
	dup2(pipe_in[0], STDIN_FILENO);
	dup2(pipe_out[1], STDOUT_FILENO);
	close(pipe_in[0]);
	close(pipe_in[1]);
	close(pipe_out[0]);
	close(pipe_out[1]);
	_exit(reducer_process_main(mr) != 0);
}

//reducer di test: accetta una sola coppia <"hello", 42> ed emette un risultato opaco
static int test_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){
	
	int v = 42;
	(void)user_arg;

	//controllo che la coppia sia ok
	if( (strcmp(token, "hello") != 0) || values_count != 1 || values[0].size != sizeof(int) || *(const int *)values[0].data != 42)
		return -1;

	return emit("hello", &v, sizeof(v), emit_arg);
}

//testa il reducer che processa un token con più valori assegnati (ESEMPIO: <"hello", [1; 2; 3]>)
static int test_reducer_groups_values_by_token(const char* token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){

	//controllo token e lunghezza dei valori
	if(values_count != 3 || strcmp(token, "hello"))
		return -1;

	//lista di valori associati al token
	int buf[3];
	for(size_t i = 0; i < values_count; i++){
		if(*(const int*)values[i].data != i+1 || values[i].size != sizeof(int))
			return -1;
		buf[i] = *(const int *)values[i].data;
	}

	return emit("hello", buf, sizeof(buf), emit_arg);
}

//test del processo reducer
static int test_reducer_process(void){

	//inizializza i dati utili
	char log_path[] = "/tmp/mr_reducer_log_XXXXXX";	//path del file di log di test 
	mr_attr_t attr;
	mr_t mr;
	int in_pipe[2];
	int out_pipe[2];
	pid_t pid;
	int status;
	char *token = NULL;
	void *value = NULL;
	size_t result_size = 0;

	//crea una dir 
	if (mkstemp(log_path) < 0)
		return -1;
	unlink(log_path);

	//setto attributi mr
	if(mr_attr_init(&attr) != 0)
		return -1;

	//setta il file di log
	if (mr_attr_set_log_file(&attr, log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	//creazione mapreducer
	if(mr_create(&mr, &attr, dummy_mapper, test_reducer_groups_values_by_token, NULL) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	//creo pipe
	if(pipe(in_pipe) != 0 || pipe(out_pipe) != 0){
		mr_attr_destroy(&attr);
		mr_destroy(mr);
		return -1;
	}

	//fork
	pid = fork();
	if(pid < 0){
		close(in_pipe[0]);
		close(in_pipe[1]);
		close(out_pipe[0]);
		close(out_pipe[1]);
		mr_attr_destroy(&attr);
		mr_destroy(mr);
		return -1;
	}

	//figlio
	if(pid == 0)
		run_reducer_child(in_pipe, out_pipe, mr);

	//padre
	close(in_pipe[0]);
	close(out_pipe[1]);

	//inizializza array di valori inventato da scrivere 
	int numeri[] = {1, 2, 3};
	//scrive un gruppo di un token in output
	for(int i = 0; i < 3; i++){
		int v = numeri[i];
		if(mr_write_pair(in_pipe[1], "hello", &v, sizeof(v), 5) != 0){
			close(in_pipe[1]);	
			close(out_pipe[0]);
			waitpid(pid, &status, 0);	
			mr_attr_destroy(&attr);
			mr_destroy(mr);
			unlink(log_path);
			return -1;
		}
	}
	close(in_pipe[1]);

	//legge il risultato dell'output
	if(mr_read_result(out_pipe[0], &token, &value, &result_size ) != 0){
		close(out_pipe[0]);
		waitpid(pid, &status, 0);	
		mr_attr_destroy(&attr);
		mr_destroy(mr);
		unlink(log_path);
		free(token);
		free(value);
		return -1;
	}
	close(out_pipe[0]);

	//controllo dei risultati appena letti
	if(strcmp(token, "hello") != 0 || result_size != 3 * sizeof(int)){
		waitpid(pid, &status, 0);	
		mr_attr_destroy(&attr);
		mr_destroy(mr);
		unlink(log_path);
		free(token);
		free(value);
		return -1;
	}

	for(size_t i = 0; i < 3; i++){
		if(((const int *)value)[i] != (int)(i + 1)){
			waitpid(pid, &status, 0);	
			mr_attr_destroy(&attr);
			mr_destroy(mr);	
			unlink(log_path);	
			free(token);
			free(value);
			return -1;
		}
	}
	waitpid(pid, &status, 0);	
	mr_attr_destroy(&attr);
	mr_destroy(mr);
	unlink(log_path);

	//controlla lo stato di uscita
	if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	free(token);
	free(value);
	return 0;
}

//main
int main(void){
	if(test_reducer_process() != 0){
		fprintf(stderr, "reducer: test non andati a buon fine\n");
		return 1;
	}
	printf("reducer: tutti i test superati\n");
	return 0;
}
