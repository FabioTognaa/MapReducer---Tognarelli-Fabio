#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "io.h"

//testa il funzionamento di readn e writen
static int test_readn_writen(void){

    int p[2];
    char buf[16] = {0};

    if (pipe(p) != 0)
        return -1;

    //testo la scrittura di 'abc' in output
    if(writen(p[1], "abc", 3) != 3){
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[1]);

    //testo la lettura di byte
    if(readn(p[0], buf, 10) != 3){
        close(p[0]);
        return -1;
    }
    //controllo che il messaggio inviato sia uguale
    if(memcmp(buf, "abc", 3) != 0){
        close(p[0]);
        return -1;
    }
    //leggo EOF
    if(readn(p[0], buf, 1) != 0){
        close(p[0]);
        return -1;
    }

    close(p[0]);
    return 0;
}

//testa il funzionamento di mr_read_line e mr_write_line
static int test_line(void){

    int p[2];
    char buf[16];
    mr_file_line_t l = {0};  //riga vuota

    //pipe
    if(pipe(p) != 0)
        return -1;

    //scrivo una riga pre-impostata sulla pipe
    if(mr_write_line(p[1], 4, "test", 1, "hello", 5) != 0){
        close(p[0]);
        close(p[1]);
        return -1;
    } 
    close(p[1]);

    //leggo dalla pipe la riga
    if(mr_read_line(p[0], &l) != 0){
        close(p[0]);
        return -1;
    }

    //controllo che infondo restituisca EOF
    if(readn(p[0], buf, 1) != 0){
        close(p[0]);    
        return -1;
    }
    close(p[0]);

    
    //se le lunghezze sono corrette
    if(l.file_name_len != 4 || l.line_number != 1 || l.line_len != 5){
        free((void *)l.file_name);
        free((void *)l.line);
        return -1;
    }
    
    //se la riga e il nome del file sono corretti
    if((memcmp(l.file_name, "test", 4) != 0) || (memcmp(l.line, "hello", 5) != 0)){
        free((void *)l.file_name);
        free((void *)l.line);
        return -1;
    }

    //cleanup memoria
    free((void *)l.file_name);
    free((void *)l.line);


    //------------------------------------------------------
    //STESSO TEST CON RIGA VUOTA
    //pipe
    if(pipe(p) != 0)
        return -1;

    //scrivo una riga pre-impostata sulla pipe
    if(mr_write_line(p[1], 4, "test", 1, "", 0) != 0){
        close(p[0]);
        close(p[1]);
        return -1;
    } 
    close(p[1]);

    //leggo dalla pipe la riga
    if(mr_read_line(p[0], &l) != 0){
        close(p[0]);
        return -1;
    }

    //controllo che infondo restituisca EOF
    if(readn(p[0], buf, 1) != 0){
        close(p[0]);    
        return -1;
    }
    close(p[0]);

    
    //se le lunghezze sono corrette
    if(l.file_name_len != 4 || l.line_number != 1 || l.line_len != 0){
        free((void *)l.file_name);
        free((void *)l.line);
        return -1;
    }
    
    //se la riga e il nome del file sono corretti
    if((memcmp(l.file_name, "test", 4) != 0)){
        free((void *)l.file_name);
        free((void *)l.line);
        return -1;
    }

    //cleanup memoria
    free((void *)l.file_name);
    free((void *)l.line);

    return 0;
}

