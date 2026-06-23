#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "mr.h"
#include "io.h"
#include "mapper_proc.h"

static int dummy_reducer(const char *token, const mr_value_t *values,
			 size_t values_count, mr_emit_result_t emit,
			 void *emit_arg, void *user_arg)
{
	(void)token;
	(void)values;
	(void)values_count;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

static int test_mapper(const mr_file_line_t *line, mr_emit_pair_t emit,
		       void *emit_arg, void *user_arg)
{
	int v = 42;

	(void)user_arg;
	if (line->line_len != 5 || memcmp(line->line, "hello", 5) != 0)
		return 0;
	return emit("hello", &v, sizeof(v), emit_arg);
}

static void run_mapper_child(int in_pipe[2], int out_pipe[2], mr_t mr)
{
	dup2(in_pipe[0], STDIN_FILENO);
	dup2(out_pipe[1], STDOUT_FILENO);
	close(in_pipe[0]);
	close(in_pipe[1]);
	close(out_pipe[0]);
	close(out_pipe[1]);
	_exit(mapper_process_main(mr) != 0);
}

static int test_mapper_process(void)
{
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

	if (mkstemp(log_path) < 0)
		return -1;
	unlink(log_path);

	if (mr_attr_init(&attr) != 0)
		return -1;
	if (mr_attr_set_log_file(&attr, log_path) != 0)
		return -1;
	if (mr_create(&mr, &attr, test_mapper, dummy_reducer, NULL) != 0)
		return -1;
	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
		mr_destroy(mr);
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		close(in_pipe[0]);
		close(in_pipe[1]);
		close(out_pipe[0]);
		close(out_pipe[1]);
		mr_destroy(mr);
		return -1;
	}
	if (pid == 0)
		run_mapper_child(in_pipe, out_pipe, mr);

	close(in_pipe[0]);
	close(out_pipe[1]);

	if (mr_write_line(in_pipe[1], 4, "test", 1, "hello", 5) != 0) {
		close(in_pipe[1]);
		close(out_pipe[0]);
		waitpid(pid, &status, 0);
		mr_destroy(mr);
		unlink(log_path);
		return -1;
	}
	close(in_pipe[1]);

	rc = mr_read_pair(out_pipe[0], &token, &value, &value_size);
	close(out_pipe[0]);
	waitpid(pid, &status, 0);
	mr_destroy(mr);
	unlink(log_path);

	if (rc != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	if (strcmp(token, "hello") != 0 || value_size != sizeof(int))
		return -1;
	if (*(int *)value != 42)
		return -1;

	free(token);
	free(value);
	return 0;
}

int main(void)
{
	if (test_mapper_process() != 0) {
		fprintf(stderr, "test_mapper_process failed\n");
		return 1;
	}

	printf("mapper: tutti i test passati.\n");
	return 0;
}
