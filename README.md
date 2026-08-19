<div align="center">

# libmr

</div>

This README is available in two languages. Questo README è disponibile in due lingue.

- [English](#english)
- [Italiano](#italiano)

---

## English

C framework for concurrent text-file analysis, inspired by MapReduce.

### Introduction

`libmr` is a C framework for analysing text files on a single machine, inspired by the MapReduce model. The goal is not to implement a specific application (word count, indexing, and so on), but to offer a static library that an external C program can use to run its own analysis concurrently.

The user program supplies two callbacks. The mapper receives a logical line from a file and may emit zero or more ⟨token, value⟩ pairs. The reducer receives a token together with all values associated with that token and produces one or more final results. The framework handles the rest: reading the input, creating the process pipeline, transporting data over pipes, grouping by token, thread synchronisation, and writing the output.

Execution uses three processes connected by pipes. The main process belongs to the calling program: it reads the files (or the regular files in a directory, in lexicographic order) and sends serialised lines to the mapper process. The mapper is multithreaded (C11 threads): a reader thread fills a producer–consumer queue and a set of workers invoke the mapper callback. The pairs produced go to the reducer process, also multithreaded, which groups values by token and only then invokes the reducer callback. Results return to the main process, which writes them to file in deterministic order (lexicographic order on the token).

Tokens are alphanumeric ASCII C strings and are the only field the framework interprets, because they are the grouping key. Intermediate values and final results are opaque byte sequences: the framework transports them without assuming they are strings, integers, or printable data.

The project targets Linux. It uses `fork()`, pipes, `dup2()`, `waitpid()`, and C11 threads. It does not use sockets, shared memory between processes, `exec()`, or POSIX pthreads.
The public interface is in `include/mr.h`; the compilation product is the static library `libmr.a`.

### Local installation

There are no dependencies to install beyond a C environment on Linux: `gcc` with C11 and thread support (`-std=c11 -pthread`) and GNU `make`. On Ubuntu 24.04 these are the tools on the lab machines; if they are missing, the `build-essential` package is enough.

After unpacking the archive (or cloning the repository) into the project directory, from the root:

```
make
```

Compilation produces `libmr.a` and the two example executables `examples/minimal` and `examples/word-count`. There is no system installation step (`make install`, prefixes, pkg-config): to use the framework it is enough to include `include/mr.h` and link `libmr.a`, as described below.

To remove objects, the library, the examples, and test binaries:

```
make clean
```

### Repository layout

```
.
├── Makefile
├── README.md
├── include/                 public and internal headers
│   ├── mr.h                 	public framework interface
│   ├── mr_internal.h        	internal definitions of struct mr and output records
│   ├── mr_err.h             	internal state of mr_start and cleanup
│   ├── io.h                 	pipe protocol for input and output between processes
│   ├── log.h                	inter-process log
│   ├── queue.h              	C11 producer–consumer queue
│   ├── mapper_proc.h        	mapper process
│   └── reducer_proc.h       	reducer process
├── src/                     implementation
│   ├── mr.c                 	public API, fork, pipes, output collection
│   ├── input.c              	input file/directory scan and line dispatch
│   ├── io.c                 	I/O with explicit lengths
│   ├── log.c				 	project log-file handling
│   ├── queue.c				 	internal queue used by mapper and reducer processes
│   ├── mapper_proc.c		 	mapper process
│   └── reducer_proc.c		 	reducer process
├── examples/				 example programs
│   ├── minimal.c            	end-to-end run with mapper/reducer that emit nothing
│   ├── word-count.c         	word-count over alphanumeric tokens
│   └── files/               	sample input (and optional output) for the examples
└── tests/                   automated tests (internal structures and pipeline)
    ├── log.c
    ├── input.c
    ├── io.c
    ├── mapper.c
    ├── reducer.c
    └── integration.c
```

`include/mr.h` is the only header an application program must include. The other headers in `include/` describe internal modules: tests use them to exercise protocol, queues, mapper, and reducer in isolation, but they are not part of the public contract.

### Makefile and tests on internal structures

The Makefile exposes three targets. `make` (equivalent to `make all`) compiles the objects in `src/`, archives the static library `libmr.a`, and builds `examples/minimal` and `examples/word-count`. `make test` rebuilds the library if needed, compiles each executable in `tests/`, and runs them in sequence. `make clean` removes the library, objects, both examples, and test binaries.

`make test` is the intended way to check internal structures and the full pipeline. Each executable covers one module; if a test fails, the suite stops and the process exits with a non-zero status.

`tests/log.c` checks the log-line format (`[timestamp] [process] [thread] [event] message`) and concurrent writes from distinct processes, synchronised with a POSIX semaphore. `tests/input.c` checks reading a single file, a directory of regular files (lexicographic order on names), and an empty file, that is the path that serialises lines toward the mapper. `tests/io.c` exercises the internal protocol: `readn`/`writen`, line messages, ⟨token, value⟩ pairs, result records, EOF on pipes, truncated headers or payloads, and invalid lengths. `tests/mapper.c` starts the mapper process in a child, sends it a line, and checks the pair emitted on stdout. `tests/reducer.c` does the symmetric check on the reducer process: it sends already serialised pairs and verifies grouping and result emission. `tests/integration.c` closes the loop: it creates temporary files (or a directory), calls `mr_create` / `mr_start` / `mr_destroy`, and compares the output file with the expected result, including a word-count case, opaque values, a mapper that emits nothing, a small queue with several threads, and failure on a missing input.

A single test can also be run after `make test` (or after compiling that binary): for example `./tests/io` or `./tests/mapper`. Test sources are not installed; they live only in the development tree.

### How to use the framework on sample input

`libmr` is not a command-line program with a fixed argv: it is a library. The caller configures attributes, registers mapper and reducer, and starts processing with `mr_start`, passing the input path (regular file or directory) and the output file path. `mr_start` is blocking: it returns only when processing has finished or on error. Public functions return `0` on success and `-1` on error, setting `errno` where appropriate.

After `make`, the compiled examples are `examples/minimal` and `examples/word-count`. Both take `<input> <output>` and call `mr_start`. `minimal` runs the full pipeline with callbacks that emit nothing (empty output file). `word-count` counts alphanumeric tokens: the mapper emits the integer `1` for each token; the reducer emits `values_count` as an `int` (valid because every emitted value is `1`).

Usage:

```
./examples/minimal examples/files/input.txt /tmp/empty.mro
./examples/word-count examples/files/input.txt /tmp/out.mro
```

A complete program, of the same kind as `examples/word-count.c` and `tests/integration.c`, has the following shape.

Mapper and reducer are defined. In occurrence counting the mapper splits the line into alphanumeric tokens and for each one emits the integer value `1`; the reducer receives all values for a token and emits the total as an `int`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mr.h"

static int word_count_mapper(const mr_file_line_t *line, mr_emit_pair_t emit,
                             void *emit_arg, void *user_arg)
{
	size_t i, start = 0;
	int in_token = 0;

	(void)user_arg;
	for (i = 0; i < line->line_len; i++) {
		char c = line->line[i];
		int alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9');
		if (!in_token) {
			if (alnum) {
				start = i;
				in_token = 1;
			}
			continue;
		}
		if (!alnum) {
			size_t n = i - start;
			char *tok = malloc(n + 1);
			int one = 1;
			if (!tok)
				return -1;
			memcpy(tok, line->line + start, n);
			tok[n] = '\0';
			if (emit(tok, &one, sizeof(one), emit_arg) != 0) {
				free(tok);
				return -1;
			}
			free(tok);
			in_token = 0;
		}
	}
	if (in_token) {
		size_t n = line->line_len - start;
		char *tok = malloc(n + 1);
		int one = 1;
		if (!tok)
			return -1;
		memcpy(tok, line->line + start, n);
		tok[n] = '\0';
		if (emit(tok, &one, sizeof(one), emit_arg) != 0) {
			free(tok);
			return -1;
		}
		free(tok);
	}
	return 0;
}

static int word_count_reducer(const char *token, const mr_value_t *values,
                              size_t values_count, mr_emit_result_t emit,
                              void *emit_arg, void *user_arg)
{
	int total = (int)values_count;

	(void)values;
	(void)user_arg;
	return emit(token, &total, sizeof(total), emit_arg);
}

int main(int argc, char **argv)
{
	mr_attr_t attr;
	mr_t mr;

	if (argc != 3) {
		fprintf(stderr, "uso: %s <input> <output>\n", argv[0]);
		return 1;
	}
	if (mr_attr_init(&attr) == -1)
		return 1;
	if (mr_attr_set_mapper_threads(&attr, 2) == -1
	    || mr_attr_set_reducer_threads(&attr, 2) == -1
	    || mr_attr_set_queue_size(&attr, 64) == -1
	    || mr_attr_set_log_file(&attr, "mr.log") == -1) {
		mr_attr_destroy(&attr);
		return 1;
	}
	if (mr_create(&mr, &attr, word_count_mapper, word_count_reducer, NULL) == -1) {
		mr_attr_destroy(&attr);
		return 1;
	}
	if (mr_start(mr, argv[1], argv[2]) == -1) {
		perror("mr_start");
		mr_destroy(mr);
		mr_attr_destroy(&attr);
		return 1;
	}
	mr_destroy(mr);
	mr_attr_destroy(&attr);
	return 0;
}
```

The client is compiled against the library already built:

```
gcc -std=c11 -pthread -Wall -Wextra -Iinclude -o wordcount wordcount.c libmr.a -pthread
```

A minimal input, analogous to the one used by the integration test:

```
printf 'bb aa\n aa\n' > input.txt
./wordcount input.txt output.mro
```

`input.txt` can be replaced by a directory: in that case the framework processes only the regular files contained directly in it, in name order, without descending into subdirectories.

The `output.mro` file is not text. Each record is binary, with explicit lengths: an `int` with the token length, the token bytes (without `'\0'`), an `int` with the result length, the opaque result bytes. Records are lexicographically ordered on the token. For the example above two records are expected, `aa` with value `2` and `bb` with value `1`, each serialised as an `int`. A `cat` of the file is not meaningful; inspecting it requires a reader that respects the lengths, as `tests/integration.c` does.

If no path is set with `mr_attr_set_log_file`, the log goes to `mr.log` in the working directory. Lines have the form `[YYYY-MM-DD HH:MM:SS] [main|mapper|reducer] [thread_id] [event] message`. After an error in `mr_start` the handle is not reusable: `mr_destroy` and a new `mr_create` are required before another run.

---

## Italiano

Framework C per l’analisi concorrente di file di testo, ispirato a MapReduce.

### Introduzione

`libmr` è un framework C per analizzare file di testo su una singola macchina, ispirato al modello MapReduce. L’obiettivo non è implementare un’applicazione specifica (conteggio parole, indicizzazione, e così via), ma offrire una libreria statica che un programma C esterno possa usare per far girare la propria analisi in modo concorrente.

Il programma utente fornisce due callback. Il mapper riceve una riga logica di un file e può emettere zero o più coppie ⟨token, valore⟩. Il reducer riceve un token insieme a tutti i valori associati a quel token e produce uno o più risultati finali. Il framework si occupa del resto: lettura dell’input, creazione della pipeline di processi, trasporto dei dati sulle pipe, raggruppamento per token, sincronizzazione dei thread e scrittura dell’output.

L’esecuzione avviene in tre processi collegati da pipe. Il processo principale appartiene al programma chiamante: legge i file (o i file regolari di una directory, in ordine lessicografico) e invia le righe serializzate al processo mapper. Il mapper è multithread (thread C11): un thread lettore riempie una coda produttore-consumatore e un insieme di worker invoca la callback mapper. Le coppie prodotte vanno al processo reducer, anch’esso multithread, che raggruppa i valori per token e solo dopo invoca la callback reducer. I risultati tornano al processo principale, che li scrive su file in ordine deterministico (ordinamento lessicografico sul token).

I token sono stringhe C alfanumeriche ASCII e sono l’unico campo che il framework interpreta, perché servono da chiave di raggruppamento. I valori intermedi e i risultati finali sono sequenze opache di byte: il framework li trasporta senza assumere che siano stringhe, interi o dati stampabili.

Il progetto è pensato per Linux. Usa `fork()`, pipe, `dup2()`, `waitpid()` e thread C11.
L’interfaccia pubblica sta in `include/mr.h`; il prodotto della compilazione è la libreria statica `libmr.a`.

### Installazione in locale

Non ci sono dipendenze da installare oltre a un ambiente C su Linux: `gcc` con supporto C11 e thread (`-std=c11 -pthread`) e GNU `make`. Su Ubuntu 24.04 questi strumenti sono quelli della macchina di laboratorio; se mancano, basta il pacchetto `build-essential`.

Dopo aver scompattato l’archivio (o clonato il repository) nella directory del progetto, dalla root:

```
make
```

La compilazione produce `libmr.a` e i due eseguibili `examples/minimal` e `examples/word-count`. Non sono previsti passi di installazione di sistema (`make install`, prefissi, pkg-config): per usare il framework basta includere `include/mr.h` e collegare `libmr.a`, come descritto più sotto.

Per rimuovere oggetti, libreria, esempi e binari di test:

```
make clean
```

### Struttura del repository

```
.
├── Makefile
├── README.md
├── include/                 header pubblici e interni
│   ├── mr.h                 	interfaccia pubblica del framework
│   ├── mr_internal.h        	definizione interne di struct mr e record di output
│   ├── mr_err.h             	stato interno di mr_start e cleanup
│   ├── io.h                 	protocollo sulle pipe in input ed output tra i processi
│   ├── log.h                	log inter-processo
│   ├── queue.h              	coda produttore-consumatore C11
│   ├── mapper_proc.h        	processo mapper
│   └── reducer_proc.h       	processo reducer
├── src/                     implementazione
│   ├── mr.c                 	API pubblica, fork, pipe, raccolta output
│   ├── input.c              	scansione file/directory di input e invio righe
│   ├── io.c                 	I/O a lunghezze esplicite
│   ├── log.c				 	gestione del file di log del progetto
│   ├── queue.c				 	gestione della coda interna ai processi mapper e reducer
│   ├── mapper_proc.c		 	processo mapper
│   └── reducer_proc.c		 	processo reducer
├── examples/				 programmi di esempio
│   ├── minimal.c            	pipeline end-to-end con mapper/reducer che non emettono nulla
│   ├── word-count.c         	conteggio delle occorrenze dei token alfanumerici
│   └── files/               	input di esempio (e output opzionale) per gli esempi
└── tests/                   test automatici (strutture interne e pipeline)
    ├── log.c
    ├── input.c
    ├── io.c
    ├── mapper.c
    ├── reducer.c
    └── integration.c
```

`include/mr.h` è l’unico header che un programma applicativo deve includere. Gli altri header in `include/` descrivono moduli interni: i test li usano per esercitare protocollo, code, mapper e reducer in isolamento, ma non fanno parte del contratto pubblico.

### Makefile e test sulle strutture interne

Il Makefile espone tre target. `make` (equivalente a `make all`) compila gli oggetti in `src/`, archivia la libreria statica `libmr.a` e costruisce `examples/minimal` e `examples/word-count`. `make test` ricostruisce la libreria se necessario, compila ogni eseguibile in `tests/` e li lancia in sequenza. `make clean` elimina libreria, oggetti, entrambi gli esempi e binari di test.

`make test` è il modo previsto per verificare le strutture interne e la pipeline completa. Ogni eseguibile copre un modulo; se un test fallisce, la batteria si ferma e il processo esce con codice diverso da zero.

`tests/log.c` controlla il formato delle righe di log (`[timestamp] [processo] [thread] [evento] messaggio`) e la scrittura concorrente da processi distinti, sincronizzata con un semaforo POSIX. `tests/input.c` verifica la lettura di un singolo file, di una directory di file regolari (ordine lessicografico sui nomi) e di un file vuoto, cioè il percorso che serializza le righe verso il mapper. `tests/io.c` esercita il protocollo interno: `readn`/`writen`, messaggi di riga, coppie ⟨token, valore⟩, record di risultato, EOF sulle pipe, header o payload troncati e lunghezze non valide. `tests/mapper.c` avvia il processo mapper in un figlio, gli invia una riga e controlla la coppia emessa su stdout. `tests/reducer.c` fa il simmetrico sul processo reducer: invia coppie già serializzate e verifica raggruppamento ed emissione del risultato. `tests/integration.c` chiude il cerchio: crea file (o directory) temporanei, chiama `mr_create` / `mr_start` / `mr_destroy` e confronta il file di output con il risultato atteso, incluso un caso di conteggio parole, valori opachi, mapper che non emette nulla, coda piccola con più thread e fallimento su input inesistente.

Si può anche lanciare un singolo test dopo `make test` (o dopo aver compilato quel binario): ad esempio `./tests/io` o `./tests/mapper`. I sorgenti di test non vengono installati; vivono solo nel tree di sviluppo.

### Come usare il framework su un input di esempio

`libmr` non è un programma a riga di comando con argv fissi: è una libreria. Il chiamante configura gli attributi, registra mapper e reducer, e avvia l’elaborazione con `mr_start`, passando il path di input (file regolare o directory) e il path del file di output. `mr_start` è bloccante: torna solo a elaborazione conclusa o in errore. Le funzioni pubbliche restituiscono `0` in successo e `-1` in errore, impostando `errno` ove appropriato.

Dopo `make`, gli esempi compilati sono `examples/minimal` e `examples/word-count`. Entrambi prendono `<input> <output>` e chiamano `mr_start`. `minimal` esegue la pipeline con callback che non emettono nulla (file di output vuoto). `word-count` conta i token alfanumerici: il mapper emette l’intero `1` per ogni token; il reducer emette `values_count` come `int` (corretto perché ogni valore emesso è `1`).

Uso:

```
./examples/minimal examples/files/input.txt /tmp/empty.mro
./examples/word-count examples/files/input.txt /tmp/out.mro
```

Un programma completo, dello stesso tipo di `examples/word-count.c` e `tests/integration.c`, ha questa forma.

Si definiscono mapper e reducer. Nel conteggio delle occorrenze il mapper spezza la riga in token alfanumerici e per ciascuno emette il valore intero `1`; il reducer riceve tutti i valori di un token e emette il totale come `int`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mr.h"

static int word_count_mapper(const mr_file_line_t *line, mr_emit_pair_t emit,
                             void *emit_arg, void *user_arg)
{
	size_t i, start = 0;
	int in_token = 0;

	(void)user_arg;
	for (i = 0; i < line->line_len; i++) {
		char c = line->line[i];
		int alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9');
		if (!in_token) {
			if (alnum) {
				start = i;
				in_token = 1;
			}
			continue;
		}
		if (!alnum) {
			size_t n = i - start;
			char *tok = malloc(n + 1);
			int one = 1;
			if (!tok)
				return -1;
			memcpy(tok, line->line + start, n);
			tok[n] = '\0';
			if (emit(tok, &one, sizeof(one), emit_arg) != 0) {
				free(tok);
				return -1;
			}
			free(tok);
			in_token = 0;
		}
	}
	if (in_token) {
		size_t n = line->line_len - start;
		char *tok = malloc(n + 1);
		int one = 1;
		if (!tok)
			return -1;
		memcpy(tok, line->line + start, n);
		tok[n] = '\0';
		if (emit(tok, &one, sizeof(one), emit_arg) != 0) {
			free(tok);
			return -1;
		}
		free(tok);
	}
	return 0;
}

static int word_count_reducer(const char *token, const mr_value_t *values,
                              size_t values_count, mr_emit_result_t emit,
                              void *emit_arg, void *user_arg)
{
	int total = (int)values_count;

	(void)values;
	(void)user_arg;
	return emit(token, &total, sizeof(total), emit_arg);
}

int main(int argc, char **argv)
{
	mr_attr_t attr;
	mr_t mr;

	if (argc != 3) {
		fprintf(stderr, "uso: %s <input> <output>\n", argv[0]);
		return 1;
	}
	if (mr_attr_init(&attr) == -1)
		return 1;
	if (mr_attr_set_mapper_threads(&attr, 2) == -1
	    || mr_attr_set_reducer_threads(&attr, 2) == -1
	    || mr_attr_set_queue_size(&attr, 64) == -1
	    || mr_attr_set_log_file(&attr, "mr.log") == -1) {
		mr_attr_destroy(&attr);
		return 1;
	}
	if (mr_create(&mr, &attr, word_count_mapper, word_count_reducer, NULL) == -1) {
		mr_attr_destroy(&attr);
		return 1;
	}
	if (mr_start(mr, argv[1], argv[2]) == -1) {
		perror("mr_start");
		mr_destroy(mr);
		mr_attr_destroy(&attr);
		return 1;
	}
	mr_destroy(mr);
	mr_attr_destroy(&attr);
	return 0;
}
```

Si compila il client contro la libreria già costruita:

```
gcc -std=c11 -pthread -Wall -Wextra -Iinclude -o wordcount wordcount.c libmr.a -pthread
```

Un input minimo, analogo a quello usato dal test di integrazione:

```
printf 'bb aa\n aa\n' > input.txt
./wordcount input.txt output.mro
```

`input.txt` può essere sostituito da una directory: in quel caso il framework elabora solo i file regolari contenuti direttamente in essa, in ordine di nome, senza scendere nelle sottodirectory.

Il file `output.mro` non è testo. Ogni record è binario, con lunghezze esplicite: un `int` con la lunghezza del token, i byte del token (senza `'\0'`), un `int` con la lunghezza del risultato, i byte opachi del risultato. I record sono ordinati lessicograficamente sul token. Per l’esempio sopra ci si aspetta due record, `aa` con valore `2` e `bb` con valore `1`, ciascuno serializzato come `int`. Un `cat` del file non è significativo; per ispezionarlo serve un lettore che rispetti le lunghezze, come fa `tests/integration.c`.

Se non si imposta un path con `mr_attr_set_log_file`, il log va in `mr.log` nella directory di lavoro. Le righe hanno la forma `[YYYY-MM-DD HH:MM:SS] [main|mapper|reducer] [id_thread] [evento] messaggio`. Dopo un errore in `mr_start` l’handle non è riutilizzabile: occorre `mr_destroy` e un nuovo `mr_create` prima di un’altra elaborazione.