static int test_pair(void){

    int p[2];
    char *token = NULL;
    void *value = NULL;
    size_t value_size = 0;
    int v = 42;
    static unsigned char binary_val[] = { 1, 0, 2 };
    
    if(pipe(p) != 0) 
        return -1;


    //scrivo la coppia <hello, 42> sulla pipe
    if(mr_write_pair(p[1], "hello", &v, sizeof(int), strlen("hello")) != 0){
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[1]);


    //lettura sulla pipe di <hello, 42>
    if(mr_read_pair(p[0], &token, &value, &value_size) != 0){
        close(p[0]);
        return -1;
    }
    close(p[0]);

    //controlli
    if(strcmp(token, "hello") != 0 || (value_size != sizeof(int)) || (*(int*)value != 42)){
        free(token);
        free(value);
        return -1;
    }

    //CASO VALUE VUOTO
    free(token);
    free(value);
    value = NULL;
    token = NULL;
    value_size = 0;

    if(pipe(p) != 0) 
        return -1;

    //scrivo la coppia <hello, NULL> sulla pipe
    if(mr_write_pair(p[1], "hello2", NULL, 0, strlen("hello2")) != 0){
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[1]);

    //leggo la coppia
    if(mr_read_pair(p[0], &token, &value, &value_size) != 0){
        close(p[0]);
        return -1;
    }
    close(p[0]);

    //confronto
    if((strcmp(token, "hello2") != 0) || (value_size != 0) || (value != NULL)){
        free(token);
        free(value);
        return -1;
    }

    //*CASO VALORE BINARIO
    free(token);
    free(value);
    token = NULL;
    value = NULL;
    value_size = 0;
    if (pipe(p) != 0)
        return -1;

    if (mr_write_pair(p[1], "ab", binary_val, 3, strlen("ab")) != 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[1]);

    if (mr_read_pair(p[0], &token, &value, &value_size) != 0) {
        close(p[0]);
        return -1;
    }
    close(p[0]);

    if (strcmp(token, "ab") != 0 ||
        value_size != 3 ||
        memcmp(value, binary_val, 3) != 0) {
        free(token);
        free(value);
        return -1;
    }

    free(token);
    free(value);
    return 0;
}

//test per mr_write/read_result su una coppia predefinita <hello, 42>
static int test_result(void){

    int p[2];
    char *token = NULL;
    void *value = NULL;
    size_t value_size = 0;
    int v= 42;
    
    if(pipe(p) != 0)
        return -1;

    //scrittura reducer -> main
    if(mr_write_result(p[1], "hello", &v, sizeof(v), strlen("hello")) != 0){
        free(token);
        free(value);
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[1]);

    //lettura reducer -> main
    if(mr_read_result(p[0], &token, &value, &value_size) != 0){
        free(token);
        free(value);
        close(p[0]);
        return -1;
    }
    close(p[0]);

    //controllo correttezza
    if((value_size != sizeof(int)) || (memcmp(token, "hello",  sizeof("hello")) != 0) || (*(int*)value != 42)){
        free(token);
        free(value);
        return -1;
    }

    free(token);
    free(value);
    token = NULL;
    value = NULL;
    value_size = 0;

    //--------------------------------
    //TEST CON UN RISULTATO NULLO

    //pipe
    if(pipe(p) != 0)
        return -1;

    // passo token con valore nullo
    if(mr_write_result(p[1], "hello", NULL, 0, strlen("hello")) != 0){
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[1]);

    //lettura reducer -> main
    if(mr_read_result(p[0], &token, &value, &value_size) != 0){
        close(p[0]);
        return -1;
    }
    close(p[0]);

    //controllo correttezza del messaggio su pipe
    if((strcmp(token, "hello") != 0) || (value_size != 0) || (value != NULL)){
        free(token);
        free(value);
        return -1;
    }

    free(token);
    free(value);
    return 0;
}

//test ERRORI
//eof raggiunto
static int test_eof(void){
    int p[2];
    char *token = NULL;
    void *value = NULL;
    size_t value_size = 0;
    mr_file_line_t file_line = {0};

    
    if(pipe(p) != 0)
        return -1;

    //chiudo in output simulando eof raggiunto
    close(p[1]);

    //provo a leggere la linea
    if(mr_read_line(p[0], &file_line) != EOF_REACHED){
        close(p[0]);
        return -1;
    }
    //provo a leggere la coppia
    if(mr_read_pair(p[0], &token, &value, &value_size) != EOF_REACHED){
        close(p[0]);
        return -1;
    }

    //provo a leggere il risultato
    if(mr_read_result(p[0], &token, &value, &value_size) != EOF_REACHED){
        close(p[0]);
        return -1;
    }

    close(p[0]);
    return 0;
}

