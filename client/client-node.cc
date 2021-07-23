#include "client-node.h"
#include <list>
#include <thread>
#include <exception>
#include <mutex>
#include <sys/time.h>
#include <poll.h>
#include <functional>
#include <map>
#include <utility>
#include <algorithm>
#include "UDP.h"
#include "remotes_data.h"
#include <sys/eventfd.h>

#define END_OF_STRING '\0'
#define FILE_LIST_SPLIT '\n'
#define NO_WAY -2



using namespace std::placeholders;

enum class HandlerType {Discover, GetList, GetFile };

class tcpUploader;


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
        }
        void handleExit() {
            uint64_t r = 1;
            ssize_t write_ok = write(event, &r, sizeof(uint64_t));
            if (write_ok <= 0 ) return;
            signal.store(true);
            for (auto &t: workers) {
                if (t.joinable()) {
                    t.join();
                }
            }
            close(event);
			close(UDPMessages::Timer);
		}

        void launch_daemon_reader() {
            workers.push_back (
                std::thread([] {
                    daemonReader::daemon();
                })
            );
        }

        void launch_tcp_downloader(const std::string file, cmplx_cmd_t *mess, struct sockaddr_in remote_address);

        void launch_tcp_uploader(const std::string file, const std::string path, remoteFilesList rem);


    };
    threadOwner threadMaster;

/** obsluga polecenia sciagniecia pliku **/
namespace TCPDownload {
    const int tcp_timeout = 5;

    bool wait_on_descriptor(int desc, int event) {
        int timer = UDPMessages::arm_timer(tcp_timeout);
        struct pollfd fds[3];
        for (int i = 0; i < 3; i++) {
            fds[i].revents = 0;
            fds[i].events = POLLIN;
        }
        fds[0].fd = desc;
        fds[1].fd = timer;
        fds[2].fd = event;

        int occurence = poll (fds, 3, -1);
        return (occurence > 0 && fds[1].revents == 0) && fds[2].revents == 0;
    }


    bool write_to_file(int sock, std::fstream &f, int event) {
        ssize_t read_;
        char buff[MAX_UDP_PACKET_SIZE+1];
        do {
            memset(buff,0,MAX_UDP_PACKET_SIZE +1);
            if (!wait_on_descriptor(sock, event)) {
                return false;
            }
            read_ = read(sock,buff,MAX_UDP_PACKET_SIZE);
            if (read_ < 0) {
                print::error("Error while reading from socket");
                return false;
            }
            else
                f.write(buff, read_);
        } while (read_ != 0);
        return true;
    }

    bool create_file(std::fstream &f, const std::string &file) {
        std::lock_guard<std::mutex> lck(Files::semaphore);
        if (Files::file_exists(file)) {
            print::error("File already exists");
            return false;
        }
        Files::create_file(file, f);
        return true;
    }

    void download_file(const std::string file, struct sockaddr_in remote_address, int event) {
        int sock = prepare_conn_tcp(remote_address,1);
        if (sock < 0) {
            print::standard_info(print::InfoType::DownloadFailed, remote_address,std::string("Error creating socket"), file);
            return;
        }

        std::fstream f;
        if (signal.load() || !create_file(f, file)) {
            return;
        }

        if (!write_to_file(sock, f, event)) {
            f.close();
            Files::delete_file(file);
            print::standard_info(print::InfoType::DownloadFailed, remote_address,std::string("Error reading from socket"), file);
        } else {
            f.close();
            Files::confirm_download(file);
            print::standard_info(print::InfoType::DownloadOk, remote_address,"", file);
        }
        close(sock);
    }
}

void threadOwner::launch_tcp_downloader(const std::string file, cmplx_cmd_t *mess, struct sockaddr_in remote_address) {
    uint64_t param = mess->param;
    remote_address.sin_port = htons((unsigned short)param);
    int event_ = this->event;
    workers.push_back (
        std::thread([file, remote_address, event_] {
            TCPDownload::download_file(file, remote_address, event_);
        })
    );
}

class Handlers {

    bool check_discover_mess(cmplx_cmd_t* &mess, std::string &mcast, ssize_t size, struct sockaddr_in remote_addr, uint64_t cmd) {
        if (!UDPMessages::check_cmd_correctness<cmplx_cmd_t>(remote_addr, false, "GOOD_DAY", size , cmd, mess)) {
            print::error("Wrong message");
            return false;
        }

        mcast = std::string{UDPMessages::buffer + sizeof(cmplx_cmd_t), size - sizeof(cmplx_cmd_t)};

        if (!parse_mcast_address(mcast)) {
            print::error("Can't parse mcast address");
            return false;
        }
        return true;
    }

    /* Zawsze zwraca @false, dajac znac modulowi UDP, by dalej odbieral wiadomosci */
    bool handleDiscover(ssize_t size, struct sockaddr_in remote_addr, socklen_t s, uint64_t cmd) {
        (void)s;
        cmplx_cmd_t *mess = nullptr;
        std::string mcast;

        if (!check_discover_mess(mess, mcast, size, remote_addr, cmd)) {
            return false;
        }
        uint64_t storage = mess->param;
        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        for (auto &v: remotesData::remotes) {
            if (std::get<0>(v) == remote_addr) {
                //update storage info
                std::get<1>(v) = storage;
                std::get<3>(v) = mcast;
                return false;
            }
        }
        std::vector<std::string> emptyVec;
        remotesData::remotes.push_back({remote_addr, storage, emptyVec, mcast});
        return false;
    }

    void update_file_list(struct sockaddr_in remote_addr, const std::string &data) {
        auto file_list = split(data, FILE_LIST_SPLIT);

        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        for (auto &n: remotesData::remotes) {
            if (std::get<0>(n) == remote_addr) {
                std::get<2>(n) = file_list;
                return;
            }
        }
        remotesData::remotes.push_back({remote_addr, (uint64_t)0, file_list, std::string{""}});
    }

    /* Zawsze zwraca @false, dajac znac modulowi UDP, by dalej odbieral wiadomosci */
    bool handleGetList(ssize_t size, struct sockaddr_in remote_addr, socklen_t s, uint64_t cmd) {
        (void)s;
        simpl_cmd_t *mess = nullptr;
        if (!UDPMessages::check_cmd_correctness<simpl_cmd_t>(remote_addr, false,"MY_LIST", size , cmd, mess)) {
            return false;
        }

        std::string data{UDPMessages::buffer + sizeof(simpl_cmd_t), size - sizeof(simpl_cmd_t)};
        update_file_list(remote_addr, data);
        return false;
    }

    /* Jesli znalezlismy chetny serwer, to zwracamy @true, by przestac oczekiwac na wiadomosc "CONNECT_ME" */
    bool handleGetFile(const std::string &file, ssize_t size, struct sockaddr_in remote_addr, socklen_t s, uint64_t cmd, struct sockaddr_in expected_remote) {
        (void)s;
        cmplx_cmd_t *mess = nullptr;
        if (!UDPMessages::check_cmd_correctness<cmplx_cmd_t>(remote_addr, false,"CONNECT_ME", size , cmd, mess) || expected_remote != remote_addr) {
            return false;
        }
        std::string f{UDPMessages::buffer + sizeof(cmplx_cmd_t), size - sizeof(cmplx_cmd_t)};

        if (f != file) {
            print::standard_info(print::InfoType::Skipping, remote_addr, "Wrong file name","");
            return false;
        }

        threadMaster.launch_tcp_downloader(file, mess, remote_addr);
        return true;
    }

