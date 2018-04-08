CPPFLAGS=-Wall

./bin/datagramtunneler: DatagramTunneler.o
	g++ DatagramTunneler.o -o ./bin/datagramtunneler

DatagramTunneler.o: ./src/DatagramTunneler.h ./src/DatagramTunneler.cpp
	g++ $(CPPFLAGS) -c ./src/DatagramTunneler.cpp
