//implementa le funzioni per la lettura delle righe del file/directory di input e la scrittura sulla pipe verso il processo mapper

#define _GNU_SOURCE // serve per poter usare getline

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "io.h"
#include "mr.h"


#define IS_FILE 1
#define IS_DIR 2
#define SOM_ELSE 3

//CONTROLLA SE L'INPUT E' UN FILE O UNA DIRECTORY
int check_path(const char *path){
    struct stat info_path;

    //controllo se il path esiste ed è accessibile
    if(stat(path, &info_path) != 0){
        return -1;
    }

    //controllo se il path fa riferimento ad un file o una dir
    if(S_ISREG(info_path.st_mode))
        return IS_FILE;
    else if(S_ISDIR(info_path.st_mode))
        return IS_DIR;
    else
        return SOM_ELSE;
}

//ESTRAE DAL PATH IL NOME DI UN FILE/DIR
static char * str_extract(const char *path){
    // Cerca l'ultimo '/' nel path
    char *file_name = strrchr(path, '/');
    
    if (file_name != NULL)
        return file_name + 1; 
    
    return (char*)path;
}

//FUNZIONE PER ORDINARE IL BUFFER DI FILE DELLA DIR
static int cmp_str(const void *a, const void *b){
    return strcmp(*(const char * const *)a, *(const char * const *)b);
}
//ESPLORA UN FILE ED INVIA AL MAPPER UNA RIGA ALLA VOLTA
static int send_one_file(const char *file_path, int mapper_write_fd){
    //apertura del file
    int fd;
    if((fd = open(file_path, O_RDONLY)) == -1){
        perror("Errorre: open() del file in input non riuscita");
        return -1;
    }

    FILE *fp;
    if (!(fp = fdopen(fd, "r"))){
        perror("Errore: fdopen del file in input non riuscita");
        close(fd);
        return -1;
    }

    //loop con getline per ottenere le righe ed inviarle serializzate alla pipe
    char *buff = NULL;
    size_t dim = 0;
    ssize_t char_letti;
    unsigned long l_number = 1;
    const char *fname = str_extract(file_path);
    size_t fname_len = strlen(fname);       


    //finche' non arrivo a EOF
    while((char_letti = getline(&buff, &dim, fp)) != -1){
        size_t len = (size_t)char_letti;  
        if(len > 0 && buff[len - 1] == '\n') len--; //rimuovo '\n'
        
        //scrivo sulla pipe la riga
        if(mr_write_line(mapper_write_fd, fname_len, (char *)fname, l_number, buff, len) != 0){
            free(buff);
            fclose(fp);
            perror("Errore: scrittura del file di input su pipe verso il mapper");
            return -1;
        } 
        l_number++;
    }
    
    //chiudo tutto
    free(buff);
    fclose(fp);
    return 0;
}

//INVIA LE RIGHE DI INPUT AL MAPPER
int mr_send_input(const char *input_path, int mapper_write_fd){

    //determina il tipo di input_path
    int path_type;
    if((path_type = check_path(input_path)) == -1){
        perror("Errore: path in ingresso non valido");
        return -1;
    }

    //caso file
    if(path_type == IS_FILE){
        int ris;
        if((ris = send_one_file(input_path, mapper_write_fd)) != 0)
            return -1;
        if (close(mapper_write_fd) == -1){
            perror("Errore: chiusura del mapper non riuscita");
            return -1;
        }
        return 0;
    }
    
    //caso dir
    if(path_type == IS_DIR){

        //apri la dir
        DIR *dir;
        struct dirent *file;
        if(!(dir = opendir(input_path))){
            perror("Errore: impossibile aprire la dir in input");
            return -1;
        }

        //leggi la dir
        char** names = NULL;
        size_t count = 0, cap = 0;
        char full_path[1024];
        while((file = readdir(dir)) != NULL){
            //evita . e ..
            if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) continue;
            
            //evita file indesiderati
            snprintf(full_path, sizeof(full_path), "%s/%s", input_path, file->d_name);
            if (check_path(full_path) != IS_FILE)
                continue;

            //salva in un array riallocando dinamicamente la lunghezza in base alla quantita' di file presenti nella cartella
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                char **tmp = realloc(names, cap * sizeof(char *));
                if (!tmp) {
                    for (size_t k = 0; k < count; k++)
                        free(names[k]);
                    free(names);
                    closedir(dir);
                    return -1;
                }
                names = tmp;
            }
    
            //aggiungo al buffer
            names[count++] = strdup(file->d_name); 
        }
        //chiusura della dir
        closedir(dir);
            
        //ordinamento alfanumerico dei file nel buffer
        qsort(names, count, sizeof(char*), cmp_str);

        //lettura file per file 
        for (size_t i = 0; i < count; i++) {
            snprintf(full_path, sizeof(full_path), "%s/%s", input_path, names[i]);
            if (send_one_file(full_path, mapper_write_fd) != 0) {
                for(size_t j = i; j < count; j++){
                    free(names[j]);
                }
                free(names);
                perror("Errore: uno dei file della dir non e' stato processato correttamente");
                return -1;
            }
            //libero lo spazio di quel file una volta finito il suo processamento
            free(names[i]);
        }
        free(names);
        if (close(mapper_write_fd) == -1){ 
            perror("Errore: chiusura della pipe in scrittura verso il mapper non riuscita");
            return -1; 
        }
        
        return 0;
    }

    if(path_type == SOM_ELSE){
        perror("Errore: file di dati in iinput non valido");
        return -1;
    }

    return 0;
}