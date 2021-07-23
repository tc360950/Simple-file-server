#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>
#include <cstring>
#include <endian.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <iostream>

#define END_OF_STRING '\0'
#define CMD_SIZE 10

typedef struct __attribute__((__packed__)) {
  char cmd[10];
  uint64_t cmd_seq;
  /** char[] data **/
} simpl_cmd_t;

typedef struct __attribute__((__packed__)) {
  char cmd[10];
  uint64_t cmd_seq;
  uint64_t param;
  /** char[] data**/
} cmplx_cmd_t;


void prepare_simpl_mess(const char *mess, uint64_t cmd_seq, simpl_cmd_t *m);


void prepare_cmplx_mess(const char *mess, uint64_t cmd_seq, uint64_t param, cmplx_cmd_t *m);


void mess_ntoh(simpl_cmd_t *m);


void mess_ntoh(cmplx_cmd_t *m);


template <class M> std::string get_message_data(char *buffer, ssize_t len) {
        return std::string(buffer + sizeof(M), len - sizeof(M));
}

#endif //MESSAGES_H
