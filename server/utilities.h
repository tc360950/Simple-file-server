#ifndef SERVER_UTILITIES_H
#define SERVER_UTILITIES_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <poll.h>

#define QUEUE_LENGTH 5


namespace error {
        /** string for storage of error messages **/
        std::string error_mess{""};

        std::mutex semaphore;

        void error(std::string e) {
             std::lock_guard<std::mutex> lck(semaphore);
             std::cerr << e << std::endl;
        }
        void error() {
            error(error_mess);
        }

    std::string compose_error_mess(struct sockaddr_in src_addr)  {
        std::string s = "[PCKG ERROR] Skipping invalid package from ";
        std::string port = std::to_string(ntohs(src_addr.sin_port));
        std::string ip_{(char*)inet_ntoa((struct in_addr)src_addr.sin_addr)};
        s.append(ip_);
        s.append(":");
        s.append(port);
        s.append(". ");
        s.append(error::error_mess);
        return s;
    }


}

bool waitOnSocket(int sock, int timeout, int eventFds) {
            struct pollfd fds[2];
            fds[0].events = POLLIN;
            fds[0].fd = sock;
            fds[0].revents = 0;
            fds[1].events = POLLIN;
            fds[1].fd = eventFds;
            fds[1].revents = 0;
            int event = poll(fds, 2, timeout*1000);
            return event > 0 && fds[1].revents == 0;
}

std::pair<int, int> prepareTCPConnection(){
        struct sockaddr_in server_address;
        int sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock < 0)
          error::error("Error creating tcp socket!");
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        server_address.sin_port = htons(0);
        if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
          error::error("bind error");
        if (listen(sock, QUEUE_LENGTH) < 0)
          error::error("listen error");


        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        if (getsockname(sock, (struct sockaddr *)&sin, &len) < 0) {
            error::error("Get sock name");
        }
        return std::make_pair(sock, ntohs(sin.sin_port));
    }


void prepareConnection(int &udp_sock) {

          udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
          if (udp_sock < 0)
            throw std::runtime_error("Creating socket");
        struct sockaddr_in local_address;
        struct ip_mreq ip_mreq;
        memset(&ip_mreq,0, sizeof(struct ip_mreq));

          ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
          if (inet_aton(PARAMS::MCAST_ADRR.c_str(), &ip_mreq.imr_multiaddr) <= 0)
            throw std::runtime_error("Inet aton error");
          if (setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
            throw std::runtime_error("Set sock option");
             int optval = 0;
        if (setsockopt(udp_sock, SOL_IP, IP_MULTICAST_LOOP, (void*)&optval, sizeof optval) < 0)
                throw std::runtime_error("setsockopt loop");

          local_address.sin_family = AF_INET;
          local_address.sin_addr.s_addr = htonl(INADDR_ANY);
          local_address.sin_port = htons(PARAMS::CMD_PORT);
          if (bind(udp_sock, (struct sockaddr *)&local_address, sizeof local_address) < 0)
            throw std::runtime_error("bind");
}

#endif
