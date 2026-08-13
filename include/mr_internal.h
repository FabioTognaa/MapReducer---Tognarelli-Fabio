#ifndef MR_INTERNAL_H
#define MR_INTERNAL_H

#include "mr.h"
#include "log.h"

// Record accumulato nel main prima della scrittura ordinata sul file di output.
typedef struct {
	char *token;
	void *res;
	size_t res_len;
	size_t token_len;
} record_from_reducer_t;

// Definizione reale dell'handle opaco mr_t.
struct mr {
	mr_attr_t attr;
	mr_mapper_t mapper;
	mr_reducer_t reducer;
	void *user_arg;
	mr_log_t log;
	int started;
	int error;
};

#endif
