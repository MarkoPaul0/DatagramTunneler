CPPFLAGS=-std=c++11 -Wall -Wconversion -Wsign-compare -Wreorder -Wold-style-cast -Werror #-Wpedantic #TODO: make compiler as strict as possible
RM=rm -f
SRC_DIR=src/
BUILD_DIR=build/
BIN_DIR=bin/

$(BIN_DIR)datagramtunneler: $(BUILD_DIR)DatagramTunneler.o $(BUILD_DIR)main.o
	g++ $(BUILD_DIR)DatagramTunneler.o $(BUILD_DIR)main.o -o $(BIN_DIR)datagramtunneler

$(BUILD_DIR)main.o: $(SRC_DIR)main.cpp $(BUILD_DIR)DatagramTunneler.o directories
	g++ $(CPPFLAGS) -o $(BUILD_DIR)main.o -c $(SRC_DIR)main.cpp

$(BUILD_DIR)DatagramTunneler.o: $(SRC_DIR)DatagramTunneler.h $(SRC_DIR)DatagramTunneler.cpp directories #$(SRC_DIR)main.cpp
	g++ $(CPPFLAGS) -o $(BUILD_DIR)/DatagramTunneler.o -c $(SRC_DIR)DatagramTunneler.cpp #$(SRC_DIR)main.cpp

directories:
	mkdir -p $(BIN_DIR) $(BUILD_DIR)

clean:
	$(RM) ./bin/* ./build/*
