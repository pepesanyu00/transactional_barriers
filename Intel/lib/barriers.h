/******************************************************************************
 * Authors: Jose Sanchez-Yun (pepesy00@uma.es)
 *          Eladio Gutierrez (eladio@uma.es)
 *          Ricardo Quislant (quislant@uma.es)
 *          Oscar Plata (oplata@uma.es)
 *
 * University: Dept. of Computer Architecture, University of Malaga,
 *             Bulevarপ্রবাসী Bulevar Louis Pasteur, 35, Malaga, 29071, Andalusia, Spain
 ******************************************************************************/
#ifndef BARRIERS_H_
#define BARRIERS_H_


#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>



// Intel recommended pause intrinsic for spin-wait loops
#define CPU_RELAX  _mm_pause()

// Error code indicating that the lock is taken by another thread
#define LOCK_TAKEN 0xFF

#define CACHE_BLOCK_SIZE 64

#define GLOBAL_RETRIES 3

#define MAX_THREADS 128

#define MAX_SPEC    4

#define MAX_RETRIES 5

#define MAX_CAPACITY_RETRIES 50

// This macro must always match the number of transactions (xacts) passed to statsFileInit, otherwise the statistics will be wrong.
#define MAX_XACT_IDS 1

/* This macro is defined to indicate which transaction corresponds to the barrier,
   so that transactions can be defined along with the barrier and this one will be the last
   in the statistics file */
#define SPEC_XACT_ID MAX_XACT_IDS-1

/* Macros to make an escaped transaction (to be able to read and write variables that would normally cause an abort due to conflict)
   inside a transaction */
#define BEGIN_ESCAPE _xsusldtrk()
#define END_ESCAPE _xresldtrk()




/* initializes the necessary variables to implement a transaction, must be called once by each thread 
   at the beginning of the parallel block  */
#define TX_DESCRIPTOR_INIT()        tm_tx_t tx;                                 \
                                    tx.order = 1;                               \
                                    tx.retries = 0;                             \
                                    tx.speculative = 0;                         \
                                    tx.status = 0

// Initializes the global variables necessary for the barriers
#define BARRIER_DESCRIPTOR_INIT(numTh) g_specvars.barrier.nb_threads = numTh;   \
                                       g_specvars.barrier.remain     = numTh



// Starts a transaction
#define BEGIN_TRANSACTION(thId, xId)                                                         \
  tx.retries = 0;                                                           \
  do                                                                           \
  {                                                                            \
    assert(xId != SPEC_XACT_ID);                                               \
    if (tx.retries){                                                          \
      profileAbortStatus(tx.status, thId, xId);                                     \
    }                                                                          \
    tx.retries++;                                                             \
    if (tx.retries > GLOBAL_RETRIES)                                          \
    {                                                                          \
      unsigned int myticket = __sync_add_and_fetch(&(g_ticketlock.ticket), 1); \
      while (myticket != g_ticketlock.turn)                                    \
        BEGIN_ESCAPE;                                                         \
        CPU_RELAX();                                                           \
        END_ESCAPE;                                                           \
      break;                                                                   \
    }                                                                          \
    while (g_ticketlock.ticket >= g_ticketlock.turn)                           \
      BEGIN_ESCAPE;                                                           \
      CPU_RELAX(); /* Avoid Lemming effect */                                  \
      END_ESCAPE;                                                             \
  } while ((tx.status = _xbegin()) != _XBEGIN_STARTED)


// commits a transaction
#define COMMIT_TRANSACTION(thId, xId)                                                      \
      if(!tx.speculative) {                                                     \
        if (tx.retries <= MAX_RETRIES) {                                        \
          _xend();                                                    \
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
          _xend();                                                    \
          profileCommit(thId, SPEC_XACT_ID, tx.retries-1); /* Speculative xact ID opened in SB_BARRIER*/ \
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
            _xend();                                                  \
            profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                    \
            tx.speculative = 0;                                                 \
            tx.retries = 0;                                                     \
            tx.specLevel = tx.specMax;                                          \
          }                                                                     \
        }                                                                       \
      }

