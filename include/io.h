#ifndef IO_H
#define IO_H

#include <sys/types.h>
#include "../include/mr.h"

#define MR_MAX_TOKEN_LEN   (1 << 20)   /* 1 MiB */
#define MR_MAX_VALUE_LEN   (64 << 20)  /* 64 MiB */
#define MR_MAX_NAME_LEN    (4096)
#define MR_MAX_LINE_LEN    (64 << 20)  /* 64 MiB */

#define ERROR_SYSTEM  (-1)
#define EOF_REACHED   1   /* mr_read_*: 0=letto, 1=EOF, -1=errore */

int mr_validate_len(int len, size_t maxLen);

ssize_t writen(int fd, void *buf, size_t n);
ssize_t readn(int fd, void *buf, size_t n);

int mr_write_pair(int fd, char *token, void *value, size_t value_size, size_t token_size);
int mr_read_pair(int fd, char **token, void **value, size_t *value_size);

int mr_read_line(int fd, mr_file_line_t *out);
int mr_write_line(int fd, size_t file_name_len, char *file_name,
		  unsigned long line_number, char *line, size_t line_len);
int mr_send_input(const char *input_path, int mapper_write_fd);

int mr_read_result(int fd, char **token, void **value, size_t *result_size);
int mr_write_result(int fd, char *token, void *value, size_t value_size, size_t token_size);

#endif
