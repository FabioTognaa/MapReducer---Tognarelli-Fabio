#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "mr.h"
#include "io.h"

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
		size_t path1 = snprintf(buf1, sizeof(buf1), "%s/%s", dir, file1);
		size_t path2 = snprintf(buf2, sizeof(buf2), "%s/%s", dir, file2);
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

//funzione per semplificare il processo di: mr_attr_init -> mr_create -> mr_start -> mr_destroy -> cleanup
static int mr_run(mr_attr_t *attr, mr_t *mr, const char *input_path, const char *output_path, mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg){

	//mr_attr_init
	if(mr_attr_init(attr) != 0)
		return -1;

	//mr_create
	if(mr_create(mr, (const mr_attr_t*)attr, mapper, reducer, user_arg) != 0){
		mr_attr_destroy(attr);
		return -1;
	}

	
	//mr_start
	if(mr_start(*mr, input_path, output_path) != 0){
		mr_attr_destroy(attr);
		mr_destroy(*mr);
		return -1;
	}

	//mr_destroy
	if(mr_destroy(*mr) != 0){
		mr_attr_destroy(attr);
		return -1;
	}

	if(mr_attr_destroy(attr) != 0)
		return -1;

	return 0;
}

//TODO funzione per leggere tutti i risultati dopo una run di mr
static int read_all_result(const char *path, ){
}

//confronta due file per controllare determinismo degli output
static int cmp_files(const char *path_a, const char *path_b){

	//apre i file in lettura
	FILE *fd1 = fopen(path_a, "r");
	if(!fd1)
		return -1;

	FILE *fd2 = fopen(path_b, "r");
	if(!fd2){
		fclose(fd1);
		return -1;
	}

	//TODO loop di confronto tra i 2 file
	while(0){

		//copio il contenuto dei 2 file in 2 buffer
		char bufa[5000], bufb[5000];
		size_t la = fread(bufa, sizeof(size_t), sizeof(fd1), fd1);
		if(la < 0)
			return -1;
		size_t lb = fread(bufb, siezof(size_t), sizeof(fd2), fd2);
		if(lb < 0)
			return -1;

		//se le lunghezze sono diverse
		if (la != lb){
			fclose(fd1);
			fclose(fd2);
			return -1;
		}

		//se si è arrivati a EOF: i file sono uguali
		if(na == 0){
			fclose(fd1);
			fclose(fd2);
			return 0;
		}

		//se i byte sono diversi
		if(memcmp(bufa, bufb, la) != 0){
			fclose(fd1);
			fclose(fd2);
			return 1;
		}
	}

}

//funzione mapper word-count di test: emette un token per ogni parola che trova sulla linea di quel file
static int word_count_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){

	int start = 0, end = 0, in_token = 0;
	size_t i;
	//scorre la linea del file
	for(i = 0; i < line->line_len; i++){
		
		//var per la lettera
		char letter = line->line[i];
		int is_alnum = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9');

		//se non ho ancora un token
		if(in_token == 0){
			//se alfanumerico
			if(is_alnum){
				start = i;
				end = (int)i;
				in_token = 1;
				continue;
			}
		}

		//se sono ad un non-alfanumerico
		if(in_token == 1){
			if(!is_alnum){
				end = i;
				//costruisco spazio per il token
				size_t token_len = (size_t)(end - start);
				char *token_buf = malloc(token_len + 1);
				//controllo su toke_buf
				if(!token_buf)
					return -1;
				memcpy(token_buf, line->line + start, token_len);
				token_buf[token_len] = '\0';
				int one = 1;
				//mando in output la coppia
				if(emit(token_buf, &one, sizeof(one), emit_arg) != 0){
					free(token_buf);
					return -1;
				}
				free(token_buf);
				start = 0;
				end = 0;
				in_token = 0;
				continue;
			}
		}
	}
	if(in_token){
		size_t token_len = (size_t)(line->line_len - start);
			char *token_buf = malloc(token_len + 1);
			//controllo su toke_buf
			if(!token_buf)
				return -1;
			memcpy(token_buf, line->line + start, token_len);
			token_buf[token_len] = '\0';
			int one = 1;
			//mando in output la coppia
			if(emit(token_buf, &one, sizeof(one), emit_arg) != 0){
				free(token_buf);
				return -1;
			}
			free(token_buf);
			start = 0;
			end = 0;
			in_token = 0;
	}
	//quando trova un carattere alfanumerico (a-z; A-Z; 0-9) inizia ad annotare il token fino al primo non-alfanumerico che incontra
	
	return 0;
}

