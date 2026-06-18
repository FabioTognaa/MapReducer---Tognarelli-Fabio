//struttura reale dell'handler opaco
struct mr{
    mr_attr_t attr;
    mr_mapper_t mapper;
    mr_reducer_t reducer;
    void *user_arg;
    int started;
    int error;
};