#define _GNU_SOURCE
#define _POSIX_SOURCE

#include <util.h>
#include <MasterThread.h>
#include <threadpool.h>
#include <Worker.h>
#include <Collector.h>
#include <sys/wait.h>

#define UNIX_PATH_MAX 108
#define SOCKNAME "./farm.sck"

static long nthread = 4;            //numero di thread nel pool
static long qlen = 8;               //lunghezza della lista dei task pendenti
static long delay = 0;              //tempo (ms) che intercorre tra l’invio delle richieste ai thread Worker
static char* dirName;               //nome della directory in cui sono presenti dei file su cui eseguire il calcolo

static threadpool_t *workers;       //threadpool dei worker
static int termina = 0;             //se termina == 1 indica al thread che gestisce i segnali ed al main che devono terminare
static int fileNum = 0;             //numero di file inviati al Collector
static int pfd[2];                  //file descriptors della pipe (pfd[0]: lettura, pfd[1]: scrittura)
static int pid;                     //identifica il processo Collector
static int ssfd;                    //file descriptor della socket
static pthread_t signalThread = 0;  //thread che gestisce i segnali

//Controlla che i parametri passati come input siano corretti
static void checkargs(int argc, char* argv[]);
//stampa il modo corretto in cui scrivere l'istruzione da riga di comando
static void usage(const char*argv0);
//funzione passata al thread che gestisce i segnali
static void* handler(void* args);
//libera la memoria e fa terminare il programma
static void terminate();

