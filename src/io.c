// File per il trasferimento dei dati tra processi in pipe
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "../include/io.h"




// header serializzato di una riga del main
typedef struct {
    int file_name_len;
    int line_len;
    unsigned long line_number;
} mr_line_header_t;

// header della coppia in uscita dal mapper
typedef struct {
    int token_len;
    int value_len;
} mr_pair_header_t ;

//funzione per validare le lunghezze di token e valore dell'header
int mr_validate_len(int len, size_t maxLen){
    if(len < 0) return ERROR_SYSTEM;
    if((size_t)len > maxLen ) return ERROR_SYSTEM;

    return 0;
}

// In scrittura: scrive un numero di byte fino a left(byte totali del buffer)
ssize_t writen(int fd, void *buf, size_t n){
    char *ptr = buf;      //puntatore al buffer
    size_t left = n;        //byte mancanti
    while(left > 0){
        //scrive w byte in un colpo solo
        ssize_t w = write(fd, ptr, left); 
            
        if(w < 0){
            if(errno == EINTR){
                continue;
            }
            else{
                perror("Errore durante la scrittura della pipe");
                return ERROR_SYSTEM;
            }
        }
        if(!w) {perror("Errore durante la scrittura sulla pipe"); return ERROR_SYSTEM;}
        ptr += w;               //incrementa puntatore al prossimo byte non ancora scritto
        left -= (size_t)w;      //decremento i byte mancanti
    }
    return n;
}


//In lettura con lo stesso concetto
ssize_t readn(int fd, void *buf, size_t n){
    char *ptr = buf;      //puntatore al buffer
    size_t left = n;        //byte mancanti
    while(left > 0){
        //legge w byte in un colpo solo
        ssize_t w = read(fd, ptr, left);
            
        if(w < 0){
            if(errno == EINTR)  //
                continue;
            perror("Errore durante la lettura della pipe");
            return ERROR_SYSTEM;
        }
        if(!w)
            break;
        ptr += (size_t)w;               //incrementa puntatore al prossimo byte non ancora scritto
        left -= (size_t)w;      //decremento i byte mancanti
    }
    return (ssize_t)(n - left);
}

//scrive la coppia <token, value> in uscita dal mapper e la rende pronta da poter essere letta dal reducer - token_size necessario poichè non posso usare strlen(token) e token non è terminato da '\0'
int mr_write_pair(int fd, char *token, void *value, size_t value_size, size_t token_size){

    mr_pair_header_t header;    //header del messaggio

    //riempimento header
    header.value_len = (int)value_size;
    header.token_len = (int)token_size;

    
    //validazione delle lunghezze dell'header
    if((mr_validate_len(header.token_len, MR_MAX_TOKEN_LEN)!= 0 || header.token_len == 0)){
        perror("Errore nell'header in scrittura: lunghezza del token nulla o troppo grande"); return ERROR_SYSTEM;}
    if((mr_validate_len(header.value_len, MR_MAX_VALUE_LEN)!= 0)){perror("Errore nell'header in scrittura: lunghezza troppo grande"); return ERROR_SYSTEM;}

    
    //scrittura dell'header

    ssize_t nh = writen(fd, &header, sizeof(header));   //lunghezza dell'header in scrittura 

    //controllo scrittura dell'header
    if(nh < 0){
        perror("Errore di sistema durante la scrittura del Mapper");
        return ERROR_SYSTEM;
    }

    if((size_t)nh < sizeof(header)){
        perror("Scrittura incompleta dell'header");
        return ERROR_SYSTEM;
    }

    
    //scrittura del token
    ssize_t nt = writen(fd, token, (size_t)header.token_len);
    if(nt <= 0){
        perror("Errore: scrittura del token sulla pipe non riuscita");
        return ERROR_SYSTEM;
    }


    //scrittura del valore
    if(header.value_len > 0){
        ssize_t nv = writen(fd, value, (size_t)header.value_len);
        if(nv <= 0){
            perror("Errore: scrittura del value sulla pipe non riuscita");
            return ERROR_SYSTEM;
        }
    }

    return 0;

}

