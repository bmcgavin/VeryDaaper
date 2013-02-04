/*
 * events.h
 *
 *  Created on: 2 Feb 2013
 *      Author: Rich
 */

#ifndef EVENTS_H_
#define EVENTS_H_


void handleClick(int x, int y);
int resize(bps_event_t *event);
void handleScreenEvent(bps_event_t *event);
static void handleNavigatorEvent(bps_event_t *event);
static void handle_events();

#endif /* EVENTS_H_ */
