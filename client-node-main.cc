#include <iostream>
#include <cstdint>
#include "client/client-node.h"
#include <boost/program_options.hpp>
#include <string>

using namespace boost::program_options;
namespace {
    int timeout;
    int port;
    std::string mcast;
    std::string folder;
}

std::string get_substr(const std::string &line) {
    if (line.find(' ') != std::string::npos){
            auto s = line.substr(line.find(' ')+1, line.length());
            return s;
    } else {
        return line;
    }
}

bool only_spaces(const std::string &f) {
    return f.find_first_not_of(' ') == std::string::npos;
}

void command_line_interface() {
    ClientNodeInterface::__init__(timeout, folder, mcast, port);
    for(std::string line; std::getline(std::cin,line);) {
        if (line == std::string("exit")) {
            ClientNodeInterface::exit();
            return;
        } else if (line == std::string("discover")) {
            ClientNodeInterface::discover();
        } else if (line.rfind("search",0) == 0) {
            if (line == std::string("search")) {
                ClientNodeInterface::get_list(std::string{""});
            } else if (line.find(' ') != std::string::npos){
                auto s = line.substr(line.find(' ')+1, line.length());
                if (s == line)
                    ClientNodeInterface::get_list(std::string{""});
                else {
                    ClientNodeInterface::get_list(s);
                }
            }

        } else if (line.rfind("fetch", 0) == 0 && line != std::string("fetch")) {
            auto s = get_substr(line);
            if (s!= line && !only_spaces(s))
                ClientNodeInterface::get_file(s);
        } else if (line.rfind("remove", 0) == 0 && line != std::string("remove")) {
            auto s = get_substr(line);
            if (s!= line && !only_spaces(s))
                ClientNodeInterface::delete_(s);
        } else if (line.rfind("upload", 0) == 0 && line != std::string("upload")) {
            auto s = get_substr(line);
            if (s!= line && !only_spaces(s))
                ClientNodeInterface::add_file(s);
        } else {
            //invalid command
        }

    }

}




bool parse_options(int argc, const char*argv[]) {
  try
 {
   options_description desc{"Options"};
   desc.add_options()
     (",g", value<std::string>()->required(), "Group address IPv4")
     (",p", value<int>() -> required(), "Port number for UDP")
     (",o",value<std::string>()->required(), "Path to storage catalog")
     (",t", value<int>()->default_value(5), "Timeout");

   variables_map vm;
   store(parse_command_line(argc, argv, desc), vm);
   notify(vm);
     timeout = vm["-t"].as<int>();
     if (timeout > 300 || timeout < 0 ) {
       throw error("Timeout value exceeds maximal value (300)!\n");
     }
   port = vm["-p"].as<int>();
   mcast = vm["-g"].as<std::string>();
   folder = vm["-o"].as<std::string>();
 }
 catch (const error &ex)
 {
   std::cerr << ex.what() << '\n';
   return false;
 }
 return true;
}

int main (int argc, const char *argv[]) {

  if (!parse_options(argc, argv))
    return 1;

  command_line_interface();

  return 0;
}
