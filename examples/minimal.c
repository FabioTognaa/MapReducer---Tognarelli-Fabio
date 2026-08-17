/*
 * MINIMAL:
 * Esempio minimo di uso di libmr: pipeline end-to-end con mapper/reducer
 * che non emettono nulla. Uso: ./minimal <input> <output>
 */

#include <stdio.h>
#include "mr.h"

static int mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
	(void)line;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

static int reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){
	(void)token;
	(void)values;
	(void)values_count;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

int main(int argc, char **argv){
	mr_attr_t attr;
	mr_t mr;

	if (argc != 3) {
		fprintf(stderr, "uso: %s <input> <output>\n", argv[0]);
		return 1;
	}

	if (mr_attr_init(&attr) != 0) {
		fprintf(stderr, "mr_attr_init\n");
		return 1;
	}

	if (mr_attr_set_mapper_threads(&attr, 2) != 0) {
		fprintf(stderr, "mr_attr_set_mapper_threads\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_attr_set_reducer_threads(&attr, 2) != 0) {
		fprintf(stderr, "mr_attr_set_reducer_threads\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_attr_set_queue_size(&attr, 64) != 0) {
		fprintf(stderr, "mr_attr_set_queue_size\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_create(&mr, &attr, mapper, reducer, NULL) != 0) {
		fprintf(stderr, "mr_create\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_start(mr, argv[1], argv[2]) != 0) {
		fprintf(stderr, "mr_start\n");
		mr_destroy(mr);
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_destroy(mr) != 0) {
		fprintf(stderr, "mr_destroy\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_attr_destroy(&attr) != 0) {
		fprintf(stderr, "mr_attr_destroy\n");
		return 1;
	}

	return 0;
}
