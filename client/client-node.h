#ifndef CLIENT_NODE_H
#define CLIENT_NODE_H
#include "../messages.h"
#include <iostream>



namespace ClientNodeInterface {
    void __init__(int to, std::string fldr, std::string mcast_addr, int prt);
    void discover();

    void delete_(const std::string &file);

    void get_list(const std::string &pattern);

    void get_file(const std::string &file);

    void add_file(const std::string &path_to_f);

    void exit();
};












#endif //CLIENT_NODE_H
