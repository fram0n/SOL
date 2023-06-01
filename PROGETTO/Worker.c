#define _POSIX_SOURCE
#define _GNU_SOURCE

#include <util.h>
#include <Worker.h>
#include <message.h>

// funzione eseguita dal Worker thread del pool
void worker(void* arg) {
    sigset_t set;
    CHECK_EQ_EXIT("sigfillset", sigfillset(&set), -1, "sigfillset: errno=%d\n", errno);
    CHECK_NEQ_EXIT("pthread_sigmask", pthread_sigmask(SIG_SETMASK, &set, NULL), 0, "pthread_sigmask: errno=%d\n", errno);
    long r = 0;
    worker_arg* args = (worker_arg*) arg;
    char* filename = (char*)malloc(BUFLEN*sizeof(char));    //nome del file sul quale eseguire il calcolo
    memset(filename, 0, BUFLEN*sizeof(char));
    strncpy(filename, (*args).filename, strlen((*args).filename));
    int fileNum = (*args).fileNum;                          //numero di file che il Collector dovrebbe ricevere
    int csfd = (*args).csfd;                                //file descriptor della socket
    if(strncmp(filename, "exit", 5) == 0) { //inviare al Collector il messaggio di terminazione
        msg_t msg;
        memset(&msg, 0, sizeof(msg_t));
        msg.num = fileNum;
        strncpy(msg.str, "exit", 5*sizeof(char));
        //invia msg al processo Collector
        int n;
        CHECK_EQ_EXIT("writen", n = writen(csfd, &msg, sizeof(msg)), -1, "write: errno=%d\n", errno);
    }
    else {  //inviare al Collector il risultato del calcolo sul file
        FILE * ifp;
        CHECK_EQ_EXIT("fopen", ifp = fopen(filename, "rb"), NULL, "fopen: errno=%d\n", errno);
        long numero;
        int len = 0;
        while(!feof(ifp)) {     //esegue il calcolo sul file 
            fread(&numero, sizeof(long), 1, ifp);
            r = r + ((len)*numero);
            memset(&numero, 0, sizeof(long));
            len++;
        }
        msg_t msg;
        memset(&msg, 0, sizeof(msg_t));
        msg.num = r;
        strncpy(msg.str, filename, strlen(filename));
        //invia r al processo Collector
        int n;
        CHECK_EQ_EXIT("writen", n = writen(csfd, &msg, sizeof(msg)), -1, "write: errno=%d\n%s\n", errno, msg.str);
        fclose(ifp);
    }
    free(args);
    free(filename);
    return;
}