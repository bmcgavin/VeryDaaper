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

bool connected = false;

int mm_stopEventsToIgnore = 0;
int exit_application = 0;
bool show_menu = false;

#define DEFAULT_DEBUG_CHANNEL "EVENTS"

void handleClick(int x, int y) {
	daap_host* host = getVisibleHost();
	if (host == NULL) {
		sendInitialCallback();
		return;
	}
	if (connected) {
		if (x > 900) {
			//Controls!
			DAAP_ClientHost_DatabaseItem* prev_song = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem));
			DAAP_ClientHost_DatabaseItem* next_song_item = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem));
			if (y <= 50) {
				//Play
				if (daap_host_get_playing(host) || daap_host_get_paused(host)) {
					daap_host_stop_song(host);
				}
				if (!daap_host_get_playing(host)) {
					daap_host_play_song(PLAYSOURCE_HOST, host, daap_host_get_selected_song(host)->id);
				}
			} else if (y <= 100) {
				//Pause/Resume
				if (daap_host_get_playing(host)) {
					daap_host_pause_song(host);
				} else {
					if (daap_host_get_paused(host)) {
						daap_host_resume_song(host);
					} else {
						daap_host_play_song(PLAYSOURCE_HOST, host, daap_host_get_selected_song(host)->id);
					}
				}
			} else if (y <= 150) {
				//Stop
				if (daap_host_get_playing(host) || daap_host_get_paused(host)) {
					daap_host_stop_song(host);
				}
			} else if (y <= 200) {
				//Prev song
				int prev_song_id = daap_host_prev_artist_album_songs(host, prev_song,
						get_songindex_by_id(host, daap_host_get_selected_song(host)->id), daap_host_get_selected_artist(host),
						daap_host_get_selected_album(host));
				if (prev_song_id != -1) {
					if (daap_host_get_selected_artist(host) != daap_host_get_artist_for_song(host, prev_song)) {
						daap_host_set_selected_artist(host, daap_host_get_artist_for_song(host, prev_song));
					}
					if (daap_host_get_selected_album(host) != daap_host_get_album_for_song_and_artist(host, prev_song, daap_host_get_selected_artist(host))) {
						daap_host_set_selected_album(host, daap_host_get_album_for_song_and_artist(host, prev_song, daap_host_get_selected_artist(host)));
					}
					daap_host_set_selected_song(host, prev_song);
				}
			} else if (y <= 250) {
				//Next song
				//Do we have a track?
				int next_song_id = daap_host_enum_artist_album_songs(host, next_song_item,
						get_songindex_by_id(host, daap_host_get_selected_song(host)->id), daap_host_get_selected_artist(host),
						daap_host_get_selected_album(host));
				//NO NEW TRACK
				if (next_song_id == -1) {
					album* next_album = get_new_album();
					//next_album = daap_host_enum_album(daap_host_get_selected_artist(host), daap_host_get_selected_album(host));
					next_album = daap_host_get_next_album(host, daap_host_get_selected_album(host));
					if (next_album == NULL) {
						//Next artist
						artist* next_artist = daap_host_get_next_artist(host, daap_host_get_selected_artist(host));
						if (next_artist == NULL) {
							//Uh
							return;
						}
						daap_host_set_selected_artist(host,next_artist);
						//daap_host_set_selected_album(host,daap_host_enum_album(next_artist, NULL));
						daap_host_set_selected_album(host,daap_host_get_next_album(host, daap_host_get_selected_album(host)));
						//Get first song_id for that artist
						daap_host_enum_artist_album_songs(host, next_song_item,
								-1, next_artist,
								daap_host_get_selected_album(host));
					} else if (next_album != daap_host_get_selected_album(host)) {
						daap_host_set_selected_album(host,next_album);
						//Get first song_id for that album
						daap_host_enum_artist_album_songs(host, next_song_item,
								-1, daap_host_get_selected_artist(host),
								next_album);
					}
				}
				daap_host_set_selected_song(host, next_song_item);
			} else if (y <= 300) {
				//Prev album
				album* prev_album = get_new_album();
				prev_album = daap_host_get_prev_album(host, daap_host_get_selected_album(host));
				int prev_song_id = -1;
				if (prev_album) {
					prev_song_id = daap_host_enum_artist_album_songs(host, prev_song,
							-1, daap_host_get_selected_artist(host),
							prev_album);
				} else {
					artist* prev_artist = daap_host_get_prev_artist(host, daap_host_get_selected_artist(host));
					if (prev_artist) {
						prev_song_id = daap_host_enum_artist_album_songs(host, prev_song,
							-1, prev_artist,
							daap_host_get_first_album_for_artist(host, prev_artist));
					} else {
						//Uh...
						TRACE("");
					}
				}
				if (prev_song_id != -1) {
					if (daap_host_get_selected_artist(host) != daap_host_get_artist_for_song(host, prev_song)) {
						daap_host_set_selected_artist(host, daap_host_get_artist_for_song(host, prev_song));
					}
					if (daap_host_get_selected_album(host) != daap_host_get_album_for_song_and_artist(host, prev_song, daap_host_get_selected_artist(host))) {
						daap_host_set_selected_album(host, daap_host_get_album_for_song_and_artist(host, prev_song, daap_host_get_selected_artist(host)));
					}
					daap_host_set_selected_song(host, prev_song);
				}
			} else if (y <= 350) {
				//Next album
				album* next_album = get_new_album();
				next_album = daap_host_get_next_album(host, daap_host_get_selected_album(host));
				if (next_album == NULL) {
					//Next artist
					artist* next_artist = daap_host_get_next_artist(host, daap_host_get_selected_artist(host));
					if (next_artist == NULL) {
						//Uh
						return;
					}
					daap_host_set_selected_artist(host,next_artist);
					daap_host_set_selected_album(host,daap_host_get_next_album(host, daap_host_get_selected_album(host)));
					//Get first song_id for that artist
					daap_host_enum_artist_album_songs(host, next_song_item,
							-1, next_artist,
							daap_host_get_selected_album(host));
					daap_host_set_selected_song(host, next_song_item);
				} else if (next_album != daap_host_get_selected_album(host)) {
					daap_host_set_selected_album(host,next_album);
					//Get first song_id for that album
					daap_host_enum_artist_album_songs(host, next_song_item,
							-1, daap_host_get_selected_artist(host),
							next_album);
					daap_host_set_selected_song(host, next_song_item);
				}
			} else if (y <= 400) {
				//Prev artist
				artist* prev_artist = daap_host_get_prev_artist(host, daap_host_get_selected_artist(host));
				int prev_song_id = -1;
				if (prev_artist) {
					prev_song_id = daap_host_enum_artist_album_songs(host, prev_song,
						-1, prev_artist,
						daap_host_get_first_album_for_artist(host, prev_artist));
				} else {
					//'Last' artist
					prev_artist = daap_host_get_first_artist(host);
					prev_song_id = daap_host_enum_artist_album_songs(host, prev_song,
						-1, prev_artist,
						daap_host_get_first_album_for_artist(host, prev_artist));
				}
				if (prev_song_id != -1) {
					if (daap_host_get_selected_artist(host) != daap_host_get_artist_for_song(host, prev_song)) {
						daap_host_set_selected_artist(host, daap_host_get_artist_for_song(host, prev_song));
					}
					if (daap_host_get_selected_album(host) != daap_host_get_album_for_song_and_artist(host, prev_song, daap_host_get_selected_artist(host))) {
						daap_host_set_selected_album(host, daap_host_get_album_for_song_and_artist(host, prev_song, daap_host_get_selected_artist(host)));
					}
					//Not playing, but update display
					daap_host_set_selected_song(host, prev_song);
				}
			} else if (y <= 450) {
				//Next artist
				artist* next_artist = daap_host_get_next_artist(host, daap_host_get_selected_artist(host));
				if (next_artist == NULL) {
					//Uh...
					return;
				}
				if (next_artist != daap_host_get_selected_artist(host)) {
					daap_host_set_selected_artist(host,next_artist);
					daap_host_set_selected_album(host,daap_host_get_next_album(host, daap_host_get_selected_album(host)));
					//Get first song_id for that artist
					daap_host_enum_artist_album_songs(host, next_song_item,
							get_songindex_by_id(host, daap_host_get_selected_song(host)->id), next_artist,
							daap_host_get_selected_album(host));
					daap_host_set_selected_song(host, next_song_item);
				}
			} else if (y <= 500) {
				//Random
				daap_host_toggle_playsource_random(host);
			} else if (y <= 550) {
				//M
				daap_host_jump_to_letter(host, "m");
			}
			return;
		} else if (y <= 550) {
			if (x <= 200) {
				daap_host_seek_percent(host, 0);
			} else if (x <= 400) {
				daap_host_seek_percent(host, 50);
			} else if (x <= 600) {
				daap_host_seek_percent(host, 99);
			}
		}
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
	daap_host* host = getVisibleHost();
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
	case MMRENDERER_STATUS_UPDATE:
		if (host != NULL) {
			daap_host_set_position(host, mmrenderer_event_get_position(event));
		}
		debugMsg(mmrenderer_event_get_position(event), 10);
		debugMsg(get_current_song_length(NULL), 11);
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
        	exit_application = 1;
        }
        break;
    case NAVIGATOR_SWIPE_DOWN:
    	show_menu = !show_menu;
        break;
    case NAVIGATOR_EXIT:
        exit_application = 1;
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
                	exit_application = 1;
                    break;
                } else if (code == NAVIGATOR_WINDOW_ACTIVE) {
                    break;
                }
            }
        }
        break;
    }
}