//formatta la coppia in uscita dal mapper e la rende pronta da poter essere letta dal reducer allocando in memoria i valori raffinati
int mr_read_pair(int fd, char **token, void **value, size_t *value_size){

    mr_pair_header_t header;    

    //controllo numero di byte passati nell'header
    ssize_t nh = readn(fd, &header, sizeof(header));

    //CONTROLLI SU HEADER DEL MESSAGGIO

    if(nh == 0)
        return EOF_REACHED;

    if(nh < 0){
        perror("Errore di sistema durante la lettura");
        return ERROR_SYSTEM;
    }

    if((size_t)nh < sizeof(header)){
        perror("Errore di sistema: lettura dell'header della coppia incompleto");
        return ERROR_SYSTEM;
    }

    if(mr_validate_len(header.token_len, MR_MAX_TOKEN_LEN) != 0 || header.token_len == 0){
        perror("Errore nell'header in lettura: lunghezza del token nulla o troppo grande");
        return ERROR_SYSTEM;
    }
    if(mr_validate_len(header.value_len, MR_MAX_VALUE_LEN) != 0){
        perror("Errore nell'header in lettura: lunghezza del valore troppo grande");
        return ERROR_SYSTEM;
    }
    
    size_t tlen = (size_t)header.token_len;
    size_t vlen = (size_t)header.value_len;

    //buffer per il token
    char* tbuf = malloc(tlen + 1); //lunghezza del token + '\0' di fine stringa
    if(!tbuf){
        perror("Errore allocazione memoria del token buffer");
        return ERROR_SYSTEM;
    }

    void *vbuf = NULL;
    if(vlen > 0){
        vbuf = malloc(vlen);
        if(!vbuf){
            free(tbuf);
            perror("Errore allocazione memoria del value buffer");
            return ERROR_SYSTEM;
        }
    }

    ssize_t nt = readn(fd, tbuf, tlen);
    if(nt < 0 || (size_t)nt < tlen) {
        free(tbuf);
        free(vbuf);
        perror("Errore lettura token dalla pipe");
        return ERROR_SYSTEM;
    }

    if(vlen > 0){
        ssize_t nv = readn(fd, vbuf, vlen);
        if(nv < 0 || (size_t)nv < vlen) {
            free(tbuf);
            free(vbuf);
            perror("Errore lettura value dalla pipe");
            return ERROR_SYSTEM;
        }
    }
    //aggiunta carattere fine stringa ('\0') al token
    tbuf[tlen] = '\0';

    //salvo in memoria i risultati
    *token = tbuf;
    if(vlen > 0){
        *value = vbuf;
    }
    else{
        *value = NULL;
    }
    *value_size = vlen;

    return 0;
}

