/*
 * events.c
 *
 *  Created on: 2 Feb 2013
 *      Author: Rich
 */


#include <bps/navigator.h>
#include <bps/screen.h>
#include <bps/bps.h>
#include <bps/event.h>
#include <stdlib.h>
#include <bps/dialog.h>
#include <bps/mmrenderer.h>
#include <mm/renderer.h>
#include <mm/renderer/types.h>

#include "daapfunc.h"
#include "libopendaap-0.4.0/debug/debug.h"

bool shutdown = false;
bool menu_show_animation = false;
bool menu_hide_animation = false;

bool connected = false;

int mm_stopEventsToIgnore = 0;

void handleClick(int x, int y) {
	daap_host* host = getVisibleHost();
	if (host == NULL) {
		sendInitialCallback();
		return;
	}
	if (connected) {
		if (y < 200) {
			//Album
			album* next_album = get_new_album();
			next_album = daap_host_enum_album(daap_host_get_selected_artist(host),
					daap_host_get_selected_album(host));
			if (next_album == NULL) {
				//Next artist
				artist* next_artist = daap_host_get_next_artist(host, daap_host_get_selected_artist(host));
				if (next_artist == NULL) {
					//Uh
					return;
				}
				daap_host_set_selected_artist(host,next_artist);
				daap_host_set_selected_album(host,daap_host_enum_album(next_artist, NULL));
				//Get first song_id for that artist
				DAAP_ClientHost_DatabaseItem* next_song = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem));
				daap_host_enum_artist_album_songs(host, next_song,
						-1, next_artist,
						daap_host_get_selected_album(host));
				stop_type = STOP_NEWSONG;
				daap_host_play_song(PLAYSOURCE_HOST, host, next_song->id);
			} else if (next_album != daap_host_get_selected_album(host)) {
				daap_host_set_selected_album(host,next_album);
				//Get first song_id for that album
				DAAP_ClientHost_DatabaseItem* next_song = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem));
				daap_host_enum_artist_album_songs(host, next_song,
						-1, daap_host_get_selected_artist(host),
						next_album);
				stop_type = STOP_NEWSONG;
				daap_host_play_song(PLAYSOURCE_HOST, host, next_song->id);
			}
		} else if (y < 400) {
			//Artist
			//artist* next_artist = get_new_artist();
			artist* next_artist = daap_host_get_next_artist(host, daap_host_get_selected_artist(host));
			if (next_artist == NULL) {
				//Uh...
				return;
			}
			if (next_artist != daap_host_get_selected_artist(host)) {
				daap_host_set_selected_artist(host,next_artist);
				daap_host_set_selected_album(host,daap_host_enum_album(next_artist, NULL));
				//Get first song_id for that artist
				DAAP_ClientHost_DatabaseItem* next_song = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem));
				daap_host_enum_artist_album_songs(host, next_song,
						-1, next_artist,
						daap_host_get_selected_album(host));
				stop_type = STOP_NEWSONG;
				daap_host_play_song(PLAYSOURCE_HOST, host, next_song->id);
			}
		} else {
			//Do we have a track?

			//Track, do nothing
			//daap_audiocb_finished();
		}
		//Play next song from selected playlist
		//This triggers two stop events, so ignore another one.
		//mm_stopEventsToIgnore++;
		daap_audiocb_finished();
	} else {
		initial_connect(host);
		connected = true;
	}
}


void handleScreenEvent(bps_event_t *event) {
    int screen_val, buttons;
    int pair[2];

    static bool mouse_pressed = false;

    screen_event_t screen_event = screen_event_get_event(event);

    //Query type of screen event and its location on the screen
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE,
            &screen_val);
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION,
            pair);

    //There is a difference between touch screen events and mouse events
    if (screen_val == SCREEN_EVENT_MTOUCH_RELEASE) {
        //Handle touch screen event
        handleClick(pair[0], pair[1]);

    } else if (screen_val == SCREEN_EVENT_POINTER) {
        //This is a mouse move event, it is applicable to a device with a usb mouse or simulator
        screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS,
                &buttons);

        if (buttons == SCREEN_LEFT_MOUSE_BUTTON) {
            //Left mouse button is pressed
            mouse_pressed = true;
        } else {
            if (mouse_pressed) {
                //Left mouse button was released, handle left click
                handleClick(pair[0], pair[1]);
                mouse_pressed = false;
            }
        }
    }
}



void handleMMRendererEvent(bps_event_t* event) {
	switch (bps_event_get_code(event)) {
	case MMRENDERER_STATE_CHANGE:
		//const int userdata = mmrenderer_event_get_userdata(event);
		switch(mmrenderer_event_get_state(event)) {
		case MMR_STOPPED:
			//NEXT!
			if (mm_stopEventsToIgnore <= 0) {
				//debugMsg("STOPPING", -1);
				daap_audiocb_finished();
			}
			mm_stopEventsToIgnore--;
			break;
		case MMR_PLAYING:
			//debugMsg("PLAYING", -1);
			break;
		default:
			//debugMsg("UNKNOWN MMR STATE", -1);
			break;
		}
		break;
	default:
		//debugMsg("UNKNOWN MMR EVENT", -1);
		break;
	}
}

int resize(bps_event_t *event) {
	return 0;
}


void handleNavigatorEvent(bps_event_t *event) {
    switch (bps_event_get_code(event)) {
    case NAVIGATOR_ORIENTATION_CHECK:
        //Signal navigator that we intend to resize
        navigator_orientation_check_response(event, true);
        break;
    case NAVIGATOR_ORIENTATION:
        if (EXIT_FAILURE == resize(event)) {
            shutdown = true;
        }
        break;
    case NAVIGATOR_SWIPE_DOWN:
        menu_show_animation = true;
        menu_hide_animation = false;
        break;
    case NAVIGATOR_EXIT:
        shutdown = true;
        break;
    case NAVIGATOR_WINDOW_INACTIVE:
        //Wait for NAVIGATOR_WINDOW_ACTIVE event
        for (;;) {
            if (BPS_SUCCESS != bps_get_event(&event, -1)) {
                fprintf(stderr, "bps_get_event failed\n");
                break;
            }

            if (event && (bps_event_get_domain(event) == navigator_get_domain())) {
                int code = bps_event_get_code(event);
                if (code == NAVIGATOR_EXIT) {
                    shutdown = true;
                    break;
                } else if (code == NAVIGATOR_WINDOW_ACTIVE) {
                    break;
                }
            }
        }
        break;
    }
}
