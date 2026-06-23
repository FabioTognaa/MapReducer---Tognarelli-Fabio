#include "mr.h"
#include <stdlib.h>
#include <errno.h>


static int mr_fail_inval(void)
{
	errno = EINVAL;
	return -1;
}

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

int mr_attr_destroy(mr_attr_t *attr)
{
	if (attr == NULL)
		return mr_fail_inval();
	return 0;
}

int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n)
{
	if (attr == NULL || n == 0)
		return mr_fail_inval();

	attr->mapper_threads = n;
	return 0;
}

int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n)
{
	if (attr == NULL || n == 0)
		return mr_fail_inval();

	attr->reducer_threads = n;
	return 0;
}

int mr_attr_set_queue_size(mr_attr_t *attr, size_t n)
{
	if (attr == NULL || n == 0)
		return mr_fail_inval();

	attr->queue_size = n;
	return 0;
}

int mr_attr_set_log_file(mr_attr_t *attr, const char *path)
{
	if (attr == NULL)
		return mr_fail_inval();

	attr->log_file = path;
	return 0;
}

int mr_create(mr_t *mr, const mr_attr_t *attr, mr_mapper_t mapper,
	      mr_reducer_t reducer, void *user_arg)
{
	struct mr *handle;

	if (mr == NULL || attr == NULL || mapper == NULL || reducer == NULL)
		return mr_fail_inval();

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

	if (mr_create_log(&handle->log, (char *)handle->attr.log_file) < 0) {
		free(handle);
		return -1;
	}

	*mr = handle;
	return 0;
}

int mr_destroy(mr_t mr)
{
	if (mr == NULL)
		return mr_fail_inval();

	if (mr->log.fd >= 0)
		mr_log_close(&mr->log);
	free(mr);
	return 0;
}

int mr_start(mr_t mr, const char *input_path, const char *output_path)
{
	if (mr == NULL || input_path == NULL || output_path == NULL)
		return mr_fail_inval();

	errno = ENOSYS;
	return -1;
}