//funzione reducer word-count di test
static int word_count_reducer(){}


/*
 * TODO: tests/integration.c — pipeline completa via API pubblica
 * (mr_create → mr_start → mr_destroy)
 * =====================================================================
 * Helper da implementare (prima dei test)
 * =====================================================================
 * read_all_results(path, out_records, max_n)  [o loop su mr_read_result]
 *   Apre il file .mro e legge tutti i record con mr_read_result (io.h).
 *   NON interpretare result come stringa C: confronta size + memcmp.
 *   Così verifichi il formato PDF sez. 8 senza dipendere dal word-count.
 * =====================================================================
 * Mapper / reducer di test
 * =====================================================================
 *
 * word_count_reducer  (PDF sez. 9)
 *   Reducer: interpreta values[] come int, somma, emette il totale.
 *   Usato dal happy path e dallo stress thread.
 *
 * opaque_mapper / opaque_reducer  (PDF sez. 10)
 *   Mapper: emette valori binari opachi (es. buffer con byte 0 interni,
 *   o una struct serializzata), NON stringhe C.
 *   Reducer: combina i byte senza strlen/strcmp/printf("%s") sui values.
 *   Serve a dimostrare che il framework non interpreta processed_token.
 *
 * silent_mapper
 *   Non chiama mai emit. Usato per verificare che la pipeline termini
 *   anche senza coppie intermedie (chiusura pipe / EOF, PDF sez. 5.1).
 *
 *
 * =====================================================================
 * Test da implementare (lista definitiva)
 * =====================================================================
 *
 * ---------------------------------------------------------------------
 * 1) test_integration_word_count
 * ---------------------------------------------------------------------
 * Senso:
 *   Smoke test principale end-to-end. Se questo fallisce o hang, il
 *   framework non è utilizzabile. Copre fork, pipe, thread, group-by
 *   token, scrittura output. Include di fatto "non hang" e propagazione EOF.
 *
 * Setup:
 *   File temporaneo, es. contenuto:
 *     "aa bb\n"
 *     "aa\n"
 *   Attr ragionevoli (es. 2 mapper, 2 reducer, queue_size 8).
 *   Callback: word_count_*.
 *
 * Assert:
 *   - mr_start == 0
 *   - output contiene esattamente 2 record (o nell'ordine atteso):
 *       token "aa", result int 2
 *       token "bb", result int 1
 *   - i token nel file sono in ordine lessicografico (aa prima di bb)
 *   - leggere SOLO via mr_read_result (token_len + token + result_len + bytes)
 *
 * Nota:
 *   Più token per riga ("aa bb") è già incluso qui: non serve un test aparte.
 *   Formato output + sort (PDF sez. 8) possono stare qui come assert,
 *   senza un test_output_format separato.
 *
 * ---------------------------------------------------------------------
 * 2) test_integration_directory
 * ---------------------------------------------------------------------
 * Senso:
 *   PDF sez. 4: input può essere una directory; i file regolari diretti
 *   vanno letti in ordine lessicografico sul nome. tests/input.c verifica
 *   solo send_input sulla pipe; qui verifichi che l'OUTPUT FINALE sia
 *   coerente con quell'ordine attraverso tutta la pipeline.
 *
 * Setup:
 *   Directory temp con almeno:
 *     b.txt → es. "x\n"
 *     a.txt → es. "y\n"
 *   (scrivi b prima di a su disco, per non dipendere dall'ordine di creazione).
 *   Opzionale (stesso test, non obbligatorio): sottodirectory con un file
 *   "trap.txt" — non deve produrre token nell'output (non ricorsivo).
 *
 * Assert:
 *   - mr_start == 0
 *   - output word-count (o mapper semplice) coerente con a.txt poi b.txt
 *   - se hai messo la trappola: nessun token proveniente da trap.txt
 *
 * ---------------------------------------------------------------------
 * 3) test_integration_determinism
 * ---------------------------------------------------------------------
 * Senso:
 *   PDF sez. 8: a parità di input, attr e callback, due esecuzioni devono
 *   produrre lo stesso file di output. Con thread, un ordinamento non
 *   deterministico o una race può far passare il test 1 "a volte".
 *
 * Setup:
 *   Stesso input del test 1 (o simile con più token).
 *   Due handle distinti: due mr_create + due mr_start su due path output.
 *   (Nel progetto BASE non riusare lo stesso mr_t per due start.)
 *
 * Assert:
 *   - entrambe le start == 0
 *   - cmp_files(out1, out2) == 0 (identici byte-per-byte)
 *
 * ---------------------------------------------------------------------
 * 4) test_integration_opaque_values
 * ---------------------------------------------------------------------
 * Senso:
 *   PDF sez. 10 e 13: in valutazione mapper/reducer diversi dal word-count.
 *   Il framework deve trasportare processed_token come sequenza di byte
 *   di lunghezza nota (anche con '\0' interni). Questo test evita che
 *   "funziona solo col conteggio parole".
 *
 * Setup:
 *   opaque_mapper: per un token fisso (o token estratti) emette un valore
 *   binario noto, es. bytes { 'A', 0, 'B' } oppure due int packed.
 *   opaque_reducer: riceve tutti i values del token, li concatena o ne
 *   verifica la somma/lunghezza, emette un result binario prevedibile.
 *   Input minimo: una o due righe sufficienti a produrre 1+ token.
 *
 * Assert:
 *   - mr_start == 0
 *   - record output: token atteso + result con size e contenuto esatti
 *     (memcmp), senza trattare result come stringa
 *
 * ---------------------------------------------------------------------
 * 5) test_integration_mapper_silent
 * ---------------------------------------------------------------------
 * Senso:
 *   Casi limite sul ciclo di vita delle pipe (PDF sez. 5.1). Se il mapper
 *   non emette coppie, il main deve comunque chiudere la pipe verso il
 *   mapper, il mapper chiudere stdout verso il reducer, il reducer
 *   chiudere stdout verso il main. Un fd lasciato aperto → hang.
 *
 * Setup:
 *   Input non vuoto (almeno una riga con testo).
 *   silent_mapper + un reducer qualsiasi (anche identity/dummy).
 *
 * Assert:
 *   - mr_start == 0 (ritorna: non hang)
 *   - file output senza record (prima mr_read_result → EOF_REACHED)
 *
 * Alternativa accettabile (uno solo dei due scenari):
 *   mapper normale + reducer che non chiama emit → stesso assert su output vuoto.
 *
 * ---------------------------------------------------------------------
 * 6) test_integration_multithread_small_queue
 * ---------------------------------------------------------------------
 * Senso:
 *   Esercita code interne piene/vuote (mtx/cnd), più writer sulla pipe
 *   mapper→reducer, più thread reducer. I test unitari mapper/reducer
 *   spesso usano 1 thread: i bug di sincronizzazione emergono qui.
 *
 * Setup:
 *   mapper_threads >= 2, reducer_threads >= 2, queue_size piccolo (es. 2).
 *   Input abbastanza lungo da stressare le code (più righe / più token).
 *   Stesse callback word_count del test 1.
 *
 * Assert:
 *   - mr_start == 0
 *   - output uguale a quello atteso in sequenziale (stessi conteggi, stessi
 *     token ordinati). Se diverso → race o perdita/duplicazione risultati.
 *
 * ---------------------------------------------------------------------
 * 7) test_integration_invalid_input  [opzionale ma corto]
 * ---------------------------------------------------------------------
 * Senso:
 *   Un solo caso negativo via mr_start: path inesistente. Mostra gestione
 *   errori senza una batteria di test API.
 *
 * Setup:
 *   mr_create ok, poi mr_start(mr, "/tmp/mr_no_such_path_xyz", out).
 *
 * Assert:
 *   - mr_start == -1
 *   - (opzionale) errno impostato in modo ragionevole (es. ENOENT)
 *
 * Se preferisci alleggerire: omettilo e documenta solo i 6 sopra.
 *
 *
 * =====================================================================
 * Infrastruttura Makefile
 * =====================================================================
 * - Aggiungere TEST_INTEGRATION = tests/integration
 * - Compilarlo linkando libmr.a (-pthread), includerlo in `make test`
 * - Aggiungerlo a `make clean`
 * - In main(): eseguire i test 1–6 (e 7 se presente); return 0 solo se
 *   tutti passano; su fallimento stampare il nome del test su stderr
 *
 *
 * Ordine consigliato di implementazione
 * -------------------------------------
 * 1. helper (write_file, run_mr, read_all_results)
 * 2. word_count_mapper/reducer + test 1
 * 3. test 2 (directory) riusando gli helper
 * 4. test 5 (silent) — catch hang presto
 * 5. test 4 (opaque), test 6 (multithread), test 3 (determinismo)
 * 6. test 7 opzionale + wiring Makefile
 */
int main(void)
{
	fprintf(stderr, "integration: nessun test implementato\n");
	return 1;
}
