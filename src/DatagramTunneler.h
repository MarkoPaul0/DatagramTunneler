#pragma once
#include <cstdio>
#define INFO(_format_,...) printf("INFO      "); printf((_format_),##__VA_ARGS__); printf("\n");
#define WARN(_format_,...) printf("WARNING   "); printf((_format_),##__VA_ARGS__); printf("\n");
#define ERROR(_format_,...) printf("ERROR     "); printf((_format_),##__VA_ARGS__); printf("\n");

class DatagramTunneler {
};
