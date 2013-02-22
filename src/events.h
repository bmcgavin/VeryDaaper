/*
 * events.h
 *
 *  Created on: 2 Feb 2013
 *      Author: Rich
 */

#ifndef EVENTS_H_
#define EVENTS_H_
#include <bps/event.h>
#include <stdbool.h>

int mm_stopEventsToIgnore;
void handleClick(int x, int y);
int resize(bps_event_t *event);
void handleScreenEvent(bps_event_t *event);
void handleNavigatorEvent(bps_event_t *event);
static void handle_events();
void handleMMRendererEvent(bps_event_t* event);

int exit_application;
bool show_menu;

#endif /* EVENTS_H_ */
