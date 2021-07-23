#include "server-node.h"

#include <cstring>
#include <stdexcept>
#include <thread>
#include <csignal>
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include "utilities.h"
#include <sys/eventfd.h>
#include <atomic>
#include <signal.h>

#define LOCAL_BUFFER_SIZE 65000
#define MAX_UDP_SIZE 65527


namespace Server {

    std::atomic<bool> signal{false};

    void catchSignal(int sig);

    class threadOwner {

        std::vector<std::thread> workers;

        int event; //event descriptor, ktory beda pollowac wszystkie watki w trakcie pollowania gniazda

        public:
        ~threadOwner () {
            for (auto &t: workers) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }

        void __init__ () {
            event = eventfd(0,0);

            struct sigaction action;
            sigset_t block_mask;

            sigemptyset (&block_mask);
            action.sa_handler = catchSignal;
            action.sa_mask = block_mask;
            action.sa_flags = 0;
            if (sigaction (SIGINT, &action, 0) == -1)
                error::error("sigaction");

        }
        void handleSigInt() {
            uint64_t r = 1;
            ssize_t write_ok = write(event, &r, sizeof(uint64_t));
            signal.store(true);
            if (write_ok <= 0) return;
            for (auto &t: workers) {
                if (t.joinable()) {
                    t.join();
                }
            }
            exit(130);
        }
        void launch_tcp_worker(int sock, int fDescriptor, FileManager &fileManager);

        void launch_tcp_worker(int sock, std::string file, uint64_t size, int64_t token, FileManager &fileManager);


    };

    threadOwner threadMaster;

    void catchSignal(int sig) {
        (void)sig;
        threadMaster.handleSigInt();
    }


    enum class udpRequests {Hello, List, Del, Get, Add, Error};

    class responseHandler;

     /** dane klienta **/
    struct sockaddr_in src_addr;
    socklen_t addrlen;


 class ServerNode {
        private:

            int udp_sock;

            FileManager fileManager;

            /* bufor do wiadomosci wysylanych przez UDP,
               ostatni bajt zawsze rowny zero*/
            char buffer[MAX_UDP_SIZE + 1];

            /**
             * <Descriptor, Port num>
             */
            std::pair<int,int> prepareTCPConnection();

        public:

            ServerNode(): fileManager{PARAMS::MAX_SPACE,PARAMS::SHRD_FLDR} {}

            void __init__() {
                prepareConnection(this->udp_sock);
                fileManager.__init__();
            }

            void listen_();

        private:

            enum udpRequests deduceMessType(const ssize_t rec_size);

            /**
             * Zakłada, że wiadomość jest już w buforze, a adres klienta w
             * @src_addr. @send_size - rozmiar wiadomości w buforze.
             * Zwraca @true w przypadku udanego wyslania.
            **/
            bool sendUDPPacket(ssize_t send_size) {
                if (sendto(udp_sock, buffer,send_size,0,(struct sockaddr *)&src_addr, addrlen) != send_size) {
                    error::error("Can't send UDP packet");
                    return false;
                }
                return true;
            }

        friend class responseHandler;
   };

    class responseHandler {
        private:
            static void send_file_list(const std::vector<std::string> &fileNames, ServerNode &server, simpl_cmd_t *mess) {
                std::string data;
                uint64_t size_so_far = sizeof(simpl_cmd_t);
                bool buffer_flushed = false;
                for (auto &f: fileNames) {
                    if (can_add(size_so_far, f)) {
                        buffer_flushed = false;
                        if (size_so_far > sizeof(simpl_cmd_t)) {
                            data.append("\n");
                            size_so_far ++;
                        }
                        data.append(f);
                        size_so_far += f.size();
                    } else {
                        mess_into_buffer(server.buffer, "MY_LIST", data, mess->cmd_seq);
                        if (!server.sendUDPPacket(size_so_far)) {
                            error::error("Error while sending file list through UDP socket");
                            return;
                        }
                        buffer_flushed = true;
                        size_so_far = sizeof(simpl_cmd_t);
                        data.clear();
                    }
                }
                if(!buffer_flushed) {
                    mess_into_buffer(server.buffer, "MY_LIST", data, mess->cmd_seq);
                    if (!server.sendUDPPacket(size_so_far)) {
                        error::error("Error while sending file list through UDP socket");
                        return;
                    }
                }
            }

