#ifndef MPI_MASTER_H
#define MPI_MASTER_H

#include <mpi.h>

void perr(const char *where, int rc);
int getenv_int(const char *k, int defv);
void bcast_more_and_port(MPI_Comm C, int more, const char *port);
void master_noTLS(const char *port, const int target);
void master_TLS(const char *port, const int target);
void master_auth(const char *port, const int target);

#endif // MPI_MASTER_H