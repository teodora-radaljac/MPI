#include "utils.h"
#include "master.h"
#include <stdio.h>

void master_noTLS(const char *port, const int target)
{
    MPI_Comm CLUSTER;
    int rc = MPI_Comm_dup(MPI_COMM_SELF, &CLUSTER);

    perr("Comm_dup(self)", rc);

    int added = 0;

    // === PRVI worker: accept na SELF (samo master u lokalnoj grupi) ===
    {
        MPI_Comm inter;
        rc = MPI_Comm_accept(port, MPI_INFO_NULL, 0, MPI_COMM_SELF, &inter);
        perr("Comm_accept#1", rc);

        int hello = 0;
        rc = MPI_Recv(&hello, 1, MPI_INT, 0, TAG_HELLO, inter, MPI_STATUS_IGNORE);
        perr("Recv(HELLO#1)", rc);
        int cmd = 1;
        rc = MPI_Send(&cmd, 1, MPI_INT, 0, TAG_MERGE_CMD, inter);
        perr("Send(MERGE_CMD#1)", rc);
        int ready = 0;
        rc = MPI_Recv(&ready, 1, MPI_INT, 0, TAG_READY, inter, MPI_STATUS_IGNORE);
        perr("Recv(READY#1)", rc);

        rc = MPI_Barrier(inter);
        perr("Barrier(inter#1)", rc);

        MPI_Comm NEWC;
        rc = MPI_Intercomm_merge(inter, 0, &NEWC);
        perr("Intercomm_merge#1", rc);
        MPI_Comm_disconnect(&inter);
        MPI_Comm_free(&CLUSTER);
        CLUSTER = NEWC;

        int s;
        MPI_Comm_size(CLUSTER, &s);
        printf("[MASTER] merged one -> size=%d (workers=%d)\n", s, s - 1);
        fflush(stdout);
        added = 1;

        // novi član mora da dobije PORT za buduće kolektivne prijeme
        bcast_more_and_port(CLUSTER, /*more=*/(added < target ? 1 : 0), port);
    }

    // === Dalji workeri: KOLEKTIVNI accept preko CLUSTER-a ===
    while (added < target)
    {
        // svi u CLUSTER-u (master + već pristigli) znaju da sledi još jedan prijem i PORT
        // (napomena: posle PRVOG merge-a već smo poslali more/port, ali ne škodi idempotentno)
        // bcast_more_and_port(CLUSTER, 1, PORT);  // već odrađeno gore posle 1. merge-a

        MPI_Comm inter2;
        rc = MPI_Comm_accept(port, MPI_INFO_NULL, 0, CLUSTER, &inter2);
        perr("Comm_accept#next", rc);

        // samo master komunicira P2P sa novim (remote rank 0)
        int cmd = 1;
        rc = MPI_Send(&cmd, 1, MPI_INT, 0, TAG_MERGE_CMD, inter2);
        perr("Send(MERGE_CMD#next)", rc);
        int ready = 0;
        rc = MPI_Recv(&ready, 1, MPI_INT, 0, TAG_READY, inter2, MPI_STATUS_IGNORE);
        perr("Recv(READY#next)", rc);

        rc = MPI_Barrier(inter2);
        perr("Barrier(inter#next)", rc);

        MPI_Comm CL_NEW;
        rc = MPI_Intercomm_merge(inter2, 0, &CL_NEW);
        perr("Intercomm_merge#next", rc);
        MPI_Comm_disconnect(&inter2);

        MPI_Comm_free(&CLUSTER);
        CLUSTER = CL_NEW;
        int s;
        MPI_Comm_size(CLUSTER, &s);
        printf("[MASTER] merged one -> size=%d (workers=%d)\n", s, s - 1);
        fflush(stdout);
        added++;

        // NOVO: posle SVAKOG merge-a — svi dobijaju PORT za eventualno sledeći krug
        bcast_more_and_port(CLUSTER, /*more=*/(added < target ? 1 : 0), port);
    }

    // === TASK-FARM DEMO ===
    int tasks[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
    int NT = (int)(sizeof(tasks) / sizeof(tasks[0]));
    int next = 0;

    int size, rank;
    MPI_Comm_size(CLUSTER, &size);
    MPI_Comm_rank(CLUSTER, &rank);
    if (size > 1)
    {
        // inicijalno: svima po 1 posao ili IDLE
        for (int w = 1; w < size; ++w)
        {
            if (next < NT)
                MPI_Send(&tasks[next++], 1, MPI_INT, w, TAG_TASK, CLUSTER);
            else
                MPI_Send(NULL, 0, MPI_INT, w, TAG_IDLE, CLUSTER);
        }
        // glavna petlja raspodele
        for (;;)
        {
            int pair[2];
            MPI_Status st;
            MPI_Recv(pair, 2, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, CLUSTER, &st);
            printf("[MASTER] result: %d -> %d (from %d)\n", pair[0], pair[1], st.MPI_SOURCE);
            fflush(stdout);
            if (next < NT)
                MPI_Send(&tasks[next++], 1, MPI_INT, st.MPI_SOURCE, TAG_TASK, CLUSTER);
            else
                MPI_Send(NULL, 0, MPI_INT, st.MPI_SOURCE, TAG_IDLE, CLUSTER);
        }
    }

    MPI_Close_port(port);
    MPI_Comm_free(&CLUSTER);
}