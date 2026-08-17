/*
 * WORD-COUNT:
 * Esempio di uso di libmr con mapper e reducer che fanno word count dell'input
 * Uso: ./word-count <input> <output>
 */

#include <stdio.h> 
#include <string.h> 
#include <stdlib.h> 
#include "mr.h"
static int mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
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

static int reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){
	int total = (int)values_count;

	(void)values;
	(void)user_arg;

	if(emit(token, &total, sizeof(total), emit_arg) != 0)
		return -1;
	return 0;
    return 0;
}

int main(int argc, char **argv){
	mr_attr_t attr;
	mr_t mr;

	if (argc != 3) {
		fprintf(stderr, "uso: %s <input> <output>\n", argv[0]);
		return 1;
	}

	if (mr_attr_init(&attr) != 0) {
		fprintf(stderr, "mr_attr_init\n");
		return 1;
	}

	if (mr_attr_set_mapper_threads(&attr, 2) != 0) {
		fprintf(stderr, "mr_attr_set_mapper_threads\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_attr_set_reducer_threads(&attr, 2) != 0) {
		fprintf(stderr, "mr_attr_set_reducer_threads\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_attr_set_queue_size(&attr, 64) != 0) {
		fprintf(stderr, "mr_attr_set_queue_size\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_create(&mr, &attr, mapper, reducer, NULL) != 0) {
		fprintf(stderr, "mr_create\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_start(mr, argv[1], argv[2]) != 0) {
		fprintf(stderr, "mr_start\n");
		mr_destroy(mr);
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_destroy(mr) != 0) {
		fprintf(stderr, "mr_destroy\n");
		mr_attr_destroy(&attr);
		return 1;
	}

	if (mr_attr_destroy(&attr) != 0) {
		fprintf(stderr, "mr_attr_destroy\n");
		return 1;
	}

	return 0;
}