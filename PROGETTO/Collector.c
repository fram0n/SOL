#define _XOPEN_SOURCE 500

#include <util.h>
#include <message.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#define BUFLEN 255
#define SOCKNAME "./farm.sck"

static msg_t* resArray;         //array che contiene i risultati del calcolo
static int len = 0;             //variabile che tiene traccia del numero di elementi in resArray
static int exitFlag = 0;        //se exitFlag == 1 è stato ricevuto un segnale di terminazione
static int fileRec = 0;         //numero di file ricevuti
static int fileToRec = -1;      //numero di file che il Collector deve ricevere 

//funzione da passare al qsort
static int cmpfunc (const void *a, const void *b);
//funzione che si occupa di mettere in ordine i risultati e stamparli
static void sortAndPrint();

void collector(int ssfd, int pfd0) {
    sigset_t sigset;    //Il processo Collector maschera tutti i segnali gestiti dal processo MasterWorker.
    CHECK_EQ_EXIT("sigemptyset", sigemptyset(&sigset), -1, "sigemptyset: errno=%d\n", errno);
    CHECK_EQ_EXIT("sigaddset", sigaddset(&sigset, SIGHUP), -1, "sigaddset: SIGHUP", NULL);
    CHECK_EQ_EXIT("sigaddset", sigaddset(&sigset, SIGINT), -1, "sigaddset: SIGINT", NULL);
    CHECK_EQ_EXIT("sigaddset", sigaddset(&sigset, SIGQUIT), -1, "sigaddset: SIGQUIT", NULL);
    CHECK_EQ_EXIT("sigaddset", sigaddset(&sigset, SIGTERM), -1, "sigaddset: SIGTERM", NULL);
    CHECK_EQ_EXIT("sigaddset", sigaddset(&sigset, SIGUSR1), -1, "sigaddset: SIGUSR1", NULL);
    CHECK_NEQ_EXIT("pthread_sigmask", pthread_sigmask(SIG_SETMASK, &sigset, NULL), 0, "pthread_sigmask", NULL);

    //ignoro SIGPIPE
    struct sigaction s;
    memset(&s, 0, sizeof(s));    
    s.sa_handler = SIG_IGN;
    CHECK_EQ_EXIT("sigaction", sigaction(SIGPIPE, &s, NULL), -1, "sigaction: errno=%d\n", errno);

    char* currDir = malloc(BUFLEN*sizeof(char));    //path della directory corrente
    memset(currDir, 0, BUFLEN*sizeof(char));
    CHECK_EQ_EXIT("getcwd", getcwd(currDir, BUFLEN*sizeof(char)), NULL, "getcwd: errno=%d\n", errno);
    char* token;
    token = strtok(currDir, "/");
    char* currDirName = malloc(BUFLEN*sizeof(char));    //nome della directory corrente
    while(token != NULL) {
        memset(currDirName, 0, BUFLEN*sizeof(char));
        strncpy(currDirName, token, BUFLEN*sizeof(char));
        token = strtok(NULL, "/");
    }

    int fd_c;
    int fd_num = 0; /* max fd attivo */
    int fd;         /* indice per verificare risultati select */

    fd_set set; /* l’insieme dei file descriptor attivi */
    fd_set rdset; /* insieme fd attesi in lettura */
    int nread; /* numero caratteri letti */

    int notused;
    SYSCALL_EXIT("listen", notused, listen(ssfd, SOMAXCONN), "listen: errno=%d\n", errno);

    resArray = malloc(sizeof(msg_t));

    /* mantengo il massimo indice di descrittore attivo in fd_num */
    if (ssfd > fd_num) fd_num = ssfd;
    if (pfd0 > fd_num) fd_num = pfd0;
    FD_ZERO(&set);
    FD_SET(ssfd, &set);
    FD_SET(pfd0, &set);
    while (!exitFlag) {
        rdset = set; /* preparo maschera per select */
        int r;
        SYSCALL_EXIT("select", r , select(fd_num+1,&rdset,NULL,NULL,NULL), "select: errno=%d\n", errno);
        /* select OK */
        for (fd = 0; fd<=fd_num; fd++) {
            if (FD_ISSET(fd, &rdset)) {
                if (fd == pfd0) {
                    char* str = malloc(6*sizeof(char));
                    memset(str, 0, 6*sizeof(char));
                    int nread;
                    CHECK_EQ_EXIT("read", nread = readn(pfd0, str, 6), -1, "read: errno=%d\n", errno);
                    if (strncmp(str, "print", 6) == 0) {
                        sortAndPrint();
                    }
                    free(str);
                }
                else if (fd == ssfd) {/* sock connect pronto */
                    SYSCALL_EXIT("accept", fd_c, accept(ssfd, NULL, 0), "accept: errno=%d\n", errno);
                    FD_SET(fd_c, &set);
                    if (fd_c > fd_num) fd_num = fd_c;
                }
                else {/* sock I/0 pronto */
                    fd_c = fd;
                    msg_t msg;
                    memset(msg.str, '\0', BUFLEN);
                    CHECK_EQ_EXIT("readn", nread = readn(fd_c, &msg, sizeof(msg_t)), -1, "read: errno=%d\n", errno);
                    if(strcmp(msg.str, "exit") == 0) {
                        fileToRec = msg.num;
                        if(fileToRec == fileRec) {
                            exitFlag = 1;
                            FD_CLR(fd_c, &set);
                            close(fd_c);  
                        }
                    }
                    else {
                        fileRec++;
                        //passa dal path assoluto al path in cui c'è solo la directory passata come input al main o solo il nome del file 
                        char* s = malloc(BUFLEN*sizeof(char));
                        memset(s, 0, BUFLEN*sizeof(char));
                        char* token;
                        char* temp = malloc(BUFLEN*sizeof(char));
                        memset(temp, 0, BUFLEN*sizeof(char));
                        strcpy(temp, msg.str);
                        token = strtok(temp, "/");
                        while(strcmp(token, currDirName) != 0) {
                            token = strtok(NULL, "/");
                        }
                        token = strtok(NULL, "/");
                        while(token != NULL) {
                            strcat(s, token);
                            strcat(s, "/");
                            token = strtok(NULL, "/");
                        }
                        s[strlen(s)-1] = '\0';
                        memset(msg.str, 0, BUFLEN*sizeof(char));
                        strcpy(msg.str, s);
                        len++;
                        resArray = realloc(resArray, len*sizeof(msg_t));
                        resArray[len-1] = msg;
                        if(fileToRec == fileRec) {
                            exitFlag = 1;
                            FD_CLR(fd_c, &set);
                            close(fd_c);  
                        }
                        free(s);
                        free(temp);
                    }
                }
            }
        }
    }

    sortAndPrint();
    free(resArray);
    free(currDir);
    free(currDirName);
    unlink(SOCKNAME);
    return;
}

static void sortAndPrint() {
    qsort(resArray, len, sizeof(msg_t), cmpfunc);
    for(int i=0; i<len; i++) {
        printf("%ld %s\n", resArray[i].num, resArray[i].str);
    }
}

static int cmpfunc (const void *a, const void *b) {
    msg_t *A = (msg_t *)a;
    msg_t *B = (msg_t *)b;
    
    if(A->num - B->num < 0) return -1;
    if(A->num - B->num > 0 ) return 1;
    return 0;
}