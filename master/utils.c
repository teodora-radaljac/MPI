#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void perr(const char *where, int rc)
{
    if (rc == MPI_SUCCESS)
        return;
    char es[256];
    int n = 0;
    MPI_Error_string(rc, es, &n);
    fprintf(stderr, "[MASTER] %s rc=%d (%s)\n", where, rc, es);
    fflush(stderr);
}

int getenv_int(const char *k, int defv)
{
    const char *s = getenv(k);
    if (!s || !*s)
        return defv;
    int v = atoi(s);
    return v > 0 ? v : defv;
}

// Bcast(more,len,port): ako more=0, len=0 i port se ne šalje.
void bcast_more_and_port(MPI_Comm C, int more, const char *port)
{
    int len = more ? ((int)strlen(port) + 1) : 0;
    MPI_Bcast(&more, 1, MPI_INT, 0, C);
    MPI_Bcast(&len,  1, MPI_INT, 0, C);
    if (more && len > 0 && len <= MPI_MAX_PORT_NAME)
        MPI_Bcast((void *)port, len, MPI_CHAR, 0, C);
}
