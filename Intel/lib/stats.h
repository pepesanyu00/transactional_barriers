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
#include <immintrin.h>
#include "barriers.h"

// MACROS DEFINED IN BARRIERS.H
//#define CACHE_BLOCK_SIZE 64
//#define LOCK_TAKEN 0xFF



// Structure to save statistics
struct Stats
{
  //Statistics are updated outside the transaction, but are reserved with malloc (could they fall near shared data accessed within the transaction and share a cache line?)
  volatile char pad1[CACHE_BLOCK_SIZE];  //Pads to avoid false sharing (so that xabortCount from one thread does not match retryFCount from another in a cache block)
  unsigned long int xabortCount;         //Total number of aborts
  unsigned long int explicitAborts;      //Number of calls to XABORT in the code
  unsigned long int explicitAbortsSubs;  //Number of explicit aborts due to lock subscription
  unsigned long int retryAborts;         //Aborts for which the hardware thinks we should retry
  unsigned long int retryConflictAborts; //Aborts for which the hardware thinks we should retry
  unsigned long int retryCapacityAborts; //Aborts for which the hardware thinks we should retry
  unsigned long int conflictAborts;      //Conflict aborts
  unsigned long int capacityAborts;      //Capacity aborts
  unsigned long int debugAborts;         //Debugger breakpoint aborts
  unsigned long int nestedAborts;        //Aborts within a nested transaction
  unsigned long int eaxzeroAborts;       //Aborts with eax = 0
  unsigned long int xcommitCount;        //Number of commits
  unsigned long int fallbackCount;       //Number of fallbacks
  unsigned long int retryCCount;         //Number of retries of those that commit
  unsigned long int retryFCount;         //Number of retries of those that enter fallback
  unsigned long int xbeginCount;         //Number of transactions that have been opened
  volatile char pad2[CACHE_BLOCK_SIZE];
};

extern struct Stats **stats;


//Functions for the statistics file
int statsFileInit(int argc, char **argv, long thCount, long xCount);
int dumpStats(double time);

// Statistics profile functions (made inline to improve performance)


// Function to determine the type of abort
inline unsigned long profileAbortStatus(unsigned long eax, long thread, long xid)
{
  stats[thread][xid].xabortCount++;
  if (eax & _XABORT_EXPLICIT)
  {
    stats[thread][xid].explicitAborts++;
    if (_XABORT_CODE(eax) == LOCK_TAKEN)
      stats[thread][xid].explicitAbortsSubs++;
  }
  if (eax & _XABORT_RETRY)
  {
    stats[thread][xid].retryAborts++;
    if (eax & _XABORT_CONFLICT)
      stats[thread][xid].retryConflictAborts++;
    if (eax & _XABORT_CAPACITY)
      stats[thread][xid].retryCapacityAborts++;
    if (eax & _XABORT_DEBUG)
      assert(0);
    if (eax & _XABORT_NESTED)
      assert(0);
  }
  if (eax & _XABORT_CONFLICT)
  {
    stats[thread][xid].conflictAborts++;
  }
  if (eax & _XABORT_CAPACITY)
  {
    stats[thread][xid].capacityAborts++;
  }
  if (eax & _XABORT_DEBUG)
  {
    stats[thread][xid].debugAborts++;
  }
  if (eax & _XABORT_NESTED)
  {
    stats[thread][xid].nestedAborts++;
  }
  if (eax == 0)
  {
    //All bits at zero (can occur due to a call to CPUID or something else)
    //See Section 8.3.5 RTM Abort Status Definition of the Intel Architecture
    //Instruction Set Extensions Programming Reference (2012))
    stats[thread][xid].eaxzeroAborts++;
  }
  return 0;
}

// Function to indicate a commit
inline void profileCommit(long thread, long xid, long retries)
{
  stats[thread][xid].xcommitCount++;
  stats[thread][xid].retryCCount += retries;
}

// Function to indicate a fallback
inline void profileFallback(long thread, long xid, long retries)
{
  stats[thread][xid].fallbackCount++;
  stats[thread][xid].retryFCount += retries;
}
#endif