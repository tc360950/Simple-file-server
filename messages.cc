#include "messages.h"
#include <iostream>


void mess_ntoh(simpl_cmd_t *m) {
    m->cmd_seq = be64toh(m->cmd_seq);
}

void mess_ntoh(cmplx_cmd_t *m) {
    m->cmd_seq = be64toh(m->cmd_seq);
    m->param = be64toh(m->param);
}

void prepare_simpl_mess(const char *mess, uint64_t cmd_seq, simpl_cmd_t *m) {
    std::memset(m->cmd, 0, 10);
    std::memcpy(m->cmd, mess, strlen(mess));
    m->cmd_seq = htobe64(cmd_seq);
}

void prepare_cmplx_mess(const char *mess, uint64_t cmd_seq, uint64_t param, cmplx_cmd_t *m) {
    std::memset(m->cmd, 0, 10);
    std::memcpy(m->cmd, mess, strlen(mess));
    m->cmd_seq = htobe64(cmd_seq);
    m->param = htobe64(param);
}