            static void add_send_decline(ServerNode &server, cmplx_cmd_t *mess, const std::string &file) {
                auto send_size = mess_into_buffer(server.buffer, "NO_WAY",file, mess->cmd_seq);
                server.sendUDPPacket(send_size);
            }

           static void get_send_response(ServerNode &server, const std::string &file,const int port) {
                simpl_cmd_t *mess = (simpl_cmd_t*) server.buffer;
                mess_ntoh(mess);
                size_t send_size = mess_into_buffer(server.buffer, "CONNECT_ME",file,mess->cmd_seq,port);
                server.sendUDPPacket(send_size);
            }

           static bool can_add(const uint64_t size_so_far, const std::string &f) {
                    return size_so_far + 1 + f.size() <= PARAMS::MAX_UDP_PACKET_SIZE;
            }

            /* otwiera plik, ktrego nazwa zapisana jest w pierwszych @len bajtach bufora @buffer */
            static std::pair<int, std::string> openFile(ServerNode &server, char *buffer, ssize_t len) {
                    std::string file(buffer, len);
                    int fd = server.fileManager.openFile(file);
                    if (fd == -1) {
                        error::error_mess = std::string("File does not exist.");
                        error::error(error::compose_error_mess(Server::src_addr));
                    }
                    return std::make_pair(fd, file);
            }

             static std::pair<bool, std::string> addFileIsOk(ServerNode &server, ssize_t rec_size, cmplx_cmd_t *mess, int64_t &storageToken) {
                    auto file = get_message_data<cmplx_cmd_t>(server.buffer, rec_size);
                    if (
                        file.size() == 0
                         ||
                        file.find('/') != std::string::npos
                         ||
                        !server.fileManager.createFile(file, std::ref(storageToken), mess->param)
                    ) {
                        add_send_decline(server, mess, file);
                        return std::make_pair(false, file);
                    }
                    return std::make_pair(true, file);
            }

        public:
            static size_t mess_into_buffer(char *buffer, const char *mess, const std::string &data, uint64_t cmd_seq) {
                simpl_cmd_t m;
                prepare_simpl_mess(mess, cmd_seq, &m);
                std::memcpy(buffer, (void*)&m, sizeof(simpl_cmd_t));
                data.copy(buffer + sizeof(simpl_cmd_t), data.size());
                return sizeof(simpl_cmd_t) + data.size();
            }

            static size_t mess_into_buffer(char *buffer, const char *mess, const std::string &data, uint64_t cmd_seq, uint64_t param) {
                cmplx_cmd_t m;
                prepare_cmplx_mess(mess, cmd_seq,param, &m);
                std::memcpy(buffer, (void*)&m, sizeof(cmplx_cmd_t));
                std::memcpy(buffer + sizeof(cmplx_cmd_t), data.data(), data.size());
                return sizeof(cmplx_cmd_t) + data.size();
            }

           static void hello_response(ServerNode &server, const ssize_t rec_size) {
                (void)rec_size;
                cmplx_cmd_t *mess = (cmplx_cmd_t*) server.buffer;
                mess_ntoh(mess);
                uint64_t storage = server.fileManager.getStorage();
                mess_into_buffer(server.buffer, "GOOD_DAY", PARAMS::MCAST_ADRR, mess->cmd_seq, storage);
                server.sendUDPPacket(sizeof(cmplx_cmd_t)+PARAMS::MCAST_ADRR.size());
            }

            static void delete_response(ServerNode &server, const ssize_t rec_size) {
                (void)rec_size;
                char  *f_name =  server.buffer + sizeof(simpl_cmd_t);
                std::string file(f_name, rec_size -sizeof(simpl_cmd_t));
                server.fileManager.deleteFile(file);
            }

           static void list_response(ServerNode &server, const ssize_t rec_size) {
                simpl_cmd_t *mess = (simpl_cmd_t*) server.buffer;
                mess_ntoh(mess);
                auto data = get_message_data<simpl_cmd_t>(server.buffer,rec_size);
                auto fileNames = server.fileManager.getFileNames(data);
                send_file_list(fileNames, server,mess);
            }

