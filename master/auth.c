#include "utils.h"
#include "master.h"
#include <stdio.h>

static const int REQUIRED_MAGIC = 5;

void master_auth(const char *port, const int target)
{
    MPI_Comm CLUSTER;
    int rc = MPI_Comm_dup(MPI_COMM_SELF, &CLUSTER);
    perr("Comm_dup(self)", rc);

    int added = 0;
    int accept_it = 1;
    // 3) Admission runde: isti kod za prvi i sve sledeće talase
    while (added < target)
    {
        printf("%d\n", added);
        fflush(stdout);
        // poravnanje runde sa trenutnim CLUSTER-om
        if (accept_it)
        {
            MPI_Barrier(CLUSTER);

            // najavi da primamo još (more=1) i podeli port
        }
        bcast_more_and_port(CLUSTER, /*more=*/1, port);
        // kolektivni accept preko CLUSTER-a (prvi put učestvuje samo master)
        MPI_Comm inter;
        printf("Pre ACCEPT");
        fflush(stdout);
        rc = MPI_Comm_accept(port, MPI_INFO_NULL, 0, CLUSTER, &inter);
        perr("Comm_accept", rc);
        printf("posle ACCEPT");
        fflush(stdout);
        accept_it = 0;
        int magic = -1;
        MPI_Recv(&magic, 1, MPI_INT, 0, TAG_AUTH, inter, MPI_STATUS_IGNORE);
        accept_it = (magic == REQUIRED_MAGIC);

        // 2) pošalji reply novom (AUTH_REPLY)
        MPI_Send(&accept_it, 1, MPI_INT, 0, TAG_AUTH_REPLY, inter);

        // 3) OBAVEZNO: broadcast odluke SVIM članovima CLUSTER-a u OVOJ RUNDI
        MPI_Bcast(&accept_it, 1, MPI_INT, 0, CLUSTER);

        if (!accept_it)
        {
            // 4a) svi lokalni (master + stari workeri) rade DISCONNECT nad istim inter
            MPI_Comm_disconnect(&inter);

            // 5) poravnanje kraja runde
            MPI_Barrier(CLUSTER);
            printf("Prosao bcast barijeru");
            fflush(stdout);
            continue; // bez merge-a u ovoj rundi
        }
        else
        {
            printf("usao u");
            fflush(stdout);
            // NEMA barijere na "inter" — merge je već kolektivan
            MPI_Comm CL_NEW;
            rc = MPI_Intercomm_merge(inter, /*high master*/ 0, &CL_NEW);
            perr("Intercomm_merge", rc);
            MPI_Comm_disconnect(&inter);

            // zameni CLUSTER i javi novu veličinu
            MPI_Comm_free(&CLUSTER);
            CLUSTER = CL_NEW;
            int s;
            MPI_Comm_size(CLUSTER, &s);
            printf("[MASTER] merged wave -> size=%d (workers=%d)\n", s, s - 1);
            fflush(stdout);
            added++;
        }
    }
    printf("USAAOOOOO");
    fflush(stdout);
    // (opciono) zatvori port da kasniji connect ne visi
    MPI_Barrier(CLUSTER);
    bcast_more_and_port(CLUSTER, /*more=*/0, /*port=*/NULL);

    MPI_Close_port(port);

    // 4) Demo task-farm
    int tasks[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
    int NT = (int)(sizeof(tasks) / sizeof(tasks[0]));
    int next = 0;

    int size, rank;
    MPI_Comm_size(CLUSTER, &size);
    MPI_Comm_rank(CLUSTER, &rank);
    if (size > 1)
    {
        for (int w = 1; w < size; ++w)
            if (next < NT)
                MPI_Send(&tasks[next++], 1, MPI_INT, w, TAG_TASK, CLUSTER);
            else
                MPI_Send(NULL, 0, MPI_INT, w, TAG_IDLE, CLUSTER);

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

    MPI_Comm_free(&CLUSTER);
}