#if !defined(_MESSAGE_H)
#define _MESSAGE_H

#define BUFLEN 255

/** 
 * tipo del messaggio
 */
typedef struct msg {
    long num;       //risultato del calcolo sul file
    char str[255];  //path del file
} msg_t;

#endif /* _WORKER_H */
