/******************************************************************************
 * Authors: Jose Sanchez-Yun (pepesy00@uma.es)
 *          Eladio Gutierrez (eladio@uma.es)
 *          Ricardo Quislant (quislant@uma.es)
 *          Oscar Plata (oplata@uma.es)
 *
 * University: Dept. of Computer Architecture, University of Malaga,
 *             Bulevar Louis Pasteur, 35, Malaga, 29071, Andalusia, Spain
 ******************************************************************************/
#ifndef BARRIERS_H
#define BARRIERS_H

#include <pthread.h>
#include <assert.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "htmintrin.h"

#define LOCK_TAKEN 0xFF
#define VALIDATION_ERROR 0xFE
#define CACHE_BLOCK_SIZE 128

#define MAX_THREADS 128
#define MAX_SPEC    2
#define MAX_RETRIES 5
#define MAX_CAPACITY_RETRIES 3

// This macro must always match the number of transactions (xacts) passed to statsFileInit, otherwise statistics will be incorrect.
#define MAX_XACT_IDS 1

/* This macro is defined to indicate which transaction corresponds to the barrier,
   so that transactions can be defined alongside the barrier and it is the last one
   in the statistics file. */
#define SPEC_XACT_ID MAX_XACT_IDS-1

/* Macros for an escape transaction (used to read and write variables that would normally cause a conflict abort)
   within a transaction */
#define BEGIN_ESCAPE __builtin_tsuspend()
#define END_ESCAPE __builtin_tresume()

/* Initializes the variables required to implement a transaction; must be called once per thread 
   at the beginning of the parallel block */
#define TX_DESCRIPTOR_INIT()        tm_tx_t tx;                                 \
                                    tx.order = 1;                               \
                                    tx.retries = 0;                             \
                                    tx.speculative = 0


// Initializes the global variables required for the barriers
#define BARRIER_DESCRIPTOR_INIT(numTh) g_specvars.barrier.nb_threads = numTh;   \
                                       g_specvars.barrier.remain     = numTh


// Begins a transaction
#define BEGIN_TRANSACTION(thId, xId)                                                     \
  assert(xId != SPEC_XACT_ID); /* Ensure it does not have the same id as sb */  \
  if(!tx.speculative) {                                                         \
    __label__ __p_failure;                                                      \
    texasru_t __p_abortCause;                                                   \
__p_failure:                                                                    \
    __p_abortCause = __builtin_get_texasru ();                                  \
    if(tx.retries) profileAbortStatus(__p_abortCause, thId, xId);               \
    tx.retries++;                                                               \
    if (tx.retries > MAX_RETRIES) {                                             \
      unsigned int myticket = __sync_add_and_fetch(&(g_fallback_lock.ticket), 1); \
      while(myticket != g_fallback_lock.turn) ;                                 \
    } else {                                                                    \
      while (g_fallback_lock.ticket >= g_fallback_lock.turn);                   \
      if(!__builtin_tbegin(0)) goto __p_failure;                                \
      if (g_fallback_lock.ticket >= g_fallback_lock.turn)                       \
      __builtin_tabort(LOCK_TAKEN);/*Early subscription*/                       \
    }                                                                           \
  }

// Commits a transaction
#define TM_STOP(thId, xId)                                                      \
      if(!tx.speculative) {                                                     \
        if (tx.retries <= MAX_RETRIES) {                                        \
          __builtin_tend(0);                                                    \
          profileCommit(thId, xId, tx.retries-1);                               \
        } else {                                                                \
          __sync_add_and_fetch(&(g_fallback_lock.turn), 1);                     \
          profileFallback(thId, xId, tx.retries-1);                             \
        }                                                                       \
        tx.retries = 0;                                                         \
        tx.specLevel = tx.specMax;                                              \
      } else {                                                                  \
        BEGIN_ESCAPE;                                                           \
        if (tx.order <= g_specvars.tx_order) {                                  \
          END_ESCAPE;                                                           \
          __builtin_tend(0);                                                    \
          profileCommit(thId, SPEC_XACT_ID, tx.retries-1); /* ID of the speculative xact opened in SB_BARRIER */ \
          tx.speculative = 0;                                                   \
          tx.retries = 0;                                                       \
          tx.specLevel = tx.specMax;                                            \
        } else {                                                                \
          END_ESCAPE;                                                           \
          tx.specLevel--;                                                       \
          if (tx.specLevel == 0) {                                              \
            BEGIN_ESCAPE;                                                       \
            while (tx.order > g_specvars.tx_order);                             \
            END_ESCAPE;                                                         \
            __builtin_tend(0);                                                  \
            profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                    \
            tx.speculative = 0;                                                 \
            tx.retries = 0;                                                     \
            tx.specLevel = tx.specMax;                                          \
          }                                                                     \
        }                                                                       \
      }

