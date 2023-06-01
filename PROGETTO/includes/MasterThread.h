#if !defined(_MASTERTHREAD_H)
#define _MASTERTHREAD_H

#include <threadpool.h>

typedef struct arg_struct {
    threadpool_t *workerThreadpool;     //threadpool dei Worker
    char* dir;                          //nome della directory passata come input al main
    long delay;                         //tempo (ms) che intercorre tra l’invio delle richieste ai thread Worker
    int pfd1;                           //file descriptor per la scrittura
    int fileNum;                        //numero totale di file che deve ricevere il Collector
} arg_struct;

/**
 * funzione che esegue il thread Master
 * controlla che i file nella directory dir siano regolari,
 * poi aggiunge al threadpool i task che dovranno eseguire i Worker
*/
void* threadMaster(void *arguments);

/**
 * @brief Controlla che il file passato come argomento sia regolare.
 * @returns 1 se il file è regolare, 0 altrimenti.
 * @param pathname path del file da controllare
*/
int isRegular(char* pathname);

/**
 * @brief Aggiunge al threadpool il task per inviare al Collector il messaggio di stampa,
 *        che indica al Collector di stampare i risultati ottenuti fino a quel momento
 * @param pfd1 file descriptor della pipe per scrittura
*/
void printSignal(int pfd1);

/**
 * @brief Aggiunge al threadpool il task per inviare al Collector il messaggio di terminazione.
 *        Se la terminazione è causata da un segnale allora setta sigFlag a 1.
 * @returns -1 se non esiste il threadpool, 0 altrimenti.
 * @param workers threadpool dei worker
 * @param fileNum numero di file che il Collector dovrebbe ricevere
 * @param sig 1 se la terminazione è stata causata da un segnale
 *            0 terminazione regolare
 */
int exitMessage(threadpool_t *workers, int fileNum, int sig);

#endif /* _MASTERTHREAD_H */
