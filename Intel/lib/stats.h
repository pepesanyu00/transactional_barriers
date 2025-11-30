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

// MACROS DEFINIDAS EN BARRIERS.H
//#define CACHE_BLOCK_SIZE 64
//#define LOCK_TAKEN 0xFF



// Estructura para guardar las estadísticas
struct Stats
{
  //Las estadísticas se actualizan fuera de transacción, pero se reservan con malloc (¿podrían caer cerca de datos compartidos accedidos dentro de transacción y compartir línea de caché?)
  volatile char pad1[CACHE_BLOCK_SIZE];  //Pads para que no haya false sharing (que no coincidan xabortCount de un thread con retryFCount de otro en un bloque cache)
  unsigned long int xabortCount;         //Número total de abortos
  unsigned long int explicitAborts;      //Número de llamadas a XABORT en el código
  unsigned long int explicitAbortsSubs;  //Número de abortos explícitos por subscripción de lock
  unsigned long int retryAborts;         //Abortos para los que el hardware piensa que debemos reintentar
  unsigned long int retryConflictAborts; //Abortos para los que el hardware piensa que debemos reintentar
  unsigned long int retryCapacityAborts; //Abortos para los que el hardware piensa que debemos reintentar
  unsigned long int conflictAborts;      //Abortos por conflicto
  unsigned long int capacityAborts;      //Abortos por capacidad
  unsigned long int debugAborts;         //Abortos por breakpoint de debugger
  unsigned long int nestedAborts;        //Abortos dentro de una transacción anidada
  unsigned long int eaxzeroAborts;       //Abortos con eax = 0
  unsigned long int xcommitCount;        //Número de commits
  unsigned long int fallbackCount;       //Número de fallbacks
  unsigned long int retryCCount;         //Número de retries de las que commitan
  unsigned long int retryFCount;         //Número de retries de las que entran en fallback
  unsigned long int xbeginCount;         //Número de transacciones que se han abierto
  volatile char pad2[CACHE_BLOCK_SIZE];
};

extern struct Stats **stats;


//Funciones para el fichero de estadísticas
int statsFileInit(int argc, char **argv, long thCount, long xCount);
int dumpStats(double time);

// Funciones de profile de estadísticas (hechas inline para mejorar el rendimiento)


// Función para determinar el tipo de aborto
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
    //Todos los bits a cero (puede ocurrir por una llamada a CPUID u otro cosa)
    //Véase Section 8.3.5 RTM Abort Status Definition del Intel Architecture
    //Instruction Set Extensions Programming Reference (2012))
    stats[thread][xid].eaxzeroAborts++;
  }
  return 0;
}

// Función para indicar un commit
inline void profileCommit(long thread, long xid, long retries)
{
  stats[thread][xid].xcommitCount++;
  stats[thread][xid].retryCCount += retries;
}

// Funcion para indicar un fallback
inline void profileFallback(long thread, long xid, long retries)
{
  stats[thread][xid].fallbackCount++;
  stats[thread][xid].retryFCount += retries;
}
#endif