//controlla se l'header e' stato letto solo parzialmente e chiuso a meta'
static int test_truncated_header(void){
    int p[2];
    char *token = NULL;
    void *value = NULL;
    size_t value_size = 0;
    if (pipe(p) != 0)
        return -1;
    // mr_pair_header_t è 8 byte (2 int); ne scrivo solo 2 
    writen(p[1], "xx", 2);
    close(p[1]);
    errno = 0;
    if (mr_read_pair(p[0], &token, &value, &value_size) != -1) {
        close(p[0]);
        return -1;
    }
    if (errno != EINVAL) {   //si controlla la corretta impostazione di errno
        close(p[0]);
        return -1;
    }
    close(p[0]);
    return 0;
}   

//controlla che il la lunghezza del token sia nulla per un value non nullo
static int test_write_pair_invalid_token(void){
    int p[2];
    int v = 1;
    if (pipe(p) != 0)
        return -1;
    errno = 0;

    //lunghezza del value esplicita sbagliata
    if (mr_write_pair(p[1], "hello", &v, sizeof(v), 0) != -1) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    //controllo errno
    if (errno != EINVAL) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    close(p[0]);
    close(p[1]);
    return 0;
}

//simula un invio in scrittura di un header completo e di un token troncato
static int test_truncated_payload(void){
    int p[2];
    int token_len = 5;
    int value_len = 0;
    char *token = NULL;
    void *value = NULL;
    size_t value_size = 0;

    //pipe
    if (pipe(p) != 0)
        return -1;

    // header come due int (layout di mr_pair_header_t in src/io.c, non esposto)
    writen(p[1], &token_len, sizeof(token_len));
    writen(p[1], &value_len, sizeof(value_len));
    writen(p[1], "ab", 2);   /* token troncato: 2 byte invece di 5 */
    close(p[1]);
    errno = 0;

    //tentativo di lettura
    if (mr_read_pair(p[0], &token, &value, &value_size) != -1) {
        close(p[0]);
        return -1;
    }
    if (errno != EINVAL) {
        close(p[0]);
        return -1;
    }
    close(p[0]);
    return 0;
}

//testa il validate_len per la validazione delle lunghezze degli headers
static int test_validate_len(void){

    //se len < 0
    if(mr_validate_len(-1, 100) != -1)
        return -1;

    //se len > maxLen
    if(mr_validate_len(100, 50) != -1)
        return -1;

    //se mr_validate_len() fallisce in modo anomalo
    if(mr_validate_len(50, 100) != 0)
        return -1;

    return 0;
}

//MAIN con chiamata di ogni test
int main(void){
    if( test_readn_writen() != 0){
        fprintf(stderr, "test su readn/writen fallito \n");
        return -1;
    }
    if( test_line() != 0){
        fprintf(stderr, "test su test_line fallito \n");
        return -1;
    }
    if( test_pair() != 0){
        fprintf(stderr, "test su test_pair fallito \n");
        return -1;
    }
    if( test_result() != 0){
        fprintf(stderr, "test su test_result fallito \n");
        return -1;
    }
    
    if( test_eof() != 0){
        fprintf(stderr, "test su test_eof fallito \n");
        return -1;
    }
    
    if( test_truncated_header() != 0){
        fprintf(stderr, "test su passaggio header troncato fallito \n");
        return -1;
    }
    
    if( test_write_pair_invalid_token() != 0){
        fprintf(stderr, "test su value_size = 0 quando value != NULL fallito \n");
        return -1;
    }
    
    if( test_truncated_payload() != 0){
        fprintf(stderr, "test su passaggio di un token troncato fallito \n");
        return -1;
    }
    
    if( test_validate_len() != 0){
        fprintf(stderr, "test su validate_len fallito \n");
        return -1;
    }

    printf("io.c: tutti i test passati\n");
    return 0;
}