//LEGGE E FORMATTA LA RIGA SULLA PIPE MAIN -> MAPPER
int mr_read_line(int fd, mr_file_line_t *out){
    
    //leggo il messaggio serializzato in arrivo sul suo header
    mr_line_header_t header;
    ssize_t nh = readn(fd, &header, sizeof(header));

    if(nh == 0)
        return EOF_REACHED;

    if(nh < 0 || (size_t)nh < sizeof(header)){
        perror("Errore di lettura riga nel mapper: lettura dell'header");
        return ERROR_SYSTEM;
    }

    if(header.line_len < 0){
        perror("Errore in lettura linea nel mapper: lunghezza linea non valida");
        return ERROR_SYSTEM;
    }
    if(mr_validate_len(header.file_name_len, MR_MAX_NAME_LEN) != 0 || header.file_name_len == 0){
        perror("Errore in lettura linea nel mapper: nome file non valido o troppo lungo");
        return ERROR_SYSTEM;
    }

    size_t file_name_len = (size_t)header.file_name_len;
    size_t line_len = (size_t)header.line_len;

    char *fbuf = malloc(file_name_len + 1);
    if(!fbuf){
        perror("Errore in lettura linea mapper: allocazione di memoria per file_name non riuscita");
        return ERROR_SYSTEM;
    }

    ssize_t nfn = readn(fd, fbuf, file_name_len);
    if(nfn < 0 || (size_t)nfn < file_name_len){
        free(fbuf);
        perror("Errore in lettura linea mapper: lettura di file_name non riuscita");
        return ERROR_SYSTEM;
    }
    fbuf[file_name_len] = '\0';

    char *lbuf = NULL;
    if(line_len > 0){
        lbuf = malloc(line_len + 1);
        if(!lbuf){
            free(fbuf);
            perror("Errore in lettura linea mapper: allocazione di memoria per line non riuscita");
            return ERROR_SYSTEM;
        }

        ssize_t nl = readn(fd, lbuf, line_len);
        if(nl < 0 || (size_t)nl < line_len){
            free(fbuf);
            free(lbuf);
            perror("Errore in lettura linea mapper: lettura di line non riuscita");
            return ERROR_SYSTEM;
        }
        lbuf[line_len] = '\0';
    }

    out->file_name = fbuf;
    out->file_name_len = file_name_len;
    out->line_number = header.line_number;
    out->line = lbuf;
    out->line_len = line_len;

    return 0;
}


//SCRIVE LA RIGA SERIALIZZATA SULLA PIPE MAIN -> MAPPER
int mr_write_line(int fd, size_t file_name_len, char* file_name, unsigned long line_number, char* line, size_t line_len ){

    //validazione delle lunghezze
    if(mr_validate_len(file_name_len, MR_MAX_NAME_LEN) != 0 || file_name_len == 0){
        perror("Errore in scrittura linea nel main: lunghezza del file_name non valida");
        return ERROR_SYSTEM;
    }
    if(mr_validate_len(line_len, MR_MAX_LINE_LEN) != 0 || line_len < 0){
        perror("Errore in scrittura linea nel main: lunghezza della linea non valida");
        return ERROR_SYSTEM;
    }
    int fn_len = (int)file_name_len;
    int l_len = (int)line_len;



    //spedisco al mapper i dati
    if((writen(fd, &fn_len, sizeof(fn_len))) < 0){
        perror("Errore in scrittura dal main: file_name_len");
        return ERROR_SYSTEM;
    }; //lunghezza del file


    if((writen(fd, &l_len, sizeof(l_len))) < 0){
        perror("Errore in scrittura dal main: line_len");
        return ERROR_SYSTEM;
    }; //lunghezza della linea

    if((writen(fd, &line_number, sizeof(line_number))) == -1){
        perror("Errore in scrittura dal main: line_number");
        return ERROR_SYSTEM;
    }   //numero di linea


    if((writen(fd, file_name, file_name_len)) < 0){   
        perror("Errore in scrittura dal main: file_name");
        return ERROR_SYSTEM;
    }   //nome del file


    if((writen(fd, line, line_len)) < 0){   
        perror("Errore in scrittura dal main: line");
        return ERROR_SYSTEM;
    }   //contenuto della linea

    return 0;
}


