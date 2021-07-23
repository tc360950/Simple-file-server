all: netstore-server netstore-client

netstore-server: server-node-main.o server-node.o server-consts.o file_manager.o messages.o
	g++ -o netstore-server server-node-main.o server-node.o server-consts.o file_manager.o messages.o -lboost_program_options -lstdc++fs -pthread
netstore-client: client-node-main.o client-node.o messages.o
	g++ -o netstore-client client-node-main.o client-node.o messages.o -lboost_program_options -lstdc++fs -pthread
server-consts.o: server/server-consts.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c server/server-consts.cc
server-node.o: server/server-node.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c server/server-node.cc
server-node-main.o: server-node-main.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c server-node-main.cc
client-node-main.o: client-node-main.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c client-node-main.cc
file_manager.o: server/file_manager.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c server/file_manager.cc
messages.o: messages.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c messages.cc
client-node.o: client/client-node.cc
	g++ -std=c++17 -lstdc++fs -Wall -Wextra -O2 -c client/client-node.cc
clean:
	rm *.o netstore-server netstore-client
