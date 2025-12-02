// worker.c — inicijalni HELLO/MERGE sa masterom, zatim KOLEKTIVNI accept dok master ne kaže "more=0"
// Build: mpicc -O2 -std=gnu11 -o worker worker.c
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "worker.h"

int main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);
  int wr;
  MPI_Comm_rank(MPI_COMM_WORLD, &wr);
  if (argc < 2)
  {
    if (wr == 0)
    {
      fprintf(stderr, "usage: %s <PORT_STRING>\n", argv[0]);
    }

    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  char *port = argv[1];
  mode_comm mode = parse_mode(argc >=3 ? argv[2] : "noTLS");

  switch (mode)
  {
  case MODE_AUTH:
    worker_auth(port);
    break;
  case MODE_NOTLS:
    worker_noTLS(port);
    break;
  case MODE_TLS:
    worker_TLS(port, wr);
    break;
  default:
    printf("[WORKER] Invalid mode specified.\n");
  }
  
  MPI_Finalize();
  return 0;
}

mode_comm parse_mode(const char *arg) {
    if (strcmp(arg, AUTH) == 0)     return MODE_AUTH;
    if (strcmp(arg, NO_TLS) == 0)   return MODE_NOTLS;
    if (strcmp(arg, TLS) == 0)      return MODE_TLS;
    return MODE_INVALID;
}
