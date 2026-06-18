#include <assert.h>
#include <stdio.h>
#include "mr.h"

static int dummy_mapper(const mr_file_line_t *line, mr_emit_pair_t emit,
			void *emit_arg, void *user_arg)
{
	(void)line;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

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

int main(void)
{
	mr_attr_t attr;
	mr_t mr;

	assert(mr_attr_init(&attr) == 0);
	assert(attr.mapper_threads == 1);
	assert(attr.reducer_threads == 1);
	assert(attr.queue_size > 0);
	assert(attr.log_file == NULL);

	assert(mr_attr_set_mapper_threads(&attr, 4) == 0);
	assert(mr_attr_set_mapper_threads(&attr, 0) == -1);

	assert(mr_attr_set_reducer_threads(&attr, 2) == 0);
	assert(attr.reducer_threads == 2);

	assert(mr_create(&mr, &attr, dummy_mapper, dummy_reducer, NULL) == 0);

	attr.mapper_threads = 99;
	assert(mr->attr.mapper_threads == 4);

	printf("Fase 1: tutti i test passati.\n");

	mr_destroy(mr);
	mr_attr_destroy(&attr);
	return 0;
}
