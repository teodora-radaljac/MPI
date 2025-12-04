#include "utils.h"
#include "worker.h"
#include <mpi.h>
#include <stdio.h>

void worker_noTLS(const char *port)
{
    // --- Prvo spajanje (samo master je s druge strane) ---
    MPI_Comm inter;
    int rc = MPI_Comm_connect(port, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &inter);
    perr("Comm_connect#1", rc);
    int hello = 1;
    rc = MPI_Send(&hello, 1, MPI_INT, 0, TAG_HELLO, inter);
    perr("Send(HELLO#1)", rc);
    int cmd = 0;
    rc = MPI_Recv(&cmd, 1, MPI_INT, 0, TAG_MERGE_CMD, inter, MPI_STATUS_IGNORE);
    perr("Recv(MERGE_CMD#1)", rc);
    int ready = 1;
    rc = MPI_Send(&ready, 1, MPI_INT, 0, TAG_READY, inter);
    perr("Send(READY#1)", rc);
    rc = MPI_Barrier(inter);
    perr("Barrier(inter#1)", rc);

    MPI_Comm CLUSTER;
    rc = MPI_Intercomm_merge(inter, 1, &CLUSTER);
    perr("Intercomm_merge#1", rc); // high=1 (novi)
    MPI_Comm_disconnect(&inter);

    int rank, size;
    MPI_Comm_rank(CLUSTER, &rank);
    MPI_Comm_size(CLUSTER, &size);
    printf("[WORKER] joined CLUSTER: rank=%d of %d (first)\n", rank, size);
    fflush(stdout);

    // --- KOLEKTIVNI prijemi dok master ne kaže "more=0" ---
    for (;;)
    {
        int more = 0, len = 0;
        char port[MPI_MAX_PORT_NAME];
        MPI_Bcast(&more, 1, MPI_INT, 0, CLUSTER);
        MPI_Bcast(&len, 1, MPI_INT, 0, CLUSTER);
        if (len > 0 && len <= MPI_MAX_PORT_NAME)
            MPI_Bcast(port, len, MPI_CHAR, 0, CLUSTER);

        if (!more)
            break;

        MPI_Comm inter2;
        int rc2 = MPI_Comm_accept(port, MPI_INFO_NULL, 0, CLUSTER, &inter2);
        perr("Comm_accept#next(w)", rc2);
        rc2 = MPI_Barrier(inter2);
        perr("Barrier(inter#next,w)", rc2);

        MPI_Comm CL_NEW;
        rc2 = MPI_Intercomm_merge(inter2, 0, &CL_NEW);
        perr("Intercomm_merge#next,w", rc2); // high=0 (postojeci)
        MPI_Comm_disconnect(&inter2);

        MPI_Comm_free(&CLUSTER);
        CLUSTER = CL_NEW;
        MPI_Comm_rank(CLUSTER, &rank);
        MPI_Comm_size(CLUSTER, &size);
        printf("[WORKER] post-merge: rank=%d of %d\n", rank, size);
        fflush(stdout);
    }

    // --- Task-farm petlja ---
    if (rank != 0)
    {
        for (;;)
        {
            MPI_Status st;
            MPI_Probe(0, MPI_ANY_TAG, CLUSTER, &st);
            if (st.MPI_TAG == TAG_IDLE)
            {
                MPI_Recv(NULL, 0, MPI_INT, 0, TAG_IDLE, CLUSTER, MPI_STATUS_IGNORE);
                continue;
            }
            if (st.MPI_TAG == TAG_TASK)
            {
                int x = 0;
                MPI_Recv(&x, 1, MPI_INT, 0, TAG_TASK, CLUSTER, MPI_STATUS_IGNORE);
                int y = x * x;
                int pair[2] = {x, y};
                MPI_Send(pair, 2, MPI_INT, 0, TAG_RESULT, CLUSTER);
                continue;
            }
            // fallback — progutaj nepoznat tag
            MPI_Recv(NULL, 0, MPI_INT, 0, st.MPI_TAG, CLUSTER, MPI_STATUS_IGNORE);
        }
    }

    MPI_Comm_free(&CLUSTER);
}
