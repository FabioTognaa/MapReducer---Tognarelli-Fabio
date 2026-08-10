#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "mr.h"
#include "io.h"
#include "mapper_proc.h"

//funzione reducer fittizia utile da passare come parametro a mr_create
static int dummy_reducer(const char *token, const mr_value_t *values,
			 size_t values_count, mr_emit_result_t emit,
			 void *emit_arg, void *user_arg){
	(void)token;
	(void)values;
	(void)values_count;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

//mapper di test: accetta righe che contengono esattamente "hello" ed emette la coppia <hello, 42>
static int test_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
	int v = 42;

	(void)user_arg;
	if (line->line_len != 5 || memcmp(line->line, "hello", 5) != 0)
		return 0;
	return emit("hello", &v, sizeof(v), emit_arg);
}

//nel figlio dopo la fork() reindirizza la comunicazione su stdin e stdout, chiude le altre e chiama mapper_process_main() su mr
static void run_mapper_child(int in_pipe[2], int out_pipe[2], mr_t mr){
	dup2(in_pipe[0], STDIN_FILENO);
	dup2(out_pipe[1], STDOUT_FILENO);
	close(in_pipe[0]);
	close(in_pipe[1]);
	close(out_pipe[0]);
	close(out_pipe[1]);
	_exit(mapper_process_main(mr) != 0);
}

//testa mapper_process
static int test_mapper_process(void)
{
	//inizializza tutti i dati
	char log_path[] = "/tmp/mr_mapper_log_XXXXXX";
	mr_attr_t attr;
	mr_t mr;
	int in_pipe[2];
	int out_pipe[2];
	pid_t pid;
	int status;
	char *token = NULL;
	void *value = NULL;
	size_t value_size = 0;
	int rc;

	//crea una dir
	if (mkstemp(log_path) < 0)
		return -1;

	unlink(log_path);

	//inizializza gli attributi del mapreducer
	if (mr_attr_init(&attr) != 0)
		return -1;
	
	//setta il file di log
	if (mr_attr_set_log_file(&attr, log_path) != 0)
		return -1;
	
	//crea il mapreducer
	if (mr_create(&mr, &attr, test_mapper, dummy_reducer, NULL) != 0)
		return -1;
	//crea le pipe
	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
		mr_destroy(mr);
		return -1;
	}

	//fa il fork()
	pid = fork();
	//cleanup
	if (pid < 0) {
		close(in_pipe[0]);
		close(in_pipe[1]);
		close(out_pipe[0]);
		close(out_pipe[1]);
		mr_destroy(mr);
		return -1;
	}
	//nel figlio
	if (pid == 0)
		//manda la funzione di setup del child
		run_mapper_child(in_pipe, out_pipe, mr);

	//chiude le pipe settate
	close(in_pipe[0]);
	close(out_pipe[1]);

	//scrive una linea in output
	if (mr_write_line(in_pipe[1], 4, "test", 1, "hello", 5) != 0) {
		//cleanup
		close(in_pipe[1]);
		close(out_pipe[0]);
		waitpid(pid, &status, 0);
		mr_destroy(mr);
		unlink(log_path);
		return -1;
	}
	close(in_pipe[1]);

	//legge l'output 
	rc = mr_read_pair(out_pipe[0], &token, &value, &value_size);
	close(out_pipe[0]);
	//attendo figlio e cleanup
	waitpid(pid, &status, 0);
	mr_destroy(mr);
	unlink(log_path);

	//controllo stato di uscita
	if (rc != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	//controlla i risultati in lettura
	if (strcmp(token, "hello") != 0 || value_size != sizeof(int))
		return -1;
	if (*(int *)value != 42)
		return -1;

	//cleanup e ritorno
	free(token);
	free(value);
	return 0;
}

int main(void)
{
	//controllo che le funzioni di test vadano correttamente
	if (test_mapper_process() != 0) {
		fprintf(stderr, "test_mapper_process failed\n");
		return 1;
	}

	printf("mapper: tutti i test passati.\n");
	return 0;
}
