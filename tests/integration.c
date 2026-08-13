#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "mr.h"
#include "mr_internal.h"
#include "io.h"

/* numero massimo di record letti da un file di output nei test */
#define MAX_RESULTS 64

//crea un file di input temporaneo
static int make_input_file(char *path, char *content){

	int fd;
	if(!path)
		return -1;

	//creo file temporaneo
	if((fd = mkstemp(path)) < 0)
		return -1;

	//scrivo il contenuto nel file
	if(content){
		ssize_t n = write(fd, content, strlen(content));
		if(n < 0 || (size_t)n != strlen(content)){
			close(fd);
			unlink(path);
			return -1;
		}
	}

	close(fd);
	//ritorna
	return 0;
}

//crea una dir di input temporanea con test minimo su file multipli
static int make_input_dir(char path[], char *file1, char *file2, char *content1, char *content2){

	//crea la cartella
	char *dir = mkdtemp(path);

	if(!dir)
		return -1;

	//crea al suo interno 2 file
		//costruisco il path dentro la cartella
		char buf1[256];
		char buf2[256];
		int path1 = snprintf(buf1, sizeof(buf1), "%s/%s", dir, file1);
		int path2 = snprintf(buf2, sizeof(buf2), "%s/%s", dir, file2);
		if(path1 < 0 || path2 < 0 || (size_t)path1 >= sizeof(buf1) || (size_t)path2 >= sizeof(buf2)){
			rmdir(dir);
			return -1;
		}

		//apro i 2 file dentro la cartelle e scrivo al loro interno
		FILE *fd1 = fopen(buf1, "w");
		if(!fd1){
			rmdir(dir);
			return -1;
		}

		FILE *fd2 = fopen(buf2, "w");
		if(!fd2){
			fclose(fd1);
			unlink(buf1);
			rmdir(dir);
			return -1;
		}

		if(content1){
			if(fprintf(fd1, "%s", content1) < 0){
				fclose(fd1);
				fclose(fd2);
				unlink(buf1);
				unlink(buf2);
				rmdir(dir);
				return -1;
			}
		}
		fclose(fd1);

		if(content2){
			if(fprintf(fd2, "%s", content2) < 0){
				fclose(fd2);
				unlink(buf2);
				unlink(buf1);
				rmdir(dir);
				return -1;
			}
		}
		fclose(fd2);

	//ritorna
	return 0;
}

//rimuove i 2 file e la directory creati da make_input_dir
static void cleanup_input_dir(char *dir, char *file1, char *file2){
	char buf1[256];
	char buf2[256];

	snprintf(buf1, sizeof(buf1), "%s/%s", dir, file1);
	snprintf(buf2, sizeof(buf2), "%s/%s", dir, file2);
	unlink(buf1);
	unlink(buf2);
	rmdir(dir);
}

//crea un path di log temporaneo (file creato e subito cancellato: resta solo il nome)
static int make_log_path(char *path){
	int fd;

	if((fd = mkstemp(path)) < 0)
		return -1;
	close(fd);
	unlink(path);
	return 0;
}

//crea un file di output temporaneo vuoto (chiude subito l'fd)
static int make_output_file(char *path){
	int fd;

	if((fd = mkstemp(path)) < 0)
		return -1;
	close(fd);
	return 0;
}

//funzione per semplificare il processo di: mr_create -> mr_start -> mr_destroy
//(attr gia' inizializzato e configurato dal chiamante; il file di output resta sul disco)
static int mr_run(mr_attr_t *attr, mr_t *mr, const char *input_path, const char *output_path, mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg){

	//mr_create
	if(mr_create(mr, attr, mapper, reducer, user_arg) != 0)
		return -1;

	//mr_start
	if(mr_start(*mr, input_path, output_path) != 0){
		mr_destroy(*mr);
		return -1;
	}

	//mr_destroy
	if(mr_destroy(*mr) != 0)
		return -1;

	return 0;
}

//libera i record letti da read_all_results
static void free_results(record_from_reducer_t *recs, size_t n){
	size_t i;

	for(i = 0; i < n; i++){
		free(recs[i].token);
		free(recs[i].res);
		recs[i].token = NULL;
		recs[i].res = NULL;
	}
}

