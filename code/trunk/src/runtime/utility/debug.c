#include "debug.h"
#include "pthread.h"
#include "record.h"

#ifdef DISTRIBUTED_SNET
#include <mpi.h>
#endif /* DISTRIBUTED_SNET */


extern char* SNetUtilDebugDumpRecord(snet_record_t *source, char* storage) {
  if(source == NULL) {
    sprintf(storage, "(RECORD NONE)");
  } else {
    switch(SNetRecGetDescriptor(source)) {
      case REC_data:
        sprintf(storage, "(RECORD %p DATA)", 
              source);
      break;

      case REC_sync:
        sprintf(storage, "(RECORD %p SYNC (NEW INPUT %p))",
              source,
              SNetRecGetStream(source));
      break;

      case REC_collect:
        sprintf(storage, "(RECORD %p COLLECT (NEW STREAM %p))",
              source,
              SNetRecGetStream(source));
      break;

      case REC_sort_begin:
        sprintf(storage, "(RECORD %p SORTBEGIN (LEVEL %d) (NUM %d))",
              source,
              SNetRecGetLevel(source),
              SNetRecGetNum(source));
      break;

      case REC_sort_end:
        sprintf(storage, "(RECORD %p SORTEND (LEVEL %d) (NUM %d))",
              source,
              SNetRecGetLevel(source),
              SNetRecGetNum(source));
      break;

      case REC_terminate:
        sprintf(storage, "(RECORD %p TERMINATE)",
              source);
      break;

      case REC_probe:
        sprintf(storage, "(RECORD %p PROBE)",
              source);
      break;
    }
  }
  return storage;
}

extern void SNetUtilDebugFatal(char* m, ...) {
  va_list p;
#ifdef DISTRIBUTED_SNET
  int my_rank;

  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  va_start(p, m);
  fprintf(stderr, "(SNET FATAL (NODE %d THREAD %lu) ", my_rank, pthread_self());
#else
  va_start(p, m);
  fprintf(stderr, "(SNET FATAL (THREAD %lu) ", pthread_self());
#endif /* DISTRIBUTED_SNET */

  vfprintf(stderr, m, p);
  fputs(")\n\n", stderr);
  va_end(p);
  abort();
}

extern void SNetUtilDebugNotice(char *m, ...) {
  va_list p;

#ifdef DISTRIBUTED_SNET
  int my_rank;

  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  va_start(p, m);
  fprintf(stderr, "(SNET NOTICE (NODE %d THREAD %lu) ", my_rank, pthread_self());
#else
  va_start(p, m);
  fprintf(stderr, "(SNET NOTICE (THREAD %lu) ", pthread_self());
#endif /* DISTRIBUTED_SNET */
  vfprintf(stderr, m, p);
  fputs(")\n", stderr);
  fflush(stderr);
  va_end(p);
}