int main(int argc, char* argv[]) {
    sigset_t set;
    if(sigfillset(&set) == -1) {
        perror("sigfillset");
        return -1;
    }
    if(pthread_sigmask(SIG_SETMASK, &set, NULL) != 0) {
        perror("pthread_sigmask");
        return -1;
    }

    //ignoro SIGPIPE
    struct sigaction s;
    memset(&s, 0, sizeof(s));    
    s.sa_handler = SIG_IGN;
    if(sigaction(SIGPIPE, &s, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    //creo la pipe
    if(pipe(pfd) == -1) { 
        perror("pipe");
        return -1;
    }

    
    unlink(SOCKNAME);
    struct sockaddr_un sa;
    strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    if((ssfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        terminate();
        return -1;
    }
    if(bind(ssfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        perror("bind");
        terminate();
        return -1;
    }
    
    //se è stato ricevuto un segnale di terminazione termino il programma
    if(termina) {
        terminate();
        return 0;
    }

    if((pid = fork()) == -1) {
        perror("fork");
        terminate();
        return -1;
    }
    if(pid == 0) {  //figlio Collector
        close(pfd[1]);  //chiude scrittura pipe
        collector(ssfd, pfd[0]);
    }
    else {  //padre MasterWorker
        //creo il thread che gestisce i segnali
        if(pthread_create(&signalThread, NULL, &handler, NULL) != 0) {
            perror("pthread_create");
            return -1;
        }
        checkargs(argc, argv);
        if(termina) {   //se arriva un segnale di terminazione prima della creazione del threadpool termina
            terminate();
            return 0;
        }
        //creo il threadpool
        if((workers = createThreadPool(nthread, qlen, sa)) == NULL) {
            perror("createThreadPool");
            terminate();
            return -1;
        }
        LOCK_RETURN(&(workers->lock), -1);
        while(workers->connected != nthread) {  //aspetta che tutti i thread si siano connessi al Collector
            pthread_cond_wait(&(workers->condConn), &(workers->lock));
        }
        UNLOCK_RETURN(&(workers->lock), -1);
        int i = 1;
        while(!termina && i<argc) {     //controlla i file passati da input e li invia ai worker
            if(EndsWithDat(argv[i])) {
                char* pathname = realpath(argv[i], NULL);
                if(pathname != NULL) {
                    if(isRegular(pathname)) {
                        fileNum++;
                        worker_arg* w_args = malloc(sizeof(worker_arg));        //preparo gli argomenti da passare al thread worker
                        memset((*w_args).filename, 0, BUFLEN*sizeof(char));
                        strncpy((*w_args).filename, pathname, strlen(pathname));

                        int r = addToThreadPool(workers, worker, (void*) w_args);
                        if (r<0) { // errore interno
                            fprintf(stderr, "FATAL ERROR, adding to the thread pool\n");
                        }
                        if(r == 1) { // coda in fase di uscita
                            fprintf(stderr, "EXITING\n");
                        }
                        sleep(delay/1000);
                    }
                }
                free(pathname);
            }
            i++;
        } 
        if(termina) {
            terminate();
            return 0;
        }
        arg_struct args;                    //preparo gli argomenti da passare al master thread
        args.workerThreadpool = workers;
        args.dir = dirName;
        args.delay = delay/1000;
        args.pfd1 = pfd[1];
        args.fileNum = fileNum;
        close(pfd[0]);  //chiude lettura pipe
        pthread_t master;
        //creo il MasterThread
        if(pthread_create(&master, NULL, &threadMaster, (void*)&args) != 0) {
            perror("pthread_create");
            terminate();
            return -1;
        }
        CHECK_NEQ_EXIT("pthread_join", pthread_join(master, NULL), 0, "pthread_join (threadMaster): errno=%d\n", errno);
        if(waitpid(pid, NULL, 0) == -1) {   //aspetta la terminazione del Collector
            perror("waitpid");
            terminate();
            return -1;
        }
        termina = 1;
        kill(getpid(), SIGINT);
        CHECK_NEQ_EXIT("pthread_join", pthread_join(signalThread, NULL), 0, "pthread_join (signalThread): errno=%d\n", errno);
        destroyThreadPool(workers, 0);
        unlink(SOCKNAME);
        return 0;
    }
}

static void usage(const char*argv0) {
    fprintf(stderr, "use: %s <-n <nthread>> <-q <qlen>> <-t <delay>> <lista file> <-d <directory-name>>\n\n", argv0);
}

static void checkargs(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        _exit(EXIT_FAILURE);
    }
    int opt;
    int count_opt = 0;
    while((opt = getopt(argc, argv, "n:q:d:t:")) != -1) {
        struct stat info;
        switch (opt) {
			case 'n':
                isNumber(optarg, &nthread);
                count_opt++;
				break;
			case 'q':
                isNumber(optarg, &qlen);
                count_opt++;
				break;
            case 'd':
                if(stat(optarg, &info) != -1) {
                    if(S_ISDIR(info.st_mode)) {
                        dirName = optarg;
                    }
                }
                count_opt++;
				break;
            case 't':
                isNumber(optarg, &delay);
                count_opt++;
				break;
            case '?':
                usage(argv[0]);
                _exit(EXIT_FAILURE);
                break;
		}
    }
}

static void* handler(void* args) {
    sigset_t set;
    int sig;
    if(sigemptyset(&set) == -1) {
        termina = 1;
        perror("sigemptyset");
        return NULL;
    }
    if(sigaddset(&set, SIGHUP) == -1) {
        termina = 1;
        perror("sigaddset (SIGHUP)");
        return NULL;
    }
    if(sigaddset(&set, SIGINT) == -1) {
        termina = 1;
        perror("sigaddset (SIGINT)");
        return NULL;
    }
    if(sigaddset(&set, SIGQUIT) == -1) {
        termina = 1;
        perror("sigaddset (SIGQUIT)");
        return NULL;
    }
    if(sigaddset(&set, SIGTERM) == -1) {
        termina = 1;
        perror("sigaddset (SIGTERM)");
        return NULL;
    }
    if(sigaddset(&set, SIGUSR1) == -1) {
        termina = 1;
        perror("sigaddset (SIGUSR1)");
        return NULL;
    }
    if(pthread_sigmask(SIG_SETMASK, &set, NULL) != 0) {
        termina = 1;
        perror("pthread_sigmask");
        return NULL;
    }
    while(!termina) {
        int r;
        sigwait(&set, &sig);
        switch(sig) {
            case SIGHUP:
                printf("\nricevuto segnale: SIGHUP\n");
                termina = 1;
                r = exitMessage(workers, fileNum, 1);
                if(r == -1) {
                    return NULL;
                }
                break;
            case SIGINT:
                if(termina != 1) {
                    printf("\nricevuto segnale: SIGINT\n");
                    termina = 1;
                    r = exitMessage(workers, fileNum, 1);
                    if(r == -1) {
                        return NULL;
                    }
                }
                break; 
            case SIGQUIT:
                printf("\nricevuto segnale: SIGQUIT\n");
                termina = 1;
                r = exitMessage(workers, fileNum, 1);
                if(r == -1) {
                    return NULL;
                }
                break;
            case SIGTERM:
                printf("\nricevuto segnale: SIGTERM\n");
                termina = 1;
                r = exitMessage(workers, fileNum, 1);
                if(r == -1) {
                    return NULL;
                }
                break;
            case SIGUSR1:
                printf("ricevuto segnale: SIGUSR1\n");
                printSignal(pfd[1]);
                break;
        }
    }
    return NULL;
}

static void terminate() {
    if(pid) kill(pid, SIGKILL); //termina il Collector
    //elimina handler
    termina = 1;
    kill(getpid(), SIGINT);
    if(signalThread) {
        CHECK_NEQ_EXIT("pthread_join", pthread_join(signalThread, NULL), 0, "pthread_join (signalThread): errno=%d\n", errno);
    }
    if(ssfd) close(ssfd);
    if(workers != NULL) destroyThreadPool(workers, 1);
    unlink(SOCKNAME);
}