    public:
        handler_t getHandler( enum HandlerType type, uint64_t cmd) {
            if (type == HandlerType::Discover) {
                auto f =  std::bind(&Handlers::handleDiscover, this, _1, _2, _3, cmd);
                return f;
            } else {
                auto f =  std::bind(&Handlers::handleGetList, this, _1, _2, _3, cmd);
                return f;
            }
        }

        handler_t getHandler(const std::string &f_, uint64_t cmd, struct sockaddr_in remote) {
            auto f =  std::bind(&Handlers::handleGetFile, this,f_, _1, _2, _3, cmd, remote);
            return f;
        }
};


class ClientNode {

    int server_udp_port;

    int timeout;

    std::string outfldr;

    std::string mcast_addr;

    Handlers handlers;

    public:

    void discover(struct sockaddr_in remote_address);

    void delete_(struct sockaddr_in remote_address, const std::string &file);

    void get_list(struct sockaddr_in remote_addres, const std::string &pattern);

    void get_file(struct sockaddr_in remote_addres, const std::string &file);

    void add_file(const std::string &file, const std::string &path);

    bool get_mcast(struct sockaddr_in *remote);

    void print_discover();

    void print_get_list();

    bool choose_server_for_download(const std::string &file, struct sockaddr_in *rem);

        ClientNode(int to, std::string fldr, std::string mcast_addr, int prt): server_udp_port{prt}, timeout{to}, outfldr{fldr}, mcast_addr{mcast_addr} {}

        ClientNode() {}
};


    void ClientNode::print_discover() {
        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        for (auto &v: remotesData::remotes) {
            std::string ip_{(char*)inet_ntoa((struct in_addr)std::get<0>(v).sin_addr)};
            std::string info;
            info.append("Found ").append(ip_).append(" (").append(std::get<3>(v));
            info.append(") with free space ");
            std::ostringstream s;
            s << std::get<1>(v);
            info.append(s.str());
            print::info(info);
        }
    }

    void ClientNode::print_get_list() {
        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        for (auto &v: remotesData::remotes) {
            std::string ip_{(char*)inet_ntoa((struct in_addr)std::get<0>(v).sin_addr)};
            for (auto &fl: std::get<2>(v)) {
                std::string info;
                info.append(fl);
                info.append(" (");
                info.append(ip_);
                info.append(")");
                print::info(info);
            }
        }
    }

    bool ClientNode::choose_server_for_download(const std::string &file, struct sockaddr_in *rem) {
        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        for (auto &v: remotesData::remotes) {
            if (find(std::get<2>(v).begin(), std::get<2>(v).end(), file) != std::get<2>(v).end()) {
                *rem = std::get<0>(v);
                return true;
            }
        }
        return false;
    }


    void ClientNode::discover(struct sockaddr_in remote_address) {
        std::lock_guard<std::mutex> lck(daemonReader::daemon_barrier);
        auto res = UDPMessages::send_hello(remote_address);
        if (!res.first) {
            print::error("Error sending UDP packet");
        } else {
            remotesData::clear_remotes();
            UDPMessages::wait_for_packet(handlers.getHandler(HandlerType::Discover, res.second));
            print_discover();
        }
    }

    void ClientNode::delete_(struct sockaddr_in remote_address, const std::string &file) {
        std::lock_guard<std::mutex> lck(daemonReader::daemon_barrier);
        if (!UDPMessages::send_delete(file, remote_address).first){
            print::error("Error sending UDP packet");
        }
    }

    void ClientNode::get_list(struct sockaddr_in remote_address, const std::string &pattern) {
        std::lock_guard<std::mutex> lck(daemonReader::daemon_barrier);
        auto res = UDPMessages::send_list(remote_address, pattern);
        if (!res.first) {
            print::error("Error sending UDP packet");
        } else {
            remotesData::clean_file_lists();
            UDPMessages::wait_for_packet(handlers.getHandler(HandlerType::GetList, res.second));
            print_get_list();
        }
    }


    void ClientNode::get_file(struct sockaddr_in remote_address, const std::string &file) {
        std::lock_guard<std::mutex> lck(daemonReader::daemon_barrier);
        auto res = UDPMessages::send_get(file, remote_address);
        if (!res.first) {
            print::error("Error sending UDP packet");
        } else {
            UDPMessages::wait_for_packet(handlers.getHandler(file, res.second, remote_address));
        }
    }


