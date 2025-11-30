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

// Esta macro siempre debe coincidir con el número de transacciones(xacts) que se pasen a statsFileInit, sino las estadísticas estarán mal.
#define MAX_XACT_IDS 1

/* Esta macro se define para indicar que transacción es la correspondiente a la barrera,
   para que se puedan definir transacciones junto con la barrera y que esta sea la última
   en el fichero de estadísticas */
#define SPEC_XACT_ID MAX_XACT_IDS-1

/* Macros para hacer una transacción escapada (para poder leer y escribir variables que normalmente darían aborto por conflicto)
   dentro de una transaccion */
#define BEGIN_ESCAPE __builtin_tsuspend()
#define END_ESCAPE __builtin_tresume()

/* inicializa las variables necesarias para implementar una transacción, debe ser llamada una vez por cada thread 
   al principio del bloque paralelo  */
#define TX_DESCRIPTOR_INIT()        tm_tx_t tx;                                 \
                                    tx.order = 1;                               \
                                    tx.retries = 0;                             \
                                    tx.speculative = 0


// Inicializa las variables globales necesarias para las barreras
#define BARRIER_DESCRIPTOR_INIT(numTh) g_specvars.barrier.nb_threads = numTh;   \
                                       g_specvars.barrier.remain     = numTh


// Empieza una transacción
#define BEGIN_TRANSACTION(thId, xId)                                                     \
  assert(xId != SPEC_XACT_ID); /* Me aseguro de que no tiene mismo id que sb*/  \
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

// commita una transacción
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
          profileCommit(thId, SPEC_XACT_ID, tx.retries-1); /* ID de la xact especulativa abierta en SB_BARRIER*/ \
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

// Comienza una barrera especulativa
#define SB_BARRIER(thId)                                                        \
  /* Se comprueba si está en modo especulativo*/                                \
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
  /* Se comprueba si es el último en pasar la barrera */                        \
  if (__sync_add_and_fetch(&(g_specvars.barrier.remain),-1) == 0) {             \
    g_specvars.barrier.remain = g_specvars.barrier.nb_threads;                  \
    __sync_add_and_fetch(&(g_specvars.tx_order), 1);                            \
  } else {                                                                      \
    __label__ __p_failure;                                                      \
    texasru_t __p_abortCause;                                                   \
__p_failure:                                                                    \
    __p_abortCause = __builtin_get_texasru ();                                  \
    /* Si hay retries significa que ha abortado, se registra el error*/         \
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

// Última barrera por pasar
#define LAST_BARRIER(thId)                                                      \
  /* Si está en especulativo, se espera a los demás y se commita */             \
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
  /* Si el el último hilo, se incrementa el global order y termina, sino espera*/\
  if (__sync_add_and_fetch(&(g_specvars.barrier.remain),-1) == 0) {             \
    g_specvars.barrier.remain = g_specvars.barrier.nb_threads;                  \
    __sync_add_and_fetch(&(g_specvars.tx_order), 1);                            \
  } else {                                                                      \
    while(tx.order > g_specvars.tx_order) ;                                     \
  }

// Macro checkspec
#define CHECK_SPEC(thId)                                                      \
      /* Commita*/                                                              \
      if(tx.speculative) {                                                      \
        BEGIN_ESCAPE;                                                           \
        if (tx.order <= g_specvars.tx_order) {                                  \
          END_ESCAPE;                                                           \
          __builtin_tend(0);                                                    \
          profileCommit(thId, SPEC_XACT_ID, tx.retries-1); /* ID de la xact especulativa abierta en SB_BARRIER*/ \
          tx.speculative = 0;                                                   \
          tx.retries = 0;                                                       \
	/* Espera y commita*/                                                   \
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

// Declarado en stats.c
extern fback_lock_t g_fallback_lock;

/* Descriptor de transacción */
// Con pad1 y pad2 se añade padding para prevenir el false sharing
typedef struct tm_tx {
  uint32_t order;
  uint8_t pad1[CACHE_BLOCK_SIZE-sizeof(uint32_t)];
  uint32_t retries;
  uint8_t speculative; 
  uint8_t pad2[CACHE_BLOCK_SIZE-sizeof(uint32_t)*3-sizeof(uint8_t)];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) tm_tx_t;

/* Descriptor de barrera transaccional */
typedef struct barrier {
  int nb_threads; /* Número de threads que esperan en la barrera */
  volatile uint32_t remain; /* Threads restantes hasta desbloquear la barrera */
} barrier_t;

//Creo una estructura para colocar el global tx order y la barrera
typedef struct global_spec_vars {
  volatile uint32_t tx_order; //Tiene que ser inicializado a 1
  uint8_t pad1[CACHE_BLOCK_SIZE-sizeof(uint32_t)];
  barrier_t barrier;
  uint8_t pad2[CACHE_BLOCK_SIZE-sizeof(barrier_t)];
} __attribute__ ((aligned (CACHE_BLOCK_SIZE))) g_spec_vars_t;

// Estructura que guarda las variables globales necesarias para las barreras
extern g_spec_vars_t g_specvars;


#endif
