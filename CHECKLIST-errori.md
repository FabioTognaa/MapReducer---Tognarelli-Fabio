# Checklist — gestione errori (manuale snello)

Stato aggiornato rispetto al codice attuale. Manuale di riferimento: due regole (`io.c`/`queue.c` silenziosi; resto logga + cleanup), `mr_err.h` minimo, policy one-shot su `mr->started`.

---

## Già fatto

### Fase 0 — Infrastruttura
- [x] `mr_start_state_t` completa in `include/mr_err.h`
- [x] `mr_child_fail` implementata in `src/mr.c`
- [x] `mr_start_cleanup` implementata in `src/mr.c`
- [x] `#include "mr_err.h"` in `mr.c`
- [x] Policy one-shot `mr->started` (controllo + `mr->started = 1`, niente reset su errore)
- [x] Errori pre-fork (`pipe`, `fork`) con log + cleanup manuale fd
- [x] Branch figlio mapper/reducer: `dup2`/`close` → `mr_child_fail`
- [x] Diversi path errore parent → `mr_start_cleanup` (`mr_send_input`, `malloc`, `open`, `mr_write_result`, …)
- [x] `Relazione.md` — paragrafo gestione errori + tabella 4 categorie

### Branch figlio reducer
- [x] Ordine corretto: `dup2` → `close` delle pipe non usate (`mr.c` ~347–363)

---

## Fase 1 — `src/mr.c` (finire + correggere bug)

### Bloccanti compilazione
- [ ] Rimuovere `CHECK_SYSCALL` alle linee ~374–377 — la macro non esiste più
- [ ] Sostituire con check manuale; su errore: `saved = errno`, log `"error"`, `return mr_start_cleanup(mr, &st, saved)`

### `wait_children` (~linee 30–48)
- [ ] Correggere `mr_log_write(mr->log, ...)` → `mr_log_write(&mr->log, ...)`
- [ ] `WEXITSTATUS != 0` → `errno = EIO`, return `-1`, **nessun log**
- [ ] `WIFSIGNALED(sts_map)` → log `"mapper terminated by signal"`, `errno = EIO`
- [ ] `WIFSIGNALED(sts_red)` → log `"reducer terminated by signal"` (oggi si controlla `sts_map` due volte)
- [ ] `waitpid` fallita → propaga `errno` kernel, return `-1`
- [ ] Path successo ~523: `wait_children(mr, pid_mapper, pid_reducer)` — firma attuale errata

### Allineare `st` con variabili locali
- [ ] Dopo `mr_send_input` OK → `st.mapper_write_open = 0`
- [ ] Usare `st.reducer_read_fd` nel loop `mr_read_result` (non `reducer_to_main[0]` diretto)
- [ ] Dopo `open` output OK → `st.output_fd = f_out`
- [ ] Aggiornare `st.records` e `st.n_records` (`record`, `dim`) prima di ogni cleanup — oggi `st.records` resta `NULL`

### Path errore ancora sporchi
- [ ] **`realloc` fallito** (~445–455): sostituire cleanup manuale con `saved = ENOMEM`, log, `mr_start_cleanup`
- [ ] **`mr_read_result == -1`** (~426–436): evitare `close` manuale prima del cleanup; usare flag `st.reducer_read_open`
- [ ] **`mr_write_result` fallita**: `st.output_fd` deve essere già impostato
- [ ] Path successo: coordinare `free_records` locale con `st.records` (evitare double-free)

### Return finale
- [ ] Sostituire `mr->error` con return esplicito `0` / `-1` + `errno`

### Pre-fork (opzionale)
- [ ] Valutare `mr_start_cleanup` con `st.children_forked = 0` al posto del cleanup duplicato su `pipe`/`fork`

---

## Fase 2 — `src/io.c` (~40 `perror`) — non iniziata

- [ ] Rimuovere ogni `perror`
- [ ] Header/len invalidi → `errno = EINVAL; return ERROR_SYSTEM`
- [ ] `malloc` fallito → `errno = ENOMEM`
- [ ] Syscall → lasciare `errno` del kernel
- [ ] `EOF_REACHED` → invariato, non toccare `errno`
- [ ] Zero log, zero `fprintf`

---

## Fase 3 — `src/input.c` (~9 `perror`) — non iniziata

