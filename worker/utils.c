#include <mpi.h>
#include <stdio.h>

void perr(const char *where, int rc)
{
    if (rc == MPI_SUCCESS)
    {
        return;
    }

    char es[256];
    int n = 0;
    MPI_Error_string(rc, es, &n);
    fprintf(stderr, "[WORKER] %s rc=%d (%s)\n", where, rc, es);
    fflush(stderr);
}