#define SB_BARRIER(thId)                                                        \
  /* Check if the thread enters the barrier for the first time (if it's in speculative mode or not) */ \
  if (tx.speculative) {                                                         \
    BEGIN_ESCAPE;                                                               \
    while (tx.order > g_specvars.tx_order);                                     \
    END_ESCAPE;                                                                 \
    /* Here I have finished a barrier so I can commit the transaction to later*/ \
    /* start the next one.*/   \
    _xend();                                                          \
    profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                            \
    tx.speculative = 0;                                                         \
    tx.retries = 0;                                                             \
  }                                                                             \
  /* increment the order of the thread */             \
  tx.order += 1;                                                                \
  tx.status = 0;                                                                \
  /* Determine if the thread is the last to enter the barrier */                        \
  if (__sync_add_and_fetch(&(g_specvars.barrier.remain),-1) == 0) {             \
    /* If the last to cross the barrier, reset and increment the global order*/                                    \
    g_specvars.barrier.remain = g_specvars.barrier.nb_threads;                  \
    __sync_add_and_fetch(&(g_specvars.tx_order), 1);                            \
  } else {                                                                      \
    __label__ __p_failure;                                                      \
__p_failure:                                                                    \
    if(tx.retries){                                                             \
      profileAbortStatus(tx.status, thId, SPEC_XACT_ID);                       \
    }                                                                           \
    tx.retries++;                                                               \
    if (tx.order <= g_specvars.tx_order) {                                      \
      tx.speculative = 0;                                                       \
      tx.retries = 0;                                                           \
    } else {                                                                    \
      tx.speculative = 1;                                                       \
        if((tx.status & _XABORT_CONFLICT) || (tx.status & _XABORT_CAPACITY)){			                \
          srand(time(NULL));							                                      \
          usleep((rand() % 30));							                                \
        }										                                                    \
        if((tx.status = _xbegin()) != _XBEGIN_STARTED) {goto __p_failure;}        \
      }                                                                         \
  }
                                                                  

/* Last barrier before finishing the execution, unlike the other macro, 
   this one does not open another transaction but finishes. */
#define LAST_BARRIER(thId)                                                      \
  if (tx.speculative) {                                                         \
    BEGIN_ESCAPE;                                                               \
    while (tx.order > g_specvars.tx_order);                                     \
    END_ESCAPE;                                                                 \
    _xend();                                                          \
    profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                            \
    tx.speculative = 0;                                                         \
    tx.retries = 0;                                                             \
  }                                                                             \
  tx.order += 1;                                                                \
  if (__sync_add_and_fetch(&(g_specvars.barrier.remain),-1) == 0) {             \
    g_specvars.barrier.remain = g_specvars.barrier.nb_threads;                  \
    __sync_add_and_fetch(&(g_specvars.tx_order), 1);                            \
  /* Here is the difference with SB_BARRIER, it doesn't create another transaction but waits for the others and finishes */ \
  } else {                                                                      \
    while(tx.order > g_specvars.tx_order) ;                                     \
  }

//  Finishes the transaction at the indicated point, so that transactions are not overly long
#define CHECK_SPEC(thId, xId)                                                      \
      if(tx.speculative) {                                                      \
        BEGIN_ESCAPE;                                                           \
        if (tx.order <= g_specvars.tx_order) {                                  \
          END_ESCAPE;                                                           \
          _xend();                                                 \
          profileCommit(thId, SPEC_XACT_ID, tx.retries-1); /* Speculative xact ID opened in SB_BARRIER*/ \
          tx.speculative = 0;                                                   \
          tx.retries = 0;                                                       \
        } else {                                                                \
            while (tx.order > g_specvars.tx_order);                             \
            END_ESCAPE;                                                         \
            _xend();                                               \
            profileCommit(thId, SPEC_XACT_ID, tx.retries-1);                    \
            tx.speculative = 0;                                                 \
            tx.retries = 0;                                                     \
        }                                                                       \
      }




// Tickets to perform the execution sequentially in case it is necessary
struct TicketLock
{
  volatile char pad1[CACHE_BLOCK_SIZE];
  volatile unsigned int ticket;
  volatile unsigned int turn;
  volatile char pad2[CACHE_BLOCK_SIZE];
};

// Declared in stats.c 
extern struct TicketLock g_ticketlock;

/* Transaction Descriptor */
typedef struct tm_tx {
  uint32_t order; /* Local order of the transaction */
  uint8_t pad1[CACHE_BLOCK_SIZE-sizeof(uint32_t)];
  uint32_t retries; /* Number of pending retries before falling back */
  uint8_t speculative; /* True if the transaction is open */
  uint32_t status;  /* Transaction status. */
  uint32_t capRetries;
  uint8_t pad2[CACHE_BLOCK_SIZE-sizeof(uint32_t)*3-sizeof(uint8_t)]; 
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) tm_tx_t;


/* Transactional barrier descriptor */
typedef struct barrier {
  int nb_threads; /* Number of threads waiting at the barrier */
  volatile uint32_t remain; /* Remaining threads until the barrier is unblocked */
} barrier_t;


typedef struct global_spec_vars {
  volatile uint32_t tx_order; // Must be initialized to 1
  uint8_t pad1[CACHE_BLOCK_SIZE-sizeof(uint32_t)];
  barrier_t barrier;
  uint8_t pad2[CACHE_BLOCK_SIZE-sizeof(barrier_t)];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) g_spec_vars_t;

// Structure that saves the global variables necessary for the barriers
extern g_spec_vars_t g_specvars;


/* Lock for fallback */
typedef struct fback_lock {
  volatile uint32_t ticket;
  volatile uint32_t turn;
  uint8_t pad[CACHE_BLOCK_SIZE-sizeof(uint32_t)*2];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) fback_lock_t;

extern fback_lock_t g_fallback_lock;

#endif