//legge tutti i risultati dopo una run di mr. Apre il file .mro e legge tutti i record con mr_read_result (io.h).
static int read_all_results(const char *path, record_from_reducer_t *out, size_t max_n, size_t *out_n){
	int fd;
	size_t n = 0;
	char *token = NULL;
	void *value = NULL;
	size_t result_size = 0;
	int rc;

	//valori in input non validi
	if(!path || !out || !out_n || max_n == 0)
		return -1;

	//apre il file in lettura
	if((fd = open(path, O_RDONLY)) < 0)
		return -1;

	//loop di lettura dei record
	while(n < max_n){
		token = NULL;
		value = NULL;
		result_size = 0;

		rc = mr_read_result(fd, &token, &value, &result_size);
		if(rc == EOF_REACHED)
			break;
		if(rc != 0){
			free_results(out, n);
			close(fd);
			return -1;
		}
		//incapsula il record
		out[n].token = token;
		out[n].res = value;
		out[n].res_len = result_size;
		out[n].token_len = strlen(token);
		n++;
	}

	/* se c'e' ancora un record oltre max_n, il test e' troppo stretto */
	token = NULL;
	value = NULL;
	result_size = 0;
	rc = mr_read_result(fd, &token, &value, &result_size);
	if(rc != EOF_REACHED){
		free(token);
		free(value);
		free_results(out, n);
		close(fd);
		return -1;
	}

	close(fd);
	*out_n = n;
	return 0;
}

//confronta due file per controllare determinismo degli output
//ritorna: 0 uguali, 1 diversi, -1 errore
static int cmp_files(const char *path_a, const char *path_b){
	FILE *fa;
	FILE *fb;
	char bufa[4096];
	char bufb[4096];
	size_t na;
	size_t nb;

	//apre i file in lettura
	fa = fopen(path_a, "rb");
	if(!fa)
		return -1;

	fb = fopen(path_b, "rb");
	if(!fb){
		fclose(fa);
		return -1;
	}

	//loop di confronto tra i 2 file
	while(1){
		//copio il contenuto dei 2 file in 2 buffer
		na = fread(bufa, 1, sizeof(bufa), fa);
		nb = fread(bufb, 1, sizeof(bufb), fb);

		//se le lunghezze sono diverse
		if(na != nb){
			fclose(fa);
			fclose(fb);
			return 1;
		}

		//se si e' arrivati a EOF: i file sono uguali
		if(na == 0){
			if(ferror(fa) || ferror(fb)){
				fclose(fa);
				fclose(fb);
				return -1;
			}
			fclose(fa);
			fclose(fb);
			return 0;
		}

		//se i byte sono diversi
		if(memcmp(bufa, bufb, na) != 0){
			fclose(fa);
			fclose(fb);
			return 1;
		}
	}
}

//controlla ogni singolo valore di un record per word-count 
static int check_int_record(record_from_reducer_t *rec, const char *exp_token, int exp_val){
	if(!rec || !rec->token || !rec->res)
		return -1;
	if(strcmp(rec->token, exp_token) != 0)
		return -1;
	if(rec->res_len != sizeof(int))
		return -1;
	if(memcmp(rec->res, &exp_val, sizeof(int)) != 0)
		return -1;
	return 0;
}

//funzione mapper word-count di test: emette un token per ogni parola che trova sulla linea di quel file
static int word_count_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
	size_t i;
	size_t start = 0;
	int in_token = 0;

	(void)user_arg;

	//scorre la linea del file
	for(i = 0; i < line->line_len; i++){

		//var per la lettera
		char letter = line->line[i];
		int is_alnum = (letter >= 'a' && letter <= 'z')
			|| (letter >= 'A' && letter <= 'Z')
			|| (letter >= '0' && letter <= '9');

		//se non ho ancora un token
		if(in_token == 0){
			//se alfanumerico
			if(is_alnum){
				start = i;
				in_token = 1;
			}
			continue;
		}

		//se sono ad un non-alfanumerico
		if(!is_alnum){
			//costruisco spazio per il token
			size_t token_len = i - start;
			char *token_buf = malloc(token_len + 1);
			int one = 1;

			//controllo su token_buf
			if(!token_buf)
				return -1;
			memcpy(token_buf, line->line + start, token_len);
			token_buf[token_len] = '\0';
			//mando in output la coppia
			if(emit(token_buf, &one, sizeof(one), emit_arg) != 0){
				free(token_buf);
				return -1;
			}
			free(token_buf);
			in_token = 0;
		}
	}

	//se sono arrivato in fondo e l'ultimo carattere compone un token
	if(in_token){
		size_t token_len = line->line_len - start;
		char *token_buf = malloc(token_len + 1);
		int one = 1;

		//controllo su token_buf
		if(!token_buf)
			return -1;
		memcpy(token_buf, line->line + start, token_len);
		token_buf[token_len] = '\0';
		//mando in output la coppia
		if(emit(token_buf, &one, sizeof(one), emit_arg) != 0){
			free(token_buf);
			return -1;
		}
		free(token_buf);
	}
	//quando trova un carattere alfanumerico (a-z; A-Z; 0-9) inizia ad annotare il token fino al primo non-alfanumerico che incontra

	return 0;
}