/**
 * Klasa odpowiedzialna za wysylanie plikow do serwerow
 * Kazda instancja odpowiada jednemu plikowi, ktory ma zostac zaladowany.
 */
class tcpUploader {
    char buffer[MAX_UDP_PACKET_SIZE +1];

    uint64_t my_cmd;

    std::string file;

    std::string path;

    uint64_t file_size;

    remoteFilesList remotes;

    const int timeout = 100; //milisekundy

    static const long pckt_shelf_life = 5000; // milisekundy

    std::pair<bool, uint64_t> send_udp(struct sockaddr_in remote_address) {
        return UDPMessages::send_add(buffer, file, file_size, remote_address);
    }

    bool send_file(struct sockaddr_in remote, int tcp_sock) {
        uint64_t read;
        fs::path path_to_f{path};
        std::fstream f(path_to_f, std::ios::in | std::ios::binary);
        do {
            if (signal.load()) return false;
            f.read(buffer, MAX_UDP_PACKET_SIZE);
            read = f ? MAX_UDP_PACKET_SIZE : f.gcount();
            if (read > 0)  {
                uint64_t sent = write(tcp_sock, buffer, read);
                if (sent != read) {
                    print::standard_info(print::InfoType::UploadFailed, remote, "Couldn't send through tcp socket",file);
                    return false;
                }
            }

        } while(read != 0);
        return true;
    }

    bool send(struct sockaddr_in remote) {
        if (signal.load())
            return false;
        int tcp_sock = prepare_conn_tcp(remote, 1);
        if (tcp_sock < 0 ) {
            print::error("Error while creating tcp socket");
            return false;
        }

        if (!send_file(remote, tcp_sock)) {
            close(tcp_sock);
            return false;
        }

        print::standard_info(print::InfoType::UploadOK, remote, "",file);
        close(tcp_sock);
        return true;
    }
    /* @mess - wiadomos wyjeta z kolejki
     * Zwraca numer port, na ktorym oczekuje nadawce wiadomosci lub - 1
     */
    int queue_mess_handle(std::tuple<cmplx_cmd_t,time_pt, struct sockaddr_in> mess, struct sockaddr_in remote_address) {
        cmplx_cmd_t m = std::get<0>(mess);
        mess_ntoh(&m);
        if (m.cmd_seq != my_cmd) {
            // nie jestem odbiorca wiadomosci - wkladam ja z powrotem o ile nie jest przeterminowana
            time_pt t = std::chrono::steady_clock::now();
            if (time_diff(t,std::get<1>(mess))< pckt_shelf_life) {
                tcpUploaders::mess_buffer.enqueue(mess);
            } else {
                print::standard_info(print::InfoType::Skipping, std::get<2>(mess), " Expired packet","");
            }
        } else if ( std::string(m.cmd) == std::string("NO_WAY") && remote_address == std::get<2>(mess)) {
            //serwer odpowiedzial "NO_WAY" mozemy probowac z nastepnym
            return NO_WAY;
        }  else {
            return m.param;
        }
        return -1;
    }

    int wait_on_queue(struct sockaddr_in remote_address) {
        std::tuple<cmplx_cmd_t,time_pt, struct sockaddr_in> mess;
        time_pt begin = std::chrono::steady_clock::now();
        while (true) {
            if (signal.load()) //exit;
                return -1;
            if (tcpUploaders::mess_buffer.wait_dequeue_for(mess, std::chrono::milliseconds(this->timeout))) {
                int port = queue_mess_handle(mess, remote_address);
                if (port >= 0)
                    return port;
                else if (port == NO_WAY){
					return -1;
				}
            }
            time_pt tp = std::chrono::steady_clock::now();
            if (time_diff(tp, begin) > this->pckt_shelf_life) {
                return -1;
            }
        }
        return -1;
    }

