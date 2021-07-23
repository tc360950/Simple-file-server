#include <iostream>
#include <cstdint>
#include "server/server-node.h"
#include "server/server-consts.h"
#include <boost/program_options.hpp>
#include <string>

using namespace boost::program_options;


bool parse_options(int argc, const char*argv[]) {
  try
 {
   options_description desc{"Options"};
   desc.add_options()
     (",g", value<std::string>()->required(), "Group address IPv4")
     (",p", value<int>() -> required(), "Port number for UDP")
     (",b", value<uint64_t>()->default_value(PARAMS::DEFAULT_MAX_SPACE), "Maximal storage space")
     (",f",value<std::string>()->required(), "Path to storage catalog")
     (",t", value<int>()->default_value(PARAMS::DEFAULT_TIMEOUT), "Timeout");

   variables_map vm;
   store(parse_command_line(argc, argv, desc), vm);
   notify(vm);
   if (vm.count("-t")) {
     PARAMS::TIME_OUT = vm["-t"].as<int>();
     if (PARAMS::TIME_OUT > PARAMS::MAX_TIMEOUT) {
       throw error("Timeout value exceeds maximal value (300)!\n");
     }
   }
   if (vm.count("-b")) {
     PARAMS::MAX_SPACE = vm["-b"].as<uint64_t>();
   }
   PARAMS::CMD_PORT = vm["-p"].as<int>();
   PARAMS::MCAST_ADRR = vm["-g"].as<std::string>();
   PARAMS::SHRD_FLDR = vm["-f"].as<std::string>();
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

  Server::run();

  return 0;
}
