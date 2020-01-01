#pragma once
#include <cstdio>

#define INFO(_format_,...)  do { printf("INFO      "); printf(_format_,##__VA_ARGS__); printf("\n");          } while (0);
#define WARN(_format_,...)  do { printf("WARNING   "); printf(_format_,##__VA_ARGS__); printf("\n");          } while (0);
#define ERROR(_format_,...) do { printf("ERROR     "); printf(_format_,##__VA_ARGS__); printf("\n");          } while (0);
#define DEATH(_format_,...) do { printf("DEATH     "); printf(_format_,##__VA_ARGS__); printf("\n"); exit(1); } while (0);