//funzione reducer word-count di test: per ogni word-token conta il corrispettivo numero di occorrenze e lo scrive in output
static int word_count_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){

	//emette il numero di occorrenze con il rispettivo token associato
	int total = (int)values_count;

	(void)values;
	(void)user_arg;

	if(emit(token, &total, sizeof(total), emit_arg) != 0)
		return -1;
	return 0;
}

//Mapper che emette valori binari opachi e non interpreta 
static int opaque_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
	unsigned char buf[3];

	(void)user_arg;

	if(line->line_len == 0)
		return 0;

	buf[0] = 'A';
	buf[1] = 0;
	buf[2] = 'B';
	return emit("tok", buf, sizeof(buf), emit_arg);
}

//* Reducer: combina i byte senza nessuna funzione built-in sui values seza interpretare processed_token.
static int opaque_reducer(const char *token, const mr_value_t *values, size_t values_count,
			  mr_emit_result_t emit, void *emit_arg, void *user_arg){
	size_t i;
	size_t total = 0;
	size_t off = 0;
	unsigned char *out;

	(void)user_arg;

	for(i = 0; i < values_count; i++)
		total += values[i].size;

	out = malloc(total == 0 ? 1 : total);
	if(!out)
		return -1;

	for(i = 0; i < values_count; i++){
		if(values[i].size > 0 && values[i].data != NULL){
			memcpy(out + off, values[i].data, values[i].size);
			off += values[i].size;
		}
	}

	if(emit(token, out, total, emit_arg) != 0){
		free(out);
		return -1;
	}
	free(out);
	return 0;
}

// mapper silenzioso che non chiama mai emit. Usato per verificare che la pipeline termini anche senza coppie intermedie
static int silent_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
	(void)line;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

//reducer dummy (per test con reducer silenzioso)
static int dummy_reducer(const char *token, const mr_value_t *values, size_t values_count,
			 mr_emit_result_t emit, void *emit_arg, void *user_arg){
	(void)token;
	(void)values;
	(void)values_count;
	(void)emit;
	(void)emit_arg;
	(void)user_arg;
	return 0;
}

