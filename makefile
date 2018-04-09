CPPFLAGS=-Wall -Werror #-Wpedantic #TODO: make compiler as strict as possible
RM=rm -f
SRC_DIR=src/
BUILD_DIR=build/
BIN_DIR=bin/

$(BIN_DIR)datagramtunneler: $(BUILD_DIR)DatagramTunneler.o $(BUILD_DIR)main.o
	g++ $(BUILD_DIR)DatagramTunneler.o $(BUILD_DIR)main.o -o $(BIN_DIR)datagramtunneler

$(BUILD_DIR)main.o: $(SRC_DIR)main.cpp $(BUILD_DIR)DatagramTunneler.o
	g++ $(CPPFLAGS) -o $(BUILD_DIR)main.o -c $(SRC_DIR)main.cpp

$(BUILD_DIR)DatagramTunneler.o: $(SRC_DIR)DatagramTunneler.h $(SRC_DIR)DatagramTunneler.cpp #$(SRC_DIR)main.cpp
	g++ $(CPPFLAGS) -o $(BUILD_DIR)/DatagramTunneler.o -c $(SRC_DIR)DatagramTunneler.cpp #$(SRC_DIR)main.cpp

clean:
	$(RM) ./bin/* ./build/*
