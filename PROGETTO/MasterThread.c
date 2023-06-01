#define _GNU_SOURCE
#define _POSIX_SOURCE

#include <util.h>
#include <MasterThread.h>
#include <Worker.h>
#include <dirent.h>

static int sigFlag = 0;     //se sigFlag == 1 è stato ricevuto un segnale di terminazione

//scorre tutta la directory ricorsivamente e invia i file regolari trovati ai worker
static void lsdir(char* rx_dir, long delay, threadpool_t *workers, int pfd1, int *fileNum);

//controlla che il file passato come argomento sia regolare
int isRegular(char* pathname);
//invia al Collector il messaggio che indica di stampare i risultati ottenuti fino a quel momento
void printSignal(int pfd1);
//invia al Collector il messaggio che indica di terminare
int exitMessage(threadpool_t *workers, int fileNum, int sig);

void* threadMaster(void *arguments) {
    sigset_t set;
    CHECK_EQ_EXIT("sigfillset", sigfillset(&set), -1, "sigfillset: errno=%d\n", errno);
    CHECK_NEQ_EXIT("pthread_sigmask", pthread_sigmask(SIG_SETMASK, &set, NULL), 0, "pthread_sigmask: errno=%d\n", errno);

    arg_struct *args = (arg_struct *)arguments;
    threadpool_t *workers = args->workerThreadpool;     //threadpool dei worker
    char* dirName = args->dir;                          //nome della directory da visitare
    long delay = args->delay;                           //tempo (ms) che intercorre tra l’invio delle richieste ai thread Worker
    int pfd1 = args->pfd1;                              //file descriptor della pipe per scrittura
    int fileNum = args->fileNum;                        //numero di file inviati al Collector
    
    if(dirName != NULL) {
        lsdir(dirName, delay, workers, pfd1, &fileNum);
    }
    if(!sigFlag) {
        exitMessage(workers, fileNum, 0);
    }
    return NULL;
}

/**
 * @brief Controlla che il file passato come argomento sia regolare.
 * @returns 1 se il file è regolare, 0 altrimenti.
 * @param pathname path del file da controllare
*/
int isRegular(char* pathname) {
    struct stat info;
    int r;
    SYSCALL_RETURN("stat", r, stat(pathname, &info),"stat di %s: errno=%d\n", pathname, errno);
    if(r == 0) {
        if(S_ISREG(info.st_mode)) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Visita la directory data e tutte le sue sottodirectory, controllando i file al loro interno.
 * @param rx_dir directory di partenza.
 * @param delay tempo (ms) che intercorre tra l’invio delle richieste ai thread Worker
 * @param workers threadpool dei worker
 * @param pfd1 file descriptor della pipe per scrittura
 * @param fileNum numero di file che il Collector dovrebbe ricevere
*/
static void lsdir(char* rx_dir, long delay, threadpool_t *workers, int pfd1, int* fileNum) {
    if(sigFlag) return;
    if(chdir(rx_dir) == -1) {   //mi sposto nella directory rx_dir
        perror("chdir");
        return;
    }
    DIR* dir;
    struct dirent* file;
    if((dir = opendir(".")) == NULL) {
        perror("opendir");
        return;
    }
    while((errno = 0, file = readdir(dir)) != NULL && !sigFlag) {
        if(file == NULL) return;
        if(strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0) {
            struct stat info;
            if(stat(file->d_name, &info) == -1) {
                perror("stat");
                break;
            }
            if(S_ISDIR(info.st_mode)) { //se file->d_name è una dir richiama lsdir altrimenti usa isRegular
                lsdir(file->d_name, delay, workers, pfd1, fileNum);
                if(chdir("..") == -1) { //torno alla directory precedente
                    perror("chdir");
                    break;
                }
            }
            else {
                if(EndsWithDat(file->d_name)) {
                    if(isRegular(file->d_name)) {
                        char* pathname = NULL;
                        if((pathname = realpath(file->d_name, NULL)) != NULL) {
                            (*fileNum)++;
                            worker_arg* w_args = malloc(sizeof(worker_arg));        //preparo gli argomenti da passare al thread worker
                            memset((*w_args).filename, 0, BUFLEN*sizeof(char));
                            strncpy((*w_args).filename, pathname, strlen(pathname));
                            (*w_args).fileNum = -1;

                            int r = addToThreadPool(workers, worker, (void*) w_args);
                            if (r<0) { // errore interno
                                fprintf(stderr, "FATAL ERROR, adding to the thread pool\n");
                            }
                            if(r == 1) { // coda in fase di uscita
                                fprintf(stderr, "EXITING\n");
                            }
                            sleep(delay);
                        }
                        if(pathname != NULL) free(pathname);
                    }
                }
            }
        }
    }
    free(dir);  
}

/**
 * @brief Aggiunge al threadpool il task per inviare al Collector il messaggio di stampa,
 *        che indica al Collector di stampare i risultati ottenuti fino a quel momento
 * @param pfd1 file descriptor della pipe per scrittura
*/
void printSignal(int pfd1) {
    char* printMsg = (char*)malloc(6*sizeof(char));
    memset(printMsg, 0, 6*sizeof(char));
    strncpy(printMsg, "print", 6);
    int n;
    CHECK_EQ_EXIT("write", n = writen(pfd1, printMsg, strlen(printMsg)+1), -1, "write: errno=%d\n", errno);
    free(printMsg);
}

/**
 * @brief Aggiunge al threadpool il task per inviare al Collector il messaggio di terminazione.
 *        Se la terminazione è causata da un segnale allora setta sigFlag a 1.
 * @returns -1 se non esiste il threadpool, 0 altrimenti.
 * @param workers threadpool dei worker
 * @param fileNum numero di file che il Collector dovrebbe ricevere
 * @param sig 1 se la terminazione è stata causata da un segnale
 *            0 terminazione regolare
 */
int exitMessage(threadpool_t *workers, int fileNum, int sig) {
    if(workers == NULL) {
        return -1;
    }
    if(sig == 1) sigFlag = 1;
    LOCK(&(workers->lock));
    while(workers->count != 0 || workers->taskonthefly != 0) {  //aspetta il completamento dei task in esecuzione e in attesa 
        pthread_cond_wait(&(workers->condEnd), &(workers->lock));
    }
    UNLOCK(&(workers->lock));
    //manda un messaggio al collector per terminare
    worker_arg* signal_args = malloc(sizeof(worker_arg));
    memset((*signal_args).filename, 0, BUFLEN*sizeof(char));
    strncpy((*signal_args).filename, "exit", 8);
    (*signal_args).fileNum = fileNum;
    int r = addToThreadPool(workers, worker, (void*) signal_args);
    if (r<0) { // errore interno
        fprintf(stderr, "FATAL ERROR, adding to the thread pool\n");
    }
    if(r == 1) { // coda in fase di uscita
        fprintf(stderr, "EXITING\n");
    }
    return 0;
}