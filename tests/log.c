#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "log.h"

#define WRITES_PER_PROC 50

static int check_line_format(const char *line)
{
	char ts[32], proc[16], evt[64], msg[128];
	size_t tid;

	if (sscanf(line, "[%[^]]] [%[^]]] [%zu] [%[^]]] %[^\n]",
		   ts, proc, &tid, evt, msg) != 5)
		return -1;
	if (strlen(ts) != 19)
		return -1;
	if (strcmp(proc, "main") != 0 || tid != 0)
		return -1;
	return 0;
}

static int count_formatted_lines(const char *path, int expected)
{
	FILE *f;
	char line[1024];
	int n = 0;

	f = fopen(path, "r");
	if (f == NULL)
		return -1;

	while (fgets(line, sizeof(line), f) != NULL) {
		if (check_line_format(line) != 0) {
			fclose(f);
			return -1;
		}
		n++;
	}
	fclose(f);

	return n == expected ? 0 : -1;
}

static int count_log_lines(const char *path, int expected,
			   int *parent_lines, int *child_lines)
{
	FILE *f;
	char line[1024];
	int n = 0;

	f = fopen(path, "r");
	if (f == NULL)
		return -1;

	while (fgets(line, sizeof(line), f) != NULL) {
		if (check_line_format(line) != 0) {
			fclose(f);
			return -1;
		}
		if (strstr(line, " parent") != NULL)
			(*parent_lines)++;
		else if (strstr(line, " child") != NULL)
			(*child_lines)++;
		else {
			fclose(f);
			return -1;
		}
		n++;
	}
	fclose(f);

	if (n != expected)
		return -1;
	return 0;
}

static int test_single_write(void)
{
	char path[] = "/tmp/mr_log_test_XXXXXX";
	mr_log_t log;
	int fd;

	fd = mkstemp(path);
	if (fd < 0)
		return -1;
	close(fd);

	if (mr_create_log(&log, path) < 0)
		return -1;
	if (mr_log_write(&log, "main", 0, "pipe", "created") != 0)
		return -1;
	mr_log_close(&log);

	if (count_formatted_lines(path, 1) != 0) {
		unlink(path);
		return -1;
	}
	unlink(path);
	return 0;
}

static int test_default_log(void)
{
	char cwd[256];
	char dir[] = "/tmp/mr_log_dir_XXXXXX";
	mr_log_t log;
	FILE *f;

	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return -1;
	if (mkdtemp(dir) == NULL)
		return -1;
	if (chdir(dir) != 0)
		return -1;

	unlink("mr.log");
	if (mr_create_log(&log, NULL) < 0)
		return -1;
	if (mr_log_write(&log, "main", 0, "test", "ok") != 0)
		return -1;
	mr_log_close(&log);

	f = fopen("mr.log", "r");
	if (f == NULL) {
		chdir(cwd);
		rmdir(dir);
		return -1;
	}
	fclose(f);
	unlink("mr.log");
	chdir(cwd);
	rmdir(dir);
	return 0;
}

static int test_fork_sync(void)
{
	char path[] = "/tmp/mr_log_fork_XXXXXX";
	mr_log_t log;
	pid_t pid;
	int fd;
	int status;
	int parent = 0, child = 0;

	fd = mkstemp(path);
	if (fd < 0)
		return -1;
	close(fd);

	if (mr_create_log(&log, path) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0) {
		for (int i = 0; i < WRITES_PER_PROC; i++) {
			if (mr_log_write(&log, "main", 0, "test", "child") != 0)
				_exit(1);
		}
		_exit(0);
	}

	for (int i = 0; i < WRITES_PER_PROC; i++) {
		if (mr_log_write(&log, "main", 0, "test", "parent") != 0)
			return -1;
	}

	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	mr_log_close(&log);

	if (count_log_lines(path, WRITES_PER_PROC * 2, &parent, &child) != 0) {
		unlink(path);
		return -1;
	}
	unlink(path);

	if (parent != WRITES_PER_PROC || child != WRITES_PER_PROC)
		return -1;
	return 0;
}

int main(void)
{
	if (test_single_write() != 0) {
		fprintf(stderr, "test_single_write failed\n");
		return 1;
	}
	if (test_default_log() != 0) {
		fprintf(stderr, "test_default_log failed\n");
		return 1;
	}
	if (test_fork_sync() != 0) {
		fprintf(stderr, "test_fork_sync failed\n");
		return 1;
	}

	printf("log: tutti i test passati.\n");
	return 0;
}