// Smoke test principale end-to-end di tutto il progetto
static int test_integration_word_count(void){
	//setup del mr
	mr_t mr = NULL;
	mr_attr_t attr;
	char input_path[] = "/tmp/mr_int_in_XXXXXX";
	char output_path[] = "/tmp/mr_int_out_XXXXXX";
	char log_path[] = "/tmp/mr_int_log_XXXXXX";
	record_from_reducer_t recs[MAX_RESULTS];
	size_t n = 0;

	//inizializzazione di attr e del log file
	if(mr_attr_init(&attr) != 0)
		return -1;

	if(make_log_path(log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	//setup di attr
	if(mr_attr_set_mapper_threads(&attr, 2) != 0
	   || mr_attr_set_reducer_threads(&attr, 2) != 0
	   || mr_attr_set_queue_size(&attr, 8) != 0
	   || mr_attr_set_log_file(&attr, log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	//creazione file di input
	if(make_input_file(input_path, "bb aa\n aa\n") != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	//creazione file di output
	if(make_output_file(output_path) != 0){
		unlink(input_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//mr_create + mr_start + mr_destroy
	if(mr_run(&attr, &mr, input_path, output_path, word_count_mapper, word_count_reducer, NULL) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//controllo dei risultati finali in base ai dati forniti in input
	if(read_all_results(output_path, recs, MAX_RESULTS, &n) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//controllo generale dei record forniti in output
	if(n != 2
	   || check_int_record(&recs[0], "aa", 2) != 0
	   || check_int_record(&recs[1], "bb", 1) != 0){
		free_results(recs, n);
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//cleanup generale
	free_results(recs, n);
	unlink(input_path);
	unlink(output_path);
	unlink(log_path);
	//mr_attr_destroy
	mr_attr_destroy(&attr);
	return 0;
}

// stesso test end-to-end ma con una dir come input:
static int test_integration_directory(void){
	mr_t mr = NULL;
	mr_attr_t attr;
	char dir_path[] = "/tmp/mr_int_dir_XXXXXX";
	char output_path[] = "/tmp/mr_int_dout_XXXXXX";
	char log_path[] = "/tmp/mr_int_dlog_XXXXXX";
	record_from_reducer_t recs[MAX_RESULTS];
	size_t n = 0;

	if(mr_attr_init(&attr) != 0)
		return -1;
	if(make_log_path(log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(mr_attr_set_mapper_threads(&attr, 2) != 0
	   || mr_attr_set_reducer_threads(&attr, 2) != 0
	   || mr_attr_set_queue_size(&attr, 8) != 0
	   || mr_attr_set_log_file(&attr, log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	/* b prima di a su disco: l'ordine di lettura deve essere per nome (a poi b) */
	if(make_input_dir(dir_path, "b.txt", "a.txt", "x\n", "y\n") != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	if(make_output_file(output_path) != 0){
		cleanup_input_dir(dir_path, "b.txt", "a.txt");
		mr_attr_destroy(&attr);
		return -1;
	}

	if(mr_run(&attr, &mr, dir_path, output_path, word_count_mapper, word_count_reducer, NULL) != 0){
		cleanup_input_dir(dir_path, "b.txt", "a.txt");
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(read_all_results(output_path, recs, MAX_RESULTS, &n) != 0){
		cleanup_input_dir(dir_path, "b.txt", "a.txt");
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	/* token ordinati: x poi y */
	if(n != 2
	   || check_int_record(&recs[0], "x", 1) != 0
	   || check_int_record(&recs[1], "y", 1) != 0){
		free_results(recs, n);
		cleanup_input_dir(dir_path, "b.txt", "a.txt");
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	free_results(recs, n);
	cleanup_input_dir(dir_path, "b.txt", "a.txt");
	unlink(output_path);
	unlink(log_path);
	mr_attr_destroy(&attr);
	return 0;
}

//test che controlla, a parità di input, su due esecuzioni, che il risultato sia uguale e quindi deterministico. gli input sono li stessi del primo test, ma abbiamo due mr distinti. in fondo si fa un file_cmp per controllare che siano identici
static int test_integration_determinism(void){
	mr_t mr1 = NULL;
	mr_t mr2 = NULL;
	mr_attr_t attr1;
	mr_attr_t attr2;
	char input_path[] = "/tmp/mr_int_det_in_XXXXXX";
	char out1[] = "/tmp/mr_int_det_o1_XXXXXX";
	char out2[] = "/tmp/mr_int_det_o2_XXXXXX";
	char log1[] = "/tmp/mr_int_det_l1_XXXXXX";
	char log2[] = "/tmp/mr_int_det_l2_XXXXXX";
	int cmp;

	if(mr_attr_init(&attr1) != 0)
		return -1;
	if(mr_attr_init(&attr2) != 0){
		mr_attr_destroy(&attr1);
		return -1;
	}
	if(make_log_path(log1) != 0 || make_log_path(log2) != 0){
		mr_attr_destroy(&attr1);
		mr_attr_destroy(&attr2);
		return -1;
	}
	if(mr_attr_set_mapper_threads(&attr1, 2) != 0
	   || mr_attr_set_reducer_threads(&attr1, 2) != 0
	   || mr_attr_set_queue_size(&attr1, 8) != 0
	   || mr_attr_set_log_file(&attr1, log1) != 0
	   || mr_attr_set_mapper_threads(&attr2, 2) != 0
	   || mr_attr_set_reducer_threads(&attr2, 2) != 0
	   || mr_attr_set_queue_size(&attr2, 8) != 0
	   || mr_attr_set_log_file(&attr2, log2) != 0){
		mr_attr_destroy(&attr1);
		mr_attr_destroy(&attr2);
		return -1;
	}

	if(make_input_file(input_path, "bb aa\n aa\n") != 0){
		mr_attr_destroy(&attr1);
		mr_attr_destroy(&attr2);
		return -1;
	}
	if(make_output_file(out1) != 0 || make_output_file(out2) != 0){
		unlink(input_path);
		unlink(out1);
		unlink(out2);
		mr_attr_destroy(&attr1);
		mr_attr_destroy(&attr2);
		return -1;
	}

	if(mr_run(&attr1, &mr1, input_path, out1, word_count_mapper, word_count_reducer, NULL) != 0){
		unlink(input_path);
		unlink(out1);
		unlink(out2);
		unlink(log1);
		unlink(log2);
		mr_attr_destroy(&attr1);
		mr_attr_destroy(&attr2);
		return -1;
	}
	if(mr_run(&attr2, &mr2, input_path, out2, word_count_mapper, word_count_reducer, NULL) != 0){
		unlink(input_path);
		unlink(out1);
		unlink(out2);
		unlink(log1);
		unlink(log2);
		mr_attr_destroy(&attr1);
		mr_attr_destroy(&attr2);
		return -1;
	}

	cmp = cmp_files(out1, out2);

	unlink(input_path);
	unlink(out1);
	unlink(out2);
	unlink(log1);
	unlink(log2);
	mr_attr_destroy(&attr1);
	mr_attr_destroy(&attr2);

	if(cmp != 0)
		return -1;
	return 0;
}

//test per controllare che mapper e reducer siano in grado di passare byte opachi, senza funzionare solo su word-count 
static int test_integration_opaque_values(void){
	mr_t mr = NULL;
	mr_attr_t attr;
	char input_path[] = "/tmp/mr_int_op_in_XXXXXX";
	char output_path[] = "/tmp/mr_int_op_out_XXXXXX";
	char log_path[] = "/tmp/mr_int_op_log_XXXXXX";
	record_from_reducer_t recs[MAX_RESULTS];
	size_t n = 0;
	unsigned char expected[3];

	expected[0] = 'A';
	expected[1] = 0;
	expected[2] = 'B';

	if(mr_attr_init(&attr) != 0)
		return -1;
	if(make_log_path(log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(mr_attr_set_mapper_threads(&attr, 2) != 0
	   || mr_attr_set_reducer_threads(&attr, 2) != 0
	   || mr_attr_set_queue_size(&attr, 8) != 0
	   || mr_attr_set_log_file(&attr, log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	if(make_input_file(input_path, "hello\n") != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(make_output_file(output_path) != 0){
		unlink(input_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(mr_run(&attr, &mr, input_path, output_path, opaque_mapper, opaque_reducer, NULL) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(read_all_results(output_path, recs, MAX_RESULTS, &n) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(n != 1
	   || strcmp(recs[0].token, "tok") != 0
	   || recs[0].res_len != sizeof(expected)
	   || memcmp(recs[0].res, expected, sizeof(expected)) != 0){
		free_results(recs, n);
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	free_results(recs, n);
	unlink(input_path);
	unlink(output_path);
	unlink(log_path);
	mr_attr_destroy(&attr);
	return 0;
}

//caso in cui vi e' un mapper che non emette coppie
static int test_integration_mapper_silent(void){
	mr_t mr = NULL;
	mr_attr_t attr;
	char input_path[] = "/tmp/mr_int_sil_in_XXXXXX";
	char output_path[] = "/tmp/mr_int_sil_out_XXXXXX";
	char log_path[] = "/tmp/mr_int_sil_log_XXXXXX";
	record_from_reducer_t recs[MAX_RESULTS];
	size_t n = 0;

	if(mr_attr_init(&attr) != 0)
		return -1;
	if(make_log_path(log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(mr_attr_set_mapper_threads(&attr, 2) != 0
	   || mr_attr_set_reducer_threads(&attr, 2) != 0
	   || mr_attr_set_queue_size(&attr, 8) != 0
	   || mr_attr_set_log_file(&attr, log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	if(make_input_file(input_path, "qualcosa da ignorare\n") != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(make_output_file(output_path) != 0){
		unlink(input_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(mr_run(&attr, &mr, input_path, output_path, silent_mapper, dummy_reducer, NULL) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(read_all_results(output_path, recs, MAX_RESULTS, &n) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	if(n != 0){
		free_results(recs, n);
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	unlink(input_path);
	unlink(output_path);
	unlink(log_path);
	mr_attr_destroy(&attr);
	return 0;
}

//test che stressa una coda interna piccola con necessita' di sincronizzazione multithrd
static int test_integration_multithread_small_queue(void){
	mr_t mr = NULL;
	mr_attr_t attr;
	char input_path[] = "/tmp/mr_int_mt_in_XXXXXX";
	char output_path[] = "/tmp/mr_int_mt_out_XXXXXX";
	char log_path[] = "/tmp/mr_int_mt_log_XXXXXX";
	record_from_reducer_t recs[MAX_RESULTS];
	size_t n = 0;
	char *content =
		"aa bb\n"
		"cc aa\n"
		"bb bb\n"
		"dd\n"
		"aa ee\n"
		"bb\n";

	//inizializzazione
	if(mr_attr_init(&attr) != 0)
		return -1;
	if(make_log_path(log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	//thrd doppi rispetto a queue_size
	if(mr_attr_set_mapper_threads(&attr, 4) != 0
	   || mr_attr_set_reducer_threads(&attr, 4) != 0
	   || mr_attr_set_queue_size(&attr, 2) != 0
	   || mr_attr_set_log_file(&attr, log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}

	if(make_input_file(input_path, content) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(make_output_file(output_path) != 0){
		unlink(input_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//lancio del mr di test
	if(mr_run(&attr, &mr, input_path, output_path, word_count_mapper, word_count_reducer, NULL) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//legge tutti i risultati
	if(read_all_results(output_path, recs, MAX_RESULTS, &n) != 0){
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//controllo del test
	if(n != 5
	   || check_int_record(&recs[0], "aa", 3) != 0
	   || check_int_record(&recs[1], "bb", 4) != 0
	   || check_int_record(&recs[2], "cc", 1) != 0
	   || check_int_record(&recs[3], "dd", 1) != 0
	   || check_int_record(&recs[4], "ee", 1) != 0){
		free_results(recs, n);
		unlink(input_path);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	//cleanup
	free_results(recs, n);
	unlink(input_path);
	unlink(output_path);
	unlink(log_path);
	mr_attr_destroy(&attr);
	return 0;
}

//input inesistente: mr_start deve fallire subito (no hang) con errno != 0;
//seconda mr_start sulla stessa mr_t deve dare EINVAL (policy one-shot)
static int test_missing_input_and_one_shot(void){
	mr_t mr = NULL;
	mr_attr_t attr;
	char log_path[] = "/tmp/mr_int_miss_log_XXXXXX";
	char output_path[] = "/tmp/mr_int_miss_out_XXXXXX";

	if(mr_attr_init(&attr) != 0)
		return -1;
	if(make_log_path(log_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(make_output_file(output_path) != 0){
		mr_attr_destroy(&attr);
		return -1;
	}
	if(mr_attr_set_log_file(&attr, log_path) != 0){
		unlink(output_path);
		mr_attr_destroy(&attr);
		return -1;
	}
	if(mr_create(&mr, &attr, silent_mapper, dummy_reducer, NULL) != 0){
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	errno = 0;
	if(mr_start(mr, "/tmp/mr_int_no_such_input", output_path) != -1 || errno == 0){
		mr_destroy(mr);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	errno = 0;
	if(mr_start(mr, "/tmp/mr_int_no_such_input", output_path) != -1 || errno != EINVAL){
		mr_destroy(mr);
		unlink(output_path);
		unlink(log_path);
		mr_attr_destroy(&attr);
		return -1;
	}

	mr_destroy(mr);
	unlink(output_path);
	unlink(log_path);
	mr_attr_destroy(&attr);
	return 0;
}


//main
int main(void)
{
	//vengono lanciati tutti i test
	if(test_integration_word_count() != 0){
		fprintf(stderr, "test_integration_word_count fallito\n");
		return 1;
	}
	if(test_integration_directory() != 0){
		fprintf(stderr, "test_integration_directory fallito\n");
		return 1;
	}
	if(test_integration_determinism() != 0){
		fprintf(stderr, "test_integration_determinism fallito\n");
		return 1;
	}
	if(test_integration_opaque_values() != 0){
		fprintf(stderr, "test_integration_opaque_values fallito\n");
		return 1;
	}
	if(test_integration_mapper_silent() != 0){
		fprintf(stderr, "test_integration_mapper_silent fallito\n");
		return 1;
	}
	if(test_integration_multithread_small_queue() != 0){
		fprintf(stderr, "test_integration_multithread_small_queue fallito\n");
		return 1;
	}
	if(test_missing_input_and_one_shot() != 0){
		fprintf(stderr, "test_missing_input_and_one_shot fallito\n");
		return 1;
	}

	printf("integration: tutti i test passati\n");
	return 0;
}
