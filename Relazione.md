## Gestione errori

Tutte le funzioni pubbliche del framework restituiscono `0` in caso di successo e `-1` in caso di errore, impostando `errno` ove appropriato (come richiesto dalla specifica).

| Categoria | Quando si verifica | Comportamento del framework |
|-----------|-------------------|----------------------------|
| **EINVAL** | Argomenti non validi: puntatori `NULL`, parametri di configurazione non ammessi (es. `mapper_threads == 0`, `queue_size == 0`), tentativo di riavviare un'elaborazione su un handle non resettato | Ritorno immediato con `-1` e `errno = EINVAL`; nessuna modifica persistente oltre a quanto già avvenuto prima del controllo |
| **ENOMEM** | Fallimento di `malloc`/`realloc`/`calloc` in `mr_create`, durante l'I/O sulle pipe o nell'accumulo dei risultati finali | Ritorno con `-1`; `errno` lasciato dal sistema; cleanup delle risorse già allocate (`free` parziale in `mr_create`, `mr_start_cleanup` in `mr_start`) |
| **kernel** | Fallimento di una system call (`pipe`, `fork`, `dup2`, `close`, `read`, `write`, `open`, `waitpid`, …): `errno` impostato dal kernel | Ritorno con `-1` propagando l'`errno` originale; in `mr_start`, `mr_start_cleanup` chiude i descrittori ancora aperti, attende i processi figli se già creati e registra l'evento sul log |
| **EIO** | Errore nella pipeline: processo figlio terminato con exit code ≠ 0 (tramite `mr_child_fail`), terminazione anomala (segnale), protocollo sulle pipe corrotto o lunghezze non valide negli header | I figli scrivono l'errore sul log e terminano con `_exit(1)`; il main, in `wait_children`, imposta `errno = EIO` e ritorna `-1` da `mr_start` |

**Policy one-shot:** dopo un errore in `mr_start`, l'handle `mr_t` non è considerato riutilizzabile per una nuova elaborazione. Il programma chiamante deve eseguire `mr_destroy(mr)` seguito da `mr_create(...)` prima di invocare nuovamente `mr_start`.

## Formato e ordine dell'output

Ogni record nel file di output contiene, in quest'ordine: lunghezza del token (`int`), byte del token (senza `'\0'`), lunghezza del risultato (`int`), byte opachi del risultato.

I record sono scritti in ordine lessicografico sul token (`strcmp`). Se il reducer emette più risultati per lo stesso token, l'ordine relativo è quello delle chiamate a `emit`.
