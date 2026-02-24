/******************************************************************************
 * Authors: Jose Sanchez-Yun (pepesy00@uma.es)
 *          Eladio Gutierrez (eladio@uma.es)
 *          Ricardo Quislant (quislant@uma.es)
 *          Oscar Plata (oplata@uma.es)
 *
 * University: Dept. of Computer Architecture, University of Malaga,
 *             Bulevar Louis Pasteur, 35, Malaga, 29071, Andalusia, Spain
 ******************************************************************************/
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


// MACROS DEFINED IN BARRIERS.H
//#define CACHE_BLOCK_SIZE 64
//#define LOCK_TAKEN 0xFF

// Structure to save statistics
struct Stats {
  char pad1[CACHE_BLOCK_SIZE]; // Pads to avoid conflicting traffic between xabortCount of one thread and xcommitCount of another in a cache block
  unsigned long int xabortCount; // Total number of aborts
  unsigned long int explicitAborts; // Number of XABORT calls in the code
  unsigned long int explicitAbortsSubs; // Number of explicit aborts due to lock subscription
  unsigned long int explicitAbortsAddPath; // Number of explicit aborts due to lock subscription
  unsigned long int persistentAborts; // Aborts for which the hardware assumes we should retry
  unsigned long int disallowedAborts; // Conflict aborts
  unsigned long int nestingAborts; // Capacity aborts
  unsigned long int footprintAborts; // Capacity aborts
  unsigned long int selfInducedAborts; // Aborts caused by a debugger breakpoint
  unsigned long int nontransactAborts; // Aborts inside a nested transaction
  unsigned long int transactAborts; // Aborts with eax = 0
  unsigned long int tlbAborts; // Aborts with eax = 0
  unsigned long int implementationAborts; // Aborts with eax = 0
  unsigned long int fetchAborts; // Aborts with eax = 0
  unsigned long int otherAborts; // Should be 0
  unsigned long int xcommitCount; // Number of commits
  unsigned long int fallbackCount; // Number of fallbacks
  unsigned long int retryCCount; // Number of retries for those that commit
  unsigned long int retryFCount; // Number of retries for those that enter fallback
  char pad2[CACHE_BLOCK_SIZE];
};

extern struct Stats **stats;

int statsFileInit(int argc, char **argv, long thCount);
int dumpStats(float time, int ver);


// Statistics profiling functions (made inline to improve performance)

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
