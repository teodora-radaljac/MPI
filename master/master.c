// master.c — primi tačno TARGET_WORKERS workera pa startuj posao
// Build: mpicc -O2 -std=gnu11 -o master master.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "master.h"

int writePortToFile(const char *file, const char *port)
{
  FILE *f = fopen(file, "w");

  if (f)
  {
    fprintf(f, "%s\n", port);
    fclose(f);
    return 0;
  }

  char err_message[256];
  snprintf(err_message, sizeof(err_message),
           "[MASTER] fopen %s", file);
  perror(err_message);

  return -1;
}

int main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);
  int ret = 0;

  const int target = getenv_int("TARGET_WORKERS", 1);

  char PORT[MPI_MAX_PORT_NAME];
  ret = MPI_Open_port(MPI_INFO_NULL, PORT);
  perr("Open_port", ret);
  if (ret != MPI_SUCCESS)
  {
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  printf("%s\n", PORT);
  fflush(stdout);

  ret = writePortToFile(PORT_FILE_NAME, PORT);
  if (ret != 0)
  {
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  printf("[MASTER] Mode: %s\n", argv[1]);
  mode_comm mode = parse_mode(argv[1]);

  switch (mode)
  {
  case MODE_AUTH:
    master_auth(PORT, target);
    break;
  case MODE_NOTLS:
    master_noTLS(PORT, target);
    break;
  case MODE_TLS:
    master_TLS(PORT, target);
    break;
  default:
    printf("[MASTER] Invalid mode specified.\n");
    break;
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
