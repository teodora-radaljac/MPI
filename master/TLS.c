#include "utils.h"
#include "master.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

static int g_session_keys[MAX_PROCS];

void secure_send_task(int x, int dest, MPI_Comm comm)
{
    int key = g_session_keys[dest];       // ključ za TAJ rank
    int enc = x ^ key;
    MPI_Send(&enc, 1, MPI_INT, dest, TAG_TASK, comm);
}

void master_TLS(const char *port, const int target)
{
    // 2) CLUSTER = COMM_SELF (posle svakog merge-a raste)
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
        MPI_Barrier(CLUSTER);

        // najavi da primamo još (more=1) i podeli port
        bcast_more_and_port(CLUSTER, /*more=*/1, port);

        // kolektivni accept preko CLUSTER-a (prvi put učestvuje samo master)
        MPI_Comm inter;
        printf("Pre ACCEPT\n");
        fflush(stdout);

        rc = MPI_Comm_accept(port, MPI_INFO_NULL, 0, CLUSTER, &inter);
        perr("Comm_accept", rc);
        printf("posle ACCEPT\n");
        fflush(stdout);

        // === FAKE "TLS" HANDSHAKE: MASTER STRANA ===
        AuthClientHello ch;
        AuthServerHello sh;
        AuthProof p;

        // 1) Primi ClientHello od root-a nove grupe (remote rank 0)
        rc = MPI_Recv(&ch, sizeof(ch), MPI_BYTE, 0, TAG_AUTH_CLIENT_HELLO, inter, MPI_STATUS_IGNORE);
        perr("Recv(AUTH_CLIENT_HELLO)", rc);

        // 2) Generiši server nonce i pošalji ServerHello
        sh.nonce_server = rand();

        rc = MPI_Send(&sh, sizeof(sh), MPI_BYTE, 0, TAG_AUTH_SERVER_HELLO, inter);
        perr("Send(AUTH_SERVER_HELLO)", rc);

        // 3) Primi proof od workera
        rc = MPI_Recv(&p, sizeof(p), MPI_BYTE, 0, TAG_AUTH_PROOF, inter, MPI_STATUS_IGNORE);
        perr("Recv(AUTH_PROOF)", rc);

        // 4) Proveri proof
        int SECRET = 0x12345678; // isti kao u worker.c
        int expected = ch.nonce_client ^ sh.nonce_server ^ SECRET;

        accept_it = (p.worker_id == ch.worker_id) &&
                    (p.nonce_client == ch.nonce_client) &&
                    (p.nonce_server == sh.nonce_server) &&
                    (p.proof == expected);

        // 5) Pošalji rezultat novom root-u
        rc = MPI_Send(&accept_it, 1, MPI_INT, 0, TAG_AUTH_RESULT, inter);
        perr("Send(AUTH_RESULT)", rc);

        // 6) Broadcast odluke svim starim članovima CLUSTER-a
        MPI_Bcast(&accept_it, 1, MPI_INT, 0, CLUSTER);

        if (!accept_it)
        {
            // AUTH odbijen → svi u CLUSTER-u rade DISCONNECT nad istim inter
            MPI_Comm_disconnect(&inter);

            // poravnanje kraja runde
            MPI_Barrier(CLUSTER);
            printf("AUTH FAILED, rejecting wave\n");
            fflush(stdout);
            continue; // bez merge-a u ovoj rundi
        }

        // ako smo stigli ovde, auth je prošao → možemo da računamo session key
        int pending_K = ch.nonce_client ^ sh.nonce_server ^ SECRET;
        printf("[MASTER] session key (pending) = 0x%x for worker_id=%d\n", pending_K, ch.worker_id);
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
        // pretpostavka: u ovoj rundi je došao TAČNO jedan novi worker → on je rank = s-1
        int new_rank = s - 1;
        g_session_keys[new_rank] = pending_K;

        printf("[MASTER] merged wave -> size=%d (workers=%d)\n", s, s - 1);
        fflush(stdout);
        added++;
    }

    printf("USAAOOOOO\n");
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
        // inicijalna raspodela
        for (int w = 1; w < size; ++w)
            if (next < NT)
                secure_send_task(tasks[next++], w, CLUSTER); // ŠIFROVANO SLANJE
            else
                MPI_Send(NULL, 0, MPI_INT, w, TAG_IDLE, CLUSTER);

        // glavna petlja
        for (;;)
        {
            int pair[2];
            MPI_Status st;
            MPI_Recv(pair, 2, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, CLUSTER, &st);
            printf("[MASTER] result: %d -> %d (from %d)\n",
                   pair[0], pair[1], st.MPI_SOURCE);
            fflush(stdout);

            if (next < NT)
                secure_send_task(tasks[next++], st.MPI_SOURCE, CLUSTER); // opet šifrovano
            else
                MPI_Send(NULL, 0, MPI_INT, st.MPI_SOURCE, TAG_IDLE, CLUSTER);
        }
    }

    MPI_Comm_free(&CLUSTER);
}