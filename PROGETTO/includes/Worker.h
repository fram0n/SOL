#if !defined(_WORKER_H)
#define _WORKER_H

#define BUFLEN 255

typedef struct worker_arg {
    char filename[BUFLEN];      //path assoluto del file
    int csfd;                   //file descriptor della socket
    int fileNum;                //numero di file che il Collector deve ricevere
} worker_arg;


/**
 * funzione eseguita dal generico Worker
 * esegue il calcolo sul file passato come argomento
 * invia il risultato al Collector
*/
void worker(void* arg);

#endif /* _WORKER_H */
