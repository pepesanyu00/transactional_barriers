/******************************************************************************
 * Authors: Jose Sanchez-Yun (pepesy00@uma.es)
 *          Eladio Gutierrez (eladio@uma.es)
 *          Ricardo Quislant (quislant@uma.es)
 *          Oscar Plata (oplata@uma.es)
 *
 * University: Dept. of Computer Architecture, University of Malaga,
 *             Bulevar Louis Pasteur, 35, Malaga, 29071, Andalusia, Spain
 ******************************************************************************/
#include "barriers.h"


unsigned long barrierCounter = 0;

struct TicketLock g_ticketlock = {.ticket = 0, .turn = 1};
fback_lock_t g_fallback_lock = {.ticket = 0, .turn = 1};
g_spec_vars_t g_specvars = {.tx_order = 1};