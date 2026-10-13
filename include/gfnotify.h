/*
 * gfnotify.h -- meldingen via een notificatieserver.
 *
 * Herald en Ranchero (OS 3) en Ringhio (OS 4) tonen een korte pop-up die
 * geen focus steelt. Ze spreken dezelfde ARexx-taal, dus een enkel
 * bericht naar hun poort volstaat; RexxMast hoeft daarvoor niet te draaien.
 * Draait er geen van drieen, dan gebeurt er niets.
 */

#ifndef GFNOTIFY_H
#define GFNOTIFY_H

#include <exec/types.h>

void gf_notify_open(void);
void gf_notify_close(void);

/* Stuurt een melding naar de eerste server die er is. 'failed' geeft de
   melding een hogere prioriteit, zodat hij opvalt. */
void gf_notify(BOOL failed, const char *title, const char *text);

#endif /* GFNOTIFY_H */