// Begins a speculative barrier
#define SB_BARRIER(thId)                                                        \
  /* Check if it's in speculative mode */                                       \
  if (tx.speculative) {                                                         \
    BEGIN_ESCAPE;                                                               \
    while (tx.order > g_specvars.tx_order);                                     \
    END_ESCAPE;                                                                 \
    __builtin_tend(0);                                                          \
    profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                            \
    tx.speculative = 0;                                                         \
    tx.retries = 0;                                                             \
    __builtin_set_texasru (0);                                                  \
  }                                                                             \
  tx.order += 1;                                                                \
  /* Check if it is the last one to pass the barrier */                        \
  if (__sync_add_and_fetch(&(g_specvars.barrier.remain),-1) == 0) {             \
    g_specvars.barrier.remain = g_specvars.barrier.nb_threads;                  \
    __sync_add_and_fetch(&(g_specvars.tx_order), 1);                            \
  } else {                                                                      \
    __label__ __p_failure;                                                      \
    texasru_t __p_abortCause;                                                   \
__p_failure:                                                                    \
    __p_abortCause = __builtin_get_texasru ();                                  \
    /* If there are retries, it means it aborted; record the error */           \
    if(tx.retries) profileAbortStatus(__p_abortCause, thId, SPEC_XACT_ID);      \
    tx.retries++;                                                               \
    if (tx.order <= g_specvars.tx_order) {                                      \
      tx.speculative = 0;                                                       \
      tx.retries = 0;                                                           \
    } else {                                                                    \
            tx.speculative = 1;                                                 \
        if(_TEXASRU_TRANSACTION_CONFLICT(__p_abortCause) || _TEXASRU_FOOTPRINT_OVERFLOW(__p_abortCause)){			\
          /* Random Backoff */                                                  \
          srand(time(NULL));							\
          usleep((rand() % 30));					        \
        }                                                                       \
        if(!__builtin_tbegin(0)) goto __p_failure;                              \
      }                                                                         \
    }

// Last barrier to pass
#define LAST_BARRIER(thId)                                                      \
  /* If in speculative mode, wait for others and commit */                      \
  if (tx.speculative) {                                                         \
    BEGIN_ESCAPE;                                                               \
    while (tx.order > g_specvars.tx_order);                                     \
    END_ESCAPE;                                                                 \
    __builtin_tend(0);                                                          \
    profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                            \
    tx.speculative = 0;                                                         \
    tx.retries = 0;                                                             \
  }                                                                             \
  tx.order += 1;                                                                \
  /* If it's the last thread, increment the global order and finish, otherwise, wait */\
  if (__sync_add_and_fetch(&(g_specvars.barrier.remain),-1) == 0) {             \
    g_specvars.barrier.remain = g_specvars.barrier.nb_threads;                  \
    __sync_add_and_fetch(&(g_specvars.tx_order), 1);                            \
  } else {                                                                      \
    while(tx.order > g_specvars.tx_order) ;                                     \
  }

// Macro checkspec
#define CHECK_SPEC(thId)                                                      \
      /* Commit */                                                              \
      if(tx.speculative) {                                                      \
        BEGIN_ESCAPE;                                                           \
        if (tx.order <= g_specvars.tx_order) {                                  \
          END_ESCAPE;                                                           \
          __builtin_tend(0);                                                    \
          profileCommit(thId, SPEC_XACT_ID, tx.retries-1); /* ID of the speculative xact opened in SB_BARRIER */ \
          tx.speculative = 0;                                                   \
          tx.retries = 0;                                                       \
	/* Wait and commit */                                                   \
        } else {                                                                \
          END_ESCAPE;                                                           \
            BEGIN_ESCAPE;                                                       \
            while (tx.order > g_specvars.tx_order);                             \
            END_ESCAPE;                                                         \
            __builtin_tend(0);                                                  \
            profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                    \
            tx.speculative = 0;                                                 \
            tx.retries = 0;                                                     \
        }                                                                       \
      }

typedef struct fback_lock {
  volatile uint32_t ticket;
  volatile uint32_t turn;
  uint8_t pad[CACHE_BLOCK_SIZE-sizeof(uint32_t)*2];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) fback_lock_t;

// Declared in stats.c
extern fback_lock_t g_fallback_lock;

/* Transaction descriptor */
// Padding is added using pad1 and pad2 to prevent false sharing
typedef struct tm_tx {
  uint32_t order;
  uint8_t pad1[CACHE_BLOCK_SIZE-sizeof(uint32_t)];
  uint32_t retries;
  uint8_t speculative; 
  uint8_t pad2[CACHE_BLOCK_SIZE-sizeof(uint32_t)*3-sizeof(uint8_t)];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) tm_tx_t;

/* Transactional barrier descriptor */
typedef struct barrier {
  int nb_threads; /* Number of threads waiting at the barrier */
  volatile uint32_t remain; /* Remaining threads until the barrier is unlocked */
} barrier_t;

// Structure to place the global tx order and the barrier
typedef struct global_spec_vars {
  volatile uint32_t tx_order; // Must be initialized to 1
  uint8_t pad1[CACHE_BLOCK_SIZE-sizeof(uint32_t)];
  barrier_t barrier;
  uint8_t pad2[CACHE_BLOCK_SIZE-sizeof(barrier_t)];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) g_spec_vars_t;

// Structure that holds the global variables required for the barriers
extern g_spec_vars_t g_specvars;


#endif
