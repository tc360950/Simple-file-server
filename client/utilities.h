#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <netinet/in.h>
#include <vector>
#include <sstream>
#include <fstream>
#include <experimental/filesystem>
#include <arpa/inet.h>
#include <chrono>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "err.h"
#include <sys/select.h>


long time_diff(std::chrono::steady_clock::time_point end, std::chrono::steady_clock::time_point begin) {
    return (long) std::chrono::duration_cast<std::chrono::milliseconds> (end - begin).count();
}

namespace fs = std::experimental::filesystem;

const int tcp_timeout = 10;

namespace print {
        std::mutex semaphore;
        enum class InfoType {DownloadFailed, DownloadOk, FileDoesNotExist, TooBig, Skipping, UploadFailed, UploadOK};
        void error(std::string e) {
             std::lock_guard<std::mutex> lck(semaphore);
             std::cerr << e << std::endl;
        }

        void info(std::string m) {
            std::lock_guard<std::mutex> lck(semaphore);
            std::cout << m << std::endl;
        }
        void standard_info(InfoType t, const std::string &file) {
            if (t == InfoType::FileDoesNotExist) {
                std::string mess;
                mess.append("File ");
                mess.append(file);
                mess.append(" does not exist");
                info(mess);
            } else if (t == InfoType::TooBig) {
                std::string mess;
                mess.append("File ");
                mess.append(file);
                mess.append(" too big");
                info(mess);

            }
        }

        void standard_info(InfoType t, struct sockaddr_in rem, const std::string &details, const std::string &additional) {
            std::string mess;
            switch (t) {
                case InfoType::DownloadFailed: {
                    mess.append("File ");
                    mess.append(additional);
                    mess.append(" downloading failed ");
                    break;
                }
                case InfoType::DownloadOk: {
                    mess.append("File ");
                    mess.append(additional);
                    mess.append(" downloaded ");
                    break;
                }
                case InfoType::Skipping: {
                    mess.append("[PCKG ERROR] Skipping invalid package from ");
                    break;
                }
                case InfoType::UploadFailed: {
                    mess.append("File ");
                    mess.append(additional);
                    mess.append(" uploading failed ");
                    break;
                }
                case InfoType::UploadOK: {
                    mess.append("File ");
                    mess.append(additional);
                    mess.append(" uploaded ");
                    break;
                }
                default:
                    break;

            }
            std::string port = std::to_string(ntohs(rem.sin_port));
            std::string ip_{(char*)inet_ntoa((struct in_addr)rem.sin_addr)};
            if (t != InfoType::Skipping) mess.append("(");
            mess.append(ip_);
            mess.append(":");
            mess.append(port);
            if (t != InfoType::Skipping) mess.append(")");
            if (t == InfoType::Skipping) mess.append(".");
            if (details.size() > 0)
                mess.append(" ");
             mess.append(details);
            info(mess);
        }
    }

namespace Files {
    std::mutex semaphore;
    std::string folder;
    std::list<std::string> during_download;

    void create_file(const std::string &name, std::fstream &f) {
        fs::path path_to_f{folder};
        path_to_f.append(name);
        during_download.push_back(name);
        f = std::fstream(path_to_f, std::ios::out | std::ios::binary);
    }

    bool file_exists(const std::string &file) {
        const fs::path path_to_dir{folder};

        for (const auto& entry : fs::directory_iterator(path_to_dir)) {
            const auto filename = entry.path().filename().string();
            if (filename == file)
                return true;
        }
        return false;
    }

    void delete_file(const std::string &file) {
        std::lock_guard<std::mutex> lck(semaphore);
        fs::path path_to_f{folder};
        path_to_f.append(file);
        during_download.remove(file);
        fs::remove(path_to_f);
    }

    void confirm_download(const std::string &file) {
        std::lock_guard<std::mutex> lck(semaphore);
        during_download.remove(file);
    }
}

bool operator==(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
    return  lhs.sin_family == rhs.sin_family
             &&
            lhs.sin_port == rhs.sin_port
             &&
            lhs.sin_addr.s_addr == rhs.sin_addr.s_addr;
}

bool operator!=(const struct sockaddr_in &lhs, const struct sockaddr_in &rhs) {
    return  !(lhs == rhs);
}

std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream{s};
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

int prepare_conn_tcp(struct sockaddr_in remote_addr, int timeout) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    timeval tval;

	     int flags = fcntl(sock, F_GETFL, 0);

       fcntl(sock, F_SETFL, flags | O_NONBLOCK);
          int conn;
          if ( (conn = connect (sock, (struct sockaddr *) &remote_addr, sizeof(struct sockaddr_in))) < 0)
                   if (errno != EINPROGRESS)
                             return(-1);
          if (conn == 0) {
              fcntl(sock, F_SETFL, flags);
              return sock;
            }
          fd_set rset, wset;
          FD_ZERO(&rset);
          FD_SET(sock, &rset);

          wset = rset;
          tval.tv_sec = timeout;
          tval.tv_usec = 0;
          if ( (conn = select(sock+1, &rset, &wset, NULL, &tval)) == 0) {
                   close(sock);
                   errno = ETIMEDOUT;
                   return(-1);
                 }
    fcntl(sock, F_SETFL, flags);
    return sock;
}


bool parse_mcast_address(const std::string &s) {
    struct in_addr d;
    if (inet_pton(AF_INET, s.data(),&d) <= 0) {
        return false;
    }
    return true;
}

bool compareServerPair(std::pair<struct sockaddr_in, uint64_t> lhs, std::pair<struct sockaddr_in, uint64_t> rhs) {
        return lhs.second > rhs.second;
}

bool get_mcast(struct sockaddr_in *remote, int server_udp_port, const std::string &mcast_addr) {
      struct addrinfo addr_hints;
      struct addrinfo *addr_result;
      memset(&addr_hints, 0, sizeof(struct addrinfo));
      addr_hints.ai_family = AF_INET;
      addr_hints.ai_socktype = SOCK_STREAM;
      addr_hints.ai_protocol = IPPROTO_TCP;
      std::ostringstream s;
        s << server_udp_port;
      int err = getaddrinfo(mcast_addr.data(), s.str().data(), &addr_hints, &addr_result);
        if(err != 0 || addr_result->ai_addr == NULL) {
            print::error("Error get addr info");
            return false;
        }
       *remote = *((struct sockaddr_in *)addr_result->ai_addr);
       return true;
}
#endif //UTLITIES_H
