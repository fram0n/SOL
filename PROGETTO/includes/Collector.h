#if !defined(_COLLECTOR_H)
#define _COLLECTOR_H

/**
 * funzione eseguita dal processo Collector:
 * attende di ricevere tutti i risultati dai Worker ed al termine stampa i valori ottenuti sullo 
 * standard output, ordinando i risultati in modo crescente
*/
void collector(int ssfd, int pfd0);

#endif /* _COLLECTOR_H */
