#include "stats.h"

char fname[256];
long threadCount;
struct Stats **stats;


int statsFileInit(int argc, char **argv, long thCount) {
  int i,j;
  char ext[25];
  //Saco la extensión con identificador de proceso para tener un archivo único
  sprintf(ext,"stats/%d.stats", getpid());
  strncpy(fname, ext, sizeof(fname)-1);
  printf("Nombre del fichero: %s",fname);
  //Inicio los arrays de estadísticas
  threadCount = thCount;
  
  stats = (struct Stats **)malloc(sizeof(struct Stats *)*thCount);
  if(!stats) return 0;
  for(i=0; i<thCount; i++) {
    stats[i] = (struct Stats *)malloc(sizeof(struct Stats)*MAX_XACT_IDS);
    if(!stats[i]) return 0;
  }
  
  for(i=0; i<thCount; i++) {
    for(j=0; j<MAX_XACT_IDS; j++) {
    stats[i][j].xabortCount = 0;
    stats[i][j].explicitAborts = 0;
    stats[i][j].explicitAbortsSubs = 0;
    stats[i][j].explicitAbortsAddPath = 0;
    stats[i][j].persistentAborts = 0;
    stats[i][j].disallowedAborts = 0;
    stats[i][j].nestingAborts = 0;
    stats[i][j].footprintAborts = 0;
    stats[i][j].selfInducedAborts = 0;
    stats[i][j].nontransactAborts = 0;
    stats[i][j].transactAborts = 0;
    stats[i][j].tlbAborts = 0;
    stats[i][j].implementationAborts = 0;
    stats[i][j].fetchAborts = 0;
    stats[i][j].otherAborts = 0;
    stats[i][j].xcommitCount = 0;
    stats[i][j].fallbackCount = 0;
    stats[i][j].retryCCount = 0;
    stats[i][j].retryFCount = 0;
    }
  }

  return 1;
}

int dumpStats(float time, int ver) {
  FILE *f;
  int i,j;
  unsigned long int tmp, comm, fall, retComm;
  
  //Creo el fichero
  f = fopen(fname,"w");
  if(!f) return 0;
  printf("Writing stats to: %s\n", fname);
  fprintf(f, "-----------------------------------------\nOutput file: %s\n----------------- Stats -----------------\n", fname);
  fprintf(f, "#Threads: %li\n", threadCount);
  if(ver) fprintf(f, "Verification passed.\n");
  else fprintf(f,"Verification wrong.\n");

  fprintf(f, "\nTime(s): %f\n", time);
  
  fprintf(f, "\nAbort Count: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].xabortCount)
      fprintf(f, "%lu ", stats[i][j].xabortCount);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "Explicit aborts: ");  
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].explicitAborts)
      fprintf(f, "%lu ", stats[i][j].explicitAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  fprintf(f, "  »     Subs: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].explicitAbortsSubs)
      fprintf(f, "%lu ", stats[i][j].explicitAbortsSubs);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "  »  AddPath: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].explicitAbortsAddPath)
      fprintf(f, "%lu ", stats[i][j].explicitAbortsAddPath);
  }
  fprintf(f, "Total: %lu\n", tmp);
      
  fprintf(f, "-Disallowed aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].disallowedAborts)
      fprintf(f, "%lu ", stats[i][j].disallowedAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  fprintf(f, "-Nesting overflow aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].nestingAborts)
      fprintf(f, "%lu ", stats[i][j].nestingAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  fprintf(f, "-Footprint overflow aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].footprintAborts)
      fprintf(f, "%lu ", stats[i][j].footprintAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  fprintf(f, "-Self-induced conflict aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].selfInducedAborts)
      fprintf(f, "%lu ", stats[i][j].selfInducedAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "  » Persistent: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].persistentAborts)
      fprintf(f, "%lu ", stats[i][j].persistentAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "Non-transactional conflict aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].nontransactAborts)
      fprintf(f, "%lu ", stats[i][j].nontransactAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "Transactional conflict aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].transactAborts)
      fprintf(f, "%lu ", stats[i][j].transactAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
    
  fprintf(f, "TLB invalidation aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].tlbAborts)
      fprintf(f, "%lu ", stats[i][j].tlbAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  fprintf(f, "Implementation specific aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].implementationAborts)
      fprintf(f, "%lu ", stats[i][j].implementationAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  fprintf(f, "Instruction fetch conflict aborts: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0, tmp=0; i<threadCount; tmp += stats[i++][j].fetchAborts)
      fprintf(f, "%lu ", stats[i][j].fetchAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "Other aborts (deberían ser 0): ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].otherAborts)
      fprintf(f, "%lu ", stats[i][j].otherAborts);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fprintf(f, "Commits: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].xcommitCount)
      fprintf(f, "%lu ", stats[i][j].xcommitCount);
  }
  fprintf(f, "Total: %lu\n", tmp);
  
  comm = tmp;
  fprintf(f,"Fallbacks: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].fallbackCount)
      fprintf(f, "%lu ", stats[i][j].fallbackCount);
  }
  fprintf(f, "Total: %lu\n", tmp);

  fall = tmp;
  fprintf(f, "RetriesCommited: ");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].retryCCount)
      fprintf(f, "%lu ", stats[i][j].retryCCount);
  }
  fprintf(f, "Total: %lu ", tmp);
  retComm = tmp;
  fprintf(f,"PerXact: %f\n",(float)tmp/(float)comm);
  
  fprintf(f,"RetriesFallbacked:");
  for(j=0, tmp=0; j<MAX_XACT_IDS; j++) {
    (j==SPEC_XACT_ID)? fprintf(f, " XIDSB: ") : fprintf(f, " XID%d: ", j);
    for(i=0; i<threadCount; tmp += stats[i++][j].retryFCount)
      fprintf(f, "%lu ", stats[i][j].retryFCount);
  }
  fprintf(f, "Total: %lu ", tmp);
  fprintf(f,"PerXact: %f\nRetriesAvg: %f", (float)tmp/(float)fall,
                     (float)(retComm+tmp)/(float)(comm+fall));
  fclose(f);
  free(stats);
  return 1;
}