    bool try_send(struct sockaddr_in remote_address) {
        if (signal.load()) return false;
        auto res = send_udp(remote_address);
        if (!res.first) {
            print::error("Add failed");
            return false;
        }
        my_cmd = res.second;
        int port = wait_on_queue(remote_address);
        if (port < 0 )
            return false;
        remote_address.sin_port = htons(port);
        return send(remote_address);
    }

    bool file_exists() {
        fs::path path_to_f{path};
        if( !fs::exists(path_to_f)) {
            return false;
        } else {
            file_size = fs::file_size(path_to_f);
            return true;
        }
    }

    std::vector<std::pair<struct sockaddr_in, uint64_t>> get_matching_servers() {
        std::vector<std::pair<struct sockaddr_in, uint64_t>> result;
        for (auto &v: remotes) {
            if (std::get<1>(v) >= file_size) {
                result.push_back(std::make_pair(std::get<0>(v), std::get<1>(v)));
            }
        }
        std::sort(result.begin(), result.end(), compareServerPair);
        return result;
    }

    public:

    tcpUploader(std::string f, std::string p, remoteFilesList rem): file{f}, path{p}, remotes{rem} {
    }

    void run(){
        if (signal.load()) return;
        if (!file_exists()) {
            print::standard_info(print::InfoType::FileDoesNotExist, this->file);
            return;
        }
        if (signal.load()) return;
        auto matching = get_matching_servers();
        if (matching.size() == 0) {
            print::standard_info(print::InfoType::TooBig, file);
            return;
        }
        if (signal.load()) return;
        for (auto &x: matching) {
            if(try_send(x.first)) {
                return;
            }
            if (signal.load()) return;
        }
        print::error("Could not upload the file");
    }

};

void ClientNode::add_file(const std::string &file, const std::string &path) {
    std::lock_guard<std::mutex> lck(daemonReader::daemon_barrier);
    auto rem = remotesData::get_remotes_copy();
    threadMaster.launch_tcp_uploader(file, path, rem);
}

void threadOwner::launch_tcp_uploader(const std::string file, const std::string path, remoteFilesList rem) {
        workers.push_back(
            std::thread( [file, path, rem] {
                tcpUploader upldr(file, path, rem);
                upldr.run();
            })
        );
}

namespace ClientNodeInterface {

    ClientNode node;

    struct sockaddr_in remote;

    void __init__(int to, std::string fldr, std::string mcast_addr, int prt) {
            node = ClientNode{to, fldr, mcast_addr, prt};
            if (!get_mcast(&remote, prt, mcast_addr)) {
                print::error("Can't recognize mcast address!");
            }
            Files::folder = fldr;
            UDPMessages::time_out = to;
            UDPMessages::__init__();
            threadMaster.__init__();
            threadMaster.launch_daemon_reader();
    }

    void discover() {
            node.discover(remote);
    }

    void delete_(const std::string &file) {
        node.delete_(remote, file);
    }

    void get_list(const std::string &pattern) {
        node.get_list(remote, pattern);
    }

    void get_file(const std::string &file) {
        struct sockaddr_in rem;
        if (!node.choose_server_for_download(file, &rem)) {
            print::info("Error - File has not been found amongst server nodes");
        } else {
            node.get_file(rem, file);
        }
    }

    void add_file(const std::string &path_to_f) {
        std::string file = fs::path(path_to_f).filename();
        node.add_file(file, path_to_f);
    }

    void exit() {
        std::lock_guard<std::mutex> lck(UDPMessages::semaphore);
        threadMaster.handleExit();
        close(UDPMessages::udp_sock);
    }
}
