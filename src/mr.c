#include <mr.h>

int mr_attr_init(mr_attr_t *attr){

    if(attr->mapper_threads != 1){
        attr->mapper_threads = 0;
    } 

    if(attr->reducer_threads != 1){
        attr->reducer_threads = 0;
    } 

    if(attr->queue_size <= 0){
        attr->queue_size = 2;
    }

    if(!attr->log_file){
        attr->log_file = NULL;
    }
    
}

int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n){

    if(attr->mapper_threads <= 0){
        return -1;
    }

    attr->mapper_threads = n;
    return 0;
}


int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n){
    if(attr->mapper_threads <= 0){
        return -1;
    }

    attr->mapper_threads = n;
    return 0;
}