            static void get_response(ServerNode &server, const ssize_t rec_size) {
                auto fDesc = openFile(server, server.buffer + sizeof(simpl_cmd_t), rec_size- sizeof(simpl_cmd_t));
                if (fDesc.first == -1) //nie udalo sie otworzyc pliku
                    return;
                /** stworz gniazdo tcp, @tcpInfo - para deskryptor, port **/
                auto tcpInfo = prepareTCPConnection();

                if (tcpInfo.first >= 0) {
                    /** odpal watek odpowiedzialny za osbluge placzenia **/
                    threadMaster.launch_tcp_worker(tcpInfo.first, fDesc.first, server.fileManager);
                    get_send_response(server, fDesc.second, tcpInfo.second );
                }
            }

            static void add_response(ServerNode &server, const ssize_t rec_size) {
                cmplx_cmd_t *mess = (cmplx_cmd_t*) server.buffer;
                mess_ntoh(mess);
                int64_t storageToken;
                auto fileOk = addFileIsOk(server, rec_size, mess, storageToken);
                if (!fileOk.first)
                    return;
                /** pusty plik zostal utworzony, pamiec zarezerwowana **/
                auto tcpInfo = prepareTCPConnection();
                if(tcpInfo.first >= 0) {
                    threadMaster.launch_tcp_worker(tcpInfo.first, fileOk.second, mess->param, storageToken, server.fileManager);
                    mess_into_buffer(server.buffer, "CAN_ADD",std::string(""), mess->cmd_seq, tcpInfo.second);
                    server.sendUDPPacket(sizeof(cmplx_cmd_t));
                }
            }

            static void error_response(ServerNode &server, const ssize_t rec_size) {
                (void)rec_size;
                (void)server;
                error::error(error::compose_error_mess(Server::src_addr));
            }
    };



    namespace tcpConnection {

        void send_file(int client_sock_d, FileManager &fileManager, int fd) {
            char buffer[LOCAL_BUFFER_SIZE];
            int64_t read;
            do {
                if (signal.load()) //zalapany SIGINT
                    return;
                read = fileManager.read(buffer, fd, LOCAL_BUFFER_SIZE);
                int64_t sent = write(client_sock_d,buffer,read);
                if (sent != read) {
                    error::error("Error while sending file through tcp socket");
                    break;
                }
            } while(read != 0);
        }

        void receive_file(int eventFds, const int client_sock_d,const int fd, const uint64_t size_, FileManager &fileManager, int64_t token) {
            char buffer[LOCAL_BUFFER_SIZE];
            int64_t read_;
            uint64_t read_overall = 0;
            bool write_ok;
            do {
                if (!waitOnSocket(client_sock_d, 10, eventFds))
                    return;
                read_ = read(client_sock_d, buffer, LOCAL_BUFFER_SIZE);
                if (read_ < 0) {
                    error::error("Error while receiving file from the client");
                    break;
                }
                if (signal.load()) //zlapany SIGINT
                    return;
                write_ok = fileManager.writeToFile(fd,buffer,(uint64_t)read_, token);
                read_overall += read_;
            } while(read_ != 0 && write_ok && read_overall < size_);
        }

        int waitForClientConnection(int sock, int eventFds) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);

            if (!waitOnSocket(sock, PARAMS::TIME_OUT, eventFds)) {
                error::error("Socket timeout  while trying to send file");
                return -1;
            }
            int client_sock_d = accept(sock, (struct sockaddr *) &client_addr, &client_addr_len);

            if (client_sock_d < 0 ) {
                error::error("Error in accept");
                close(sock);
                return -1;
            }

