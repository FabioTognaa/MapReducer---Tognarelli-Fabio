#include "log.h"

//struttura reale dell'handler opaco
struct mr{
    mr_attr_t attr;         //attributi di configurazione
    mr_mapper_t mapper;     //funzione mapper
    mr_reducer_t reducer;   //funzione reducer
    void *user_arg;         //contesto del mapper e reducer
    mr_log_t log;           //file di log per salvare logs
    int started;            //flag per indicare se il framework è stato avviato
    int error;              //flag per indicare se ci è stato un errore
};