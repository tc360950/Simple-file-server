#ifndef UDP_H
#define UDP_H
#include<chrono>
#include "../blocking_concurrent_queue.h"
#include "utilities.h"
#include <atomic>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

#define MAX_UDP_PACKET_SIZE 65507
#define TTL_VALUE 4


typedef std::chrono::steady_clock::time_point  time_pt;

typedef std::function<bool(ssize_t, struct sockaddr_in, socklen_t)> handler_t;

std::atomic<bool> signal{false};

namespace tcpUploaders {
    /** Tutaj watki uploadujace pliki beda otrzymywać odpowiedzi od serwerów **/
    BlockingConcurrentQueue<std::tuple<cmplx_cmd_t, time_pt, struct sockaddr_in>> mess_buffer;

    void upload(const std::string &file, const std::string &path);
}


namespace UDPMessages {

    std::atomic<uint64_t> last_cmd_seq{1};

    char buffer[MAX_UDP_PACKET_SIZE + 1];

    int udp_sock;

    int time_out;

    std::mutex semaphore;

	int Timer;

    int arm_timer(int timeout) {
        struct timespec t;
        t.tv_sec = timeout;
        t.tv_nsec = 0;
        struct timespec t_zero;
        t_zero.tv_nsec = 0;
        t_zero.tv_sec = 0;
        struct itimerspec it;
        it.it_interval = t_zero;
        it.it_value = t;
        if (timerfd_settime(Timer,0, &it, 0) < 0) {
            print::error("Can't set timer");
        }
        return Timer;
    }

    bool send_udp(ssize_t len, struct sockaddr_in remote_address) {
        std::lock_guard<std::mutex> lck(semaphore);
        bool valid = sendto(udp_sock, buffer, len, 0 , (struct sockaddr *) &remote_address, sizeof(remote_address)) == len;
        if (!valid)
            print::error("Can't send UDP packet");
        return valid;
    }

    bool send_udp(char *buff, ssize_t len, struct sockaddr_in remote_address) {
        std::lock_guard<std::mutex> lck(semaphore);
        bool valid = sendto(udp_sock, buff, len, 0 , (struct sockaddr *) &remote_address, sizeof(remote_address)) == len;
        return valid;
    }

    std::pair<bool, uint64_t> send_hello(struct sockaddr_in remote_address) {
        simpl_cmd_t mess;
        uint64_t cmd_num = last_cmd_seq++;
        prepare_simpl_mess("HELLO", cmd_num, &mess);
        memcpy(buffer, &mess, sizeof(simpl_cmd_t));
        bool valid = send_udp( sizeof(simpl_cmd_t), remote_address);
        return std::make_pair(valid, cmd_num);
    }

    std::pair<bool, uint64_t> send_list(struct sockaddr_in remote_address, const std::string &pattern) {
        simpl_cmd_t mess;
        uint64_t cmd_num = last_cmd_seq++;
        prepare_simpl_mess("LIST", cmd_num, &mess);
        memcpy(buffer, &mess, sizeof(simpl_cmd_t));
        memcpy(buffer +sizeof(simpl_cmd_t), pattern.data(), pattern.size());
        bool valid = send_udp( sizeof(simpl_cmd_t) + pattern.size(), remote_address);
        return std::make_pair(valid, cmd_num);
    }

    std::pair<bool, uint64_t> send_get(const std::string &file, struct sockaddr_in remote_address) {
        simpl_cmd_t mess;
        uint64_t cmd_num = last_cmd_seq++;
        prepare_simpl_mess("GET", cmd_num, &mess);
        memcpy(buffer, (void*) &mess, sizeof(simpl_cmd_t));
        memcpy(buffer +sizeof(simpl_cmd_t), file.data(), file.size());
        bool valid = send_udp( sizeof(simpl_cmd_t) + file.size(), remote_address);
        return std::make_pair(valid, cmd_num);
    }

    std::pair<bool, uint64_t> send_delete(const std::string &file, struct sockaddr_in remote_address) {
        simpl_cmd_t mess;
        uint64_t cmd_num = last_cmd_seq++;
        prepare_simpl_mess("DEL", cmd_num, &mess);
        memcpy(buffer, (void*) &mess, sizeof(simpl_cmd_t));
        memcpy(buffer +sizeof(simpl_cmd_t), file.data(), file.size());
        bool valid = send_udp( sizeof(simpl_cmd_t) + file.size(), remote_address);
        return std::make_pair(valid, cmd_num);
    }

    std::pair<bool,uint64_t> send_add(char *buff, std::string file, uint64_t size, struct sockaddr_in remote_address) {
        cmplx_cmd_t mess;
        uint64_t cmd_num = last_cmd_seq++;
        prepare_cmplx_mess("ADD",cmd_num, size, &mess);
        memcpy(buff, (void*) &mess, sizeof(cmplx_cmd_t));
        memcpy(buff +sizeof(cmplx_cmd_t), file.data(), file.size());
        bool valid = send_udp(buff, sizeof(cmplx_cmd_t) + file.size(), remote_address);
        return std::make_pair(valid, cmd_num);
    }

    bool push_into_queue(ssize_t read, struct sockaddr_in remote) {
        cmplx_cmd_t *m = (cmplx_cmd_t *) buffer;
        if (m->cmd[9] != '\0') {
            return false;
        }
        std::string ms(m->cmd);
        if (ms != std::string("NO_WAY")) {
          if (read != sizeof(cmplx_cmd_t))
              return false;
          if (ms != std::string("CAN_ADD"))
              return false;
        }
        time_pt t = std::chrono::steady_clock::now();
        tcpUploaders::mess_buffer.enqueue({*m, t, remote});
        return true;
    }

    /**
     * Zwraca @true jezeli powinnismy przestac przesylac dalsze pakiety do Handler'a.
     */
    bool read_and_handle(handler_t MessHandler) {
        struct sockaddr_in remote_address;
        socklen_t addr_s = sizeof(remote_address);

        ssize_t read_ = recvfrom(udp_sock, buffer, MAX_UDP_PACKET_SIZE,0 ,(struct sockaddr *) &remote_address, &addr_s);

        if (read_ <= 0) {
            print::error("Error read socket");
            return false;
        } else {
             // sprawdz czy jest to wiadomosc dla uploader'ow
             if (push_into_queue(read_, remote_address)) {
                return false;
             }
             return MessHandler(read_, remote_address, addr_s);
        }
    }

    bool wait_on_descriptor(int desc, int timer) {
        struct pollfd fds[2];
        fds[0].fd = desc;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = timer;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        int event = poll (fds, 2, -1);
        return (event > 0 && fds[1].revents == 0);
    }

    /**
     * Przez @time_out sekund odbiera wiadomosci z gniazda UDP,
     * przekazujac je funkcji @MessHandler
     */
    void wait_for_packet( handler_t MessHandler) {
        int timer = arm_timer(time_out);
        if (timer < 0)
            return;

        bool abandon = false;

        while (!abandon) {
            memset(buffer, 0, MAX_UDP_PACKET_SIZE + 1);
            if (wait_on_descriptor(udp_sock, timer)) {
                bool stop = read_and_handle(MessHandler);
                if (stop)
                    abandon = true;
            } else {
                abandon = true;
            }
        }
    }

    void prepare_connection() {
        udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_sock < 0)
            throw std::runtime_error("Creating socket");

        int  optval = 1;
        if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof optval) < 0)
            throw std::runtime_error("setsockopt broadcast");

        optval = TTL_VALUE;
        if (setsockopt(udp_sock, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&optval, sizeof optval) < 0)
            throw std::runtime_error("TTL");

        struct sockaddr_in local_address;
        local_address.sin_family = AF_INET;
        local_address.sin_addr.s_addr = htonl(INADDR_ANY);
        local_address.sin_port = htons(0);
        if (bind(udp_sock, (struct sockaddr *)&local_address, sizeof local_address) < 0)
            throw std::runtime_error("bind");

    }
    /**
     * @exactSize - jesli true, to odebrana wiadomosci powinna miec dokladnie dlugosc sizeof(M)
     * W chwili wywolania odebrana wiadomosc powinna znajdowac sie w UDPMessages::buffer
     */
    template <class M>  bool check_cmd_correctness(struct sockaddr_in rem, bool exactSize, const char *s, ssize_t size, uint64_t cmd, M* &m) {
        if ((exactSize && size != (ssize_t) sizeof(M) ) || size < (ssize_t) sizeof(M)) {
            print::standard_info(print::InfoType::Skipping, rem, "Wrong package size","");
            return false;
        }
        M *mess = (M*) UDPMessages::buffer;
        mess_ntoh(mess);
        if (cmd != mess->cmd_seq) {
            print::standard_info(print::InfoType::Skipping, rem, "Wrong cmd sequence","");
            return false;
        }
        std::string infoConnect(mess->cmd,10);
        if (infoConnect != std::string("CONNECT_ME")) {
            if (mess->cmd[CMD_SIZE - 1] != END_OF_STRING ) {
                    std::string cmd_str{mess->cmd,10};
                    std::string error_mes("Cmd too long (");
                    error_mes.append(cmd_str).append(")");
                    print::standard_info(print::InfoType::Skipping, rem, error_mes,"");
                    return false;
            }
            std::string info(mess->cmd);
            std::string required_info(s);
            if (s != info) {
                std::string error_mes("Wrong cmd, got: ");
                error_mes.append(info).append(", required: ").append(s);
                print::standard_info(print::InfoType::Skipping, rem, error_mes,"");
                return false;
            }
        }
        m = mess;
        return true;
    }

    void __init__() {
		prepare_connection();
		Timer = timerfd_create(CLOCK_REALTIME, 0);
        if (Timer < 0)
			print::error("Timer creat");
	}
}



namespace daemonReader {
    std::mutex daemon_barrier;
    const std::chrono::milliseconds sleep_time{10}; //milisekundy
    char buffer[MAX_UDP_PACKET_SIZE + 1];

    bool dummyHandler(ssize_t q, struct sockaddr_in r, socklen_t y ) {
        (void)q;
        (void)r;
        (void)y;
        return true;
    }

    void read() {
        struct pollfd fds;
        fds.fd = UDPMessages::udp_sock;
        fds.events = POLLIN;
        fds.revents = 0;
        int event = poll (&fds, 1, 0);
        if (event > 0) {
            UDPMessages::read_and_handle(handler_t(dummyHandler));
        }
    }

    void daemon() {
        while(true) {
            {
                if (signal.load()) {
                    return;
                }
                std::lock_guard<std::mutex> lck(daemon_barrier);
                read();
            }
            std::this_thread::sleep_for(sleep_time);
        }
    }
}








#endif