            return client_sock_d;
        }

        void handle_get_request(int eventFds, int sock, int fd, FileManager &fileManager) {
            int client_sock_d = waitForClientConnection(sock, eventFds);

            if (client_sock_d < 0 ) {
                fileManager.closeFile(fd);
                return;
            }

            send_file(client_sock_d, fileManager, fd);

            /** clean- up **/
            shutdown(client_sock_d, SHUT_WR);
            close(client_sock_d);
            close(sock);
            fileManager.closeFile(fd);
        }

        void handle_add_request(int eventFds, int sock, const std::string &file, uint64_t size_, int64_t token, FileManager &fileManager) {
            int client_sock_d = waitForClientConnection(sock, eventFds);
            if (client_sock_d < 0)
                return;

            int fd = fileManager.openFile(file);
            if (fd < 0) {
                error::error("Cant open the file");
                close(client_sock_d);
                return;
            }
            receive_file(eventFds, client_sock_d, fd, size_, fileManager, token);
             /** clean- up **/
            shutdown(client_sock_d, SHUT_WR);
            close(client_sock_d);
            close(sock);
            fileManager.closeFile(fd);
            fileManager.releaseSpace(token);
        }
    }

    typedef void (*UdpAction) (ServerNode&,ssize_t);

    std::map<udpRequests, UdpAction> codeToAction = {
                            {udpRequests::Hello,responseHandler::hello_response},
                            {udpRequests::List, responseHandler::list_response},
                            {udpRequests::Del,responseHandler::delete_response},
                            {udpRequests::Get,responseHandler::get_response},
                            {udpRequests::Add,responseHandler::add_response},
                            {udpRequests::Error, responseHandler::error_response}
                            };


   enum udpRequests ServerNode::deduceMessType(const ssize_t rec_size) {
            if ( (size_t) rec_size < sizeof(simpl_cmd_t)) {
                error::error_mess  = std::string("Wrong package size");
                return udpRequests::Error;
            }
            const simpl_cmd_t *m = (simpl_cmd_t*) buffer;
            if (m->cmd[CMD_SIZE - 1] != END_OF_STRING) {
                error::error_mess  = std::string("Wrong message");
                return udpRequests::Error;
            }
            std::string mess(m->cmd);
            if (mess == std::string{"HELLO"})
                    return udpRequests::Hello;
            else if (mess == std::string{"LIST"})
                    return udpRequests::List;
            else if (mess == std::string{"GET"})
                    return udpRequests::Get;
            else if (mess == std::string{"DEL"})
                    return udpRequests::Del;
            else if (mess == std::string{"ADD"})
                    return udpRequests::Add;
            else {
                error::error_mess = std::string("Unrecognized request").append(mess);
                return udpRequests::Error;
            }
    }

    void ServerNode::listen_() {
        ssize_t rec_size;
        while(true) {
            memset(buffer,0,PARAMS::MAX_UDP_PACKET_SIZE+ 1);
            addrlen = sizeof(src_addr);
            rec_size = recvfrom(udp_sock,buffer, PARAMS::MAX_UDP_PACKET_SIZE,0, (struct sockaddr *)&src_addr, &addrlen);
            codeToAction[deduceMessType(rec_size)](*this,rec_size);
       }

    }

    void run() {
        ServerNode node;
        node.__init__();
        threadMaster.__init__();
        node.listen_();
    }


    void threadOwner::launch_tcp_worker(int sock, int fDescriptor, FileManager &fileManager) {
            sigset_t block_mask;
            sigemptyset (&block_mask);
            sigaddset(&block_mask, SIGINT);                        /*Blokuję SIGINT - zeby dziecko mialo zablokowany*/
            if (sigprocmask(SIG_BLOCK, &block_mask, 0) == -1)
                error::error("sigprocmask block");
            int event_ = this->event;
            workers.push_back(
                    std::thread([event_, sock, fDescriptor,&fileManager]{
                        tcpConnection::handle_get_request(event_, sock, fDescriptor, fileManager);
                }));
            if (sigprocmask(SIG_UNBLOCK, &block_mask, 0) == -1)
                error::error("sigprocmask unblock");
            }

    void threadOwner::launch_tcp_worker(int sock, std::string file, uint64_t size, int64_t token, FileManager &fileManager) {
        sigset_t block_mask;
        sigemptyset (&block_mask);
        sigaddset(&block_mask, SIGINT);                        /*Blokuję SIGINT - zeby dziecko mialo zablokowany*/
        if (sigprocmask(SIG_BLOCK, &block_mask, 0) == -1)
            error::error("sigprocmask block");
        int event_ = this->event;
        workers.push_back(
            std::thread([event_, sock, file, size, token, &fileManager] {
                tcpConnection::handle_add_request(event_, sock, file, size, token, fileManager);
            }));
        if (sigprocmask(SIG_UNBLOCK, &block_mask, 0) == -1)
            error::error("sigprocmask unblock");
    }

}
