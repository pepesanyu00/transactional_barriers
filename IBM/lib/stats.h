#ifndef STATS_H_
#define STATS_H_


#include <pthread.h>
#include <assert.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include "htmintrin.h"
#include "barriers.h"


// MACROS DEFINIDAS EN BARRIERS.H
//#define CACHE_BLOCK_SIZE 64
//#define LOCK_TAKEN 0xFF

// Estructura para guardar las estadísticas
struct Stats {
  char pad1[CACHE_BLOCK_SIZE];//Pads para que no haya tráfico que no coincidan xabortCount de un thread con xcommitCount the otro en un bloque cache
  unsigned long int xabortCount; //Número total de abortos
  unsigned long int explicitAborts; //Número de llamadas a XABORT en el código
  unsigned long int explicitAbortsSubs; //Número de abortos explícitos por subscripción de lock
  unsigned long int explicitAbortsAddPath; //Número de abortos explícitos por subscripción de lock
  unsigned long int persistentAborts; //Abortos para los que el hardware piensa que debemos reintentar
  unsigned long int disallowedAborts; //Abortos por conflicto
  unsigned long int nestingAborts; //Abortos por capacidad
  unsigned long int footprintAborts; //Abortos por capacidad
  unsigned long int selfInducedAborts; //Abortos por breakpoint de debugger
  unsigned long int nontransactAborts; //Abortos dentro de una transacción anidada
  unsigned long int transactAborts; //Abortos con eax = 0
  unsigned long int tlbAborts; //Abortos con eax = 0
  unsigned long int implementationAborts; //Abortos con eax = 0
  unsigned long int fetchAborts; //Abortos con eax = 0
  unsigned long int otherAborts; //deberian ser 0
  unsigned long int xcommitCount; //Número de commits
  unsigned long int fallbackCount; //Número de fallbacks
  unsigned long int retryCCount; //Número de retries de las commitan
  unsigned long int retryFCount; //Número de retries de las entran en fallback
  char pad2[CACHE_BLOCK_SIZE];
};

extern struct Stats **stats;

int statsFileInit(int argc, char **argv, long thCount);
int dumpStats(float time, int ver);


// Funciones de profile de estadísticas (hechas inline para mejorar el rendimiento)

inline unsigned long profileAbortStatus(texasru_t cause, long thread, long xid) {
  stats[thread][xid].xabortCount++;
  if(_TEXASRU_ABORT(cause)) {
    stats[thread][xid].explicitAborts++;
    if(_TEXASRU_FAILURE_CODE(cause) == LOCK_TAKEN) stats[thread][xid].explicitAbortsSubs++;
    else if(_TEXASRU_FAILURE_CODE(cause) == VALIDATION_ERROR) {
      stats[thread][xid].explicitAbortsAddPath++;
      return 1;
    }
  } else if(_TEXASRU_DISALLOWED(cause)) {
    stats[thread][xid].disallowedAborts++;
    if(_TEXASRU_FAILURE_PERSISTENT(cause)) stats[thread][xid].persistentAborts++;
  } else if(_TEXASRU_NESTING_OVERFLOW(cause)) {
    stats[thread][xid].nestingAborts++;
    if(_TEXASRU_FAILURE_PERSISTENT(cause)) stats[thread][xid].persistentAborts++;
  } else if(_TEXASRU_FOOTPRINT_OVERFLOW(cause)) {
    stats[thread][xid].footprintAborts++;
    if(_TEXASRU_FAILURE_PERSISTENT(cause)) stats[thread][xid].persistentAborts++;
  } else if(_TEXASRU_SELF_INDUCED_CONFLICT(cause)) {
    stats[thread][xid].selfInducedAborts++;
    if(_TEXASRU_FAILURE_PERSISTENT(cause)) stats[thread][xid].persistentAborts++;
  } else if(_TEXASRU_NON_TRANSACTIONAL_CONFLICT(cause)) {
    stats[thread][xid].nontransactAborts++;
  } else if(_TEXASRU_TRANSACTION_CONFLICT(cause)) {
    stats[thread][xid].transactAborts++;
  } else if(_TEXASRU_TRANSLATION_INVALIDATION_CONFLICT(cause)) {
    stats[thread][xid].tlbAborts++;
  } else if(_TEXASRU_IMPLEMENTAION_SPECIFIC(cause)) {
    stats[thread][xid].implementationAborts++;
  } else if(_TEXASRU_INSTRUCTION_FETCH_CONFLICT(cause)) {
    stats[thread][xid].fetchAborts++;
  } else {
    stats[thread][xid].otherAborts++;
  }
  return 0;
}

inline void profileCommit(long thread, long xid, long retries) {
  stats[thread][xid].xcommitCount++;
  stats[thread][xid].retryCCount += retries;
}

inline void profileFallback(long thread, long xid, long retries) {
  stats[thread][xid].fallbackCount++;
  stats[thread][xid].retryFCount += retries;
}

#endif