//LEGGERE E FORMATTA SULLA PIPE REDUCER -> MAIN
int mr_read_result(int fd, char **token, void **value, size_t *result_size){
    mr_pair_header_t header;

    ssize_t nh = readn(fd, &header, sizeof(header));

    if(nh == 0)
        return EOF_REACHED;

    if(nh < 0){
        perror("Errore: header in lettura sulla pipe reducer -> main non valido");
        return ERROR_SYSTEM;
    }

    if((size_t)nh < sizeof(header)){
        perror("Errore: lettura incompleta dell'header sulla pipe reducer -> main");
        return ERROR_SYSTEM;
    }

    //validazione delle lunghezze
    if(mr_validate_len(header.token_len, MR_MAX_TOKEN_LEN) != 0 || header.token_len == 0){
        perror("Errore: lunghezza token in lettura sulla pipe reducer -> main non valido");
        return ERROR_SYSTEM;
    }
    
    if(mr_validate_len(header.value_len, MR_MAX_VALUE_LEN) != 0){
        perror("Errore: lunghezza token in lettura sulla pipe reducer -> main non valido");
        return ERROR_SYSTEM;
    }

    //nbyte delle lunghezze
    size_t tlen = (size_t)header.token_len;
    size_t vlen = (size_t)header.value_len;


    //buffer per il token
    char* tbuf = malloc(tlen + 1); //lunghezza del token + '\0' di fine stringa
    if(!tbuf){
        perror("Errore: allocazione memoria del token buffer non riuscita sulla pipe reducer -> main");
        return ERROR_SYSTEM;
    }

    //buffer per il valore
    void *vbuf = NULL;
    if(vlen > 0){
        vbuf = malloc(vlen);
        if(!vbuf){
            free(tbuf);
            perror("Errore: allocazione memoria del value buffer non riuscita sulla pipe reducer -> main");
            return ERROR_SYSTEM;
        }
    }

    
    ssize_t nt = readn(fd, tbuf, tlen);
    if(nt < 0 || (size_t)nt < tlen) {
        free(tbuf);
        free(vbuf);
        perror("Errore: lettura token sulla pipe reducer -> main non riuscita");
        return ERROR_SYSTEM;
    }

    if(vlen > 0){
        ssize_t nv = readn(fd, vbuf, vlen);
        if(nv < 0 || (size_t)nv < vlen) {
            free(tbuf);
            free(vbuf);
            perror("Errore: lettura value sulla pipe reducer -> main non riuscita");
            return ERROR_SYSTEM;
        }
    }

    //allocazione di token e di value
    tbuf[tlen] = '\0';
    *token = tbuf;

    if(vlen > 0){
        *value = vbuf;
    }else{
        *value = NULL;
    }
    *result_size = vlen;

    return 0;
}

//SCRIVE SULLA PIPE REDUCER -> MAIN
int mr_write_result(int fd, char *token, void *value, size_t value_size, size_t token_size){

    mr_pair_header_t header;

    header.token_len = (int)token_size;
    header.value_len = (int)value_size;

    //validazione header
    if(mr_validate_len(header.token_len, MR_MAX_TOKEN_LEN) != 0 || header.token_len == 0){
        perror("Errore: validazione token in scrittura sulla pipe reducer->main");
        return ERROR_SYSTEM;
    }

    if(mr_validate_len(header.value_len, MR_MAX_VALUE_LEN) != 0){
        perror("Errore: validazione value in scrittura sulla pipe reducer->main");
        return ERROR_SYSTEM;
    }

    //scrittura dell'header sulla pipe
    ssize_t nh = writen(fd, &header, sizeof(header));
    if(nh < 0){
        perror("Errore: scrittura dell'header sulla pipe reducer -> main non riuscita");
        return ERROR_SYSTEM;
    }

    if((size_t)nh < sizeof(header)){
        perror("Errore: scrittura incompleta dell'header sulla pipe reducer -> main");
        return ERROR_SYSTEM;
    }


    //scrittura del token
    if((writen(fd, token, (size_t)header.token_len)) <= 0){
        perror("Errore: scrittura del token sulla pipe reducer -> main non riuscita");
        return ERROR_SYSTEM;
    }

    //scrittura del value
    if(header.value_len != 0){
        if((writen(fd, value, (size_t)header.value_len)) <= 0){
            perror("Errore: scrittura del value sulla pipe reducer -> main non riuscita");
            return ERROR_SYSTEM;
        }
    }

   return 0;
}