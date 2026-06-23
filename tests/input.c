#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "io.h"

static void free_line(mr_file_line_t *l)
{
	free((void *)l->file_name);
	free((void *)l->line);
}

static int strneq(const char *s, size_t n, const char *exp)
{
	size_t elen = strlen(exp);

	return n == elen && (elen == 0 || memcmp(s, exp, elen) == 0);
}

static int expect_line(int fd, const char *fname, unsigned long num, const char *text)
{
	mr_file_line_t l = {0};
	int rc = mr_read_line(fd, &l);

	if (rc != 0 || !strneq(l.file_name, l.file_name_len, fname) ||
	    l.line_number != num || !strneq(l.line, l.line_len, text)) {
		free_line(&l);
		return -1;
	}
	free_line(&l);
	return 0;
}

static int write_file(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");

	if (f == NULL)
		return -1;
	if (content != NULL && fputs(content, f) == EOF) {
		fclose(f);
		return -1;
	}
	return fclose(f) != 0 ? -1 : 0;
}

static int pipe_send_input(const char *path, int *read_fd)
{
	int p[2];
	pid_t pid;
	int st;

	if (pipe(p) != 0)
		return -1;
	pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		close(p[0]);
		_exit(mr_send_input(path, p[1]) != 0);
	}
	close(p[1]);
	*read_fd = p[0];
	if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		close(p[0]);
		return -1;
	}
	return 0;
}

static int test_single_file(void)
{
	char path[] = "/tmp/mr_input_test_XXXXXX";
	static const char *rows[] = { "hello", "world", "", "last" };
	const char *fname;
	int fd, rfd;

	fd = mkstemp(path);
	if (fd < 0 || write_file(path, "hello\nworld\n\nlast") != 0)
		return -1;
	close(fd);
	fname = strrchr(path, '/') + 1;

	if (pipe_send_input(path, &rfd) != 0) {
		unlink(path);
		return -1;
	}

	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		if (expect_line(rfd, fname, i + 1, rows[i]) != 0) {
			close(rfd);
			unlink(path);
			return -1;
		}
	}

	if (mr_read_line(rfd, &(mr_file_line_t){0}) != EOF_REACHED) {
		close(rfd);
		unlink(path);
		return -1;
	}
	close(rfd);
	unlink(path);
	return 0;
}

static int test_directory(void)
{
	char dir[] = "/tmp/mr_input_dir_XXXXXX";
	char path_a[256], path_b[256];
	int rfd;

	if (mkdtemp(dir) == NULL)
		return -1;
	snprintf(path_a, sizeof(path_a), "%s/a.txt", dir);
	snprintf(path_b, sizeof(path_b), "%s/b.txt", dir);
	if (write_file(path_b, "beta\n") != 0 || write_file(path_a, "alpha\n") != 0)
		return -1;

	if (pipe_send_input(dir, &rfd) != 0)
		return -1;
	if (expect_line(rfd, "a.txt", 1, "alpha") != 0 ||
	    expect_line(rfd, "b.txt", 1, "beta") != 0 ||
	    mr_read_line(rfd, &(mr_file_line_t){0}) != EOF_REACHED) {
		close(rfd);
		return -1;
	}

	close(rfd);
	unlink(path_a);
	unlink(path_b);
	rmdir(dir);
	return 0;
}

static int test_empty_file(void)
{
	char path[] = "/tmp/mr_input_empty_XXXXXX";
	int fd, rfd;

	fd = mkstemp(path);
	if (fd < 0)
		return -1;
	close(fd);

	if (pipe_send_input(path, &rfd) != 0) {
		unlink(path);
		return -1;
	}

	if (mr_read_line(rfd, &(mr_file_line_t){0}) != EOF_REACHED) {
		close(rfd);
		unlink(path);
		return -1;
	}
	close(rfd);
	unlink(path);
	return 0;
}

int main(void)
{
	static int (*tests[])(void) = {
		test_single_file,
		test_directory,
		test_empty_file,
	};
	static const char *names[] = {
		"test_single_file",
		"test_directory",
		"test_empty_file",
	};

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		if (tests[i]() != 0) {
			fprintf(stderr, "%s failed\n", names[i]);
			return 1;
		}
	}

	printf("input: tutti i test passati.\n");
	return 0;
}