- [ ] Rimuovere tutti i `perror`
- [ ] Su **errore**: `return -1`, **non** chiudere `mapper_write_fd`
- [ ] Su **successo**: chiudi pipe; parent fa `st.mapper_write_open = 0`
- [ ] Path invalido → `errno = EINVAL`
- [ ] `realloc`/`strdup` falliti → `errno = ENOMEM`

---

## Fase 4 — `src/queue.c` — non iniziata

- [ ] Rimuovere `fprintf(stderr, "coda chiusa")` — non è errore
- [ ] `q == NULL` → `errno = EINVAL; return -1` (silenzioso)
- [ ] `malloc` → `ENOMEM`; `mtx_init`/`cnd_init` falliti → `errno` + `-1`, niente `perror`

---

## Fase 5 — `src/mapper_proc.c` — non iniziata

- [ ] `fprintf(stderr, …)` → `mr_log_write(ctx->log, "mapper", 0, "error", …)`
- [ ] `perror` su `mtx_lock` → log + `return -1`
- [ ] Token invalido / callback `-1` → `ctx->error = 1`
- [ ] A fine `mapper_process_main`: se errore, chiudi stdout prima di `return -1`

---

## Fase 6 — `src/reducer_proc.c` — non iniziata

- [ ] Stesso schema del mapper (`fprintf` → `mr_log_write`, flag errore, chiusura stdout)

---

## Fase 7 — `src/log.c` — opzionale

- [ ] `perror` su argomenti `NULL` → `errno = EINVAL; return -1` (senza stampa)
- [ ] `perror` su syscall log → lasciare o togliere (fuori dal percorso utente)

---

## Fase 8 — API pubblica

- [ ] `mr_create`: su `malloc` fallito → `errno = ENOMEM` (oggi `return -1` nudo)
- [ ] Verificare che `record_from_reducer_t` in `mr.h` pubblico sia voluto

---

## Fase 9 — Verifica

- [ ] `make clean && make` (oggi probabilmente fallisce per `CHECK_SYSCALL` mancante)
- [ ] `make test`
- [ ] Input inesistente → `mr_start` termina subito (no hang)
- [ ] `errno != 0` dopo fallimento, coerente con la causa
- [ ] Log: una riga `"error"` per failure; niente doppio parent+figlio su `exit(1)`
- [ ] Seconda `mr_start` sulla stessa `mr_t` → `EINVAL`
- [ ] Programma utente: solo `perror("mr_start")`, niente doppia diagnostica

---

## Ordine consigliato

| Priorità | Cosa | Perché |
|----------|------|--------|
| 1 | Fix Fase 1 (`CHECK_SYSCALL`, `wait_children`, sync `st`) | Compila + anti-hang |
| 2 | `io.c` | Meccanico, impatta tutta la pipeline |
| 3 | `input.c` + `mapper_write_open` | Allineato al cleanup centralizzato |
| 4 | `queue.c` | Veloce |
| 5 | `mapper_proc.c` + `reducer_proc.c` | Log nei figli |
| 6 | Ritocchi `log.c`, `mr_create`, test | |

---

## Regole rapide (manuale snello)

| Dove | Cosa fare |
|------|-----------|
| `io.c`, `queue.c` | `errno` + `return -1`. Zero log, zero `perror`. |
| `input.c`, worker | `return -1`, `errno` già ok. Log solo nel processo che conosce l'errore. |
| `mr_start` (parent) | `saved = errno`, `mr_log_write(..., "error", ...)`, `mr_start_cleanup`. |
| Figlio post-fork setup | `mr_child_fail` |
| API pubblica | `errno = EINVAL; return -1` inline |
| Programma utente | `perror("mr_start")` |

### `errno` (relazione)

| Causa | `errno` |
|-------|---------|
| Parametro/header/token invalido | `EINVAL` |
| `malloc`/`realloc` | `ENOMEM` |
| Syscall | quello del kernel |
| Figlio fallito | `EIO` |

### Policy one-shot

`mr->started = 1` all'inizio di `mr_start`, mai resettato. Dopo errore: `mr_destroy` + `mr_create`.

---

## Cosa non fare

- Non aggiungere `mr_fail_errno`, `MR_SYSCALL`, `mr_log_error`
- Non usare `goto`
- Non resettare `mr->started` su errore
- Non loggare nel parent se il figlio è uscito con `exit(1)` (ha già loggato)
- Non mettere log in `io.c` / `queue.c`
- Non lasciare cleanup duplicati in `mr_start` dopo il refactor
