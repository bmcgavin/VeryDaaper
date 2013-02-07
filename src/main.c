/*
 * Copyright (c) 2011-2012 Research In Motion Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <screen/screen.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <bps/navigator.h>
#include <bps/screen.h>
#include <bps/bps.h>
#include <bps/event.h>
#include <bps/mmrenderer.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <math.h>

#include "bbutil.h"

#include "libopendaap-0.4.0/debug/debug.h"
#include "daapfunc.h"
#include "events.h"

#define DEFAULT_DEBUG_CHANNEL "main"

#if ! defined(DEFAULT_AUDIO_OUT)
    #define DEFAULT_AUDIO_OUT "audio:default"
#endif
#if ! defined(DEFAULT_CONTEXT_NAME)
	#define DEFAULT_CONTEXT_NAME "context"
#endif

static screen_context_t screen_cxt;

static font_t* font;
static float pos_x, pos_y;
float text_width, text_height;
mmrenderer_monitor_t* mmr_monitor = NULL;
intptr_t userdata = NULL;

int initialize() {

    //Query width and height of the window surface created by utility code
    EGLint surface_width, surface_height;

    eglQuerySurface(egl_disp, egl_surf, EGL_WIDTH, &surface_width);
    eglQuerySurface(egl_disp, egl_surf, EGL_HEIGHT, &surface_height);


    int dpi = bbutil_calculate_dpi(screen_cxt);
    float stretch_factor = (float)surface_width / (float)100.0f;
    int point_size = (int)(1.0f * stretch_factor / ((float)dpi / 170.0f ));
    font = bbutil_load_font("/usr/fonts/font_repository/monotype/arial.ttf", point_size, dpi);
    bbutil_measure_text(font, "Hello world", &text_width, &text_height);
    pos_x = 0;
    pos_y = 550;

    debugInit();
    pos_y -= text_height;

    return EXIT_SUCCESS;
}

void render() {
	glClear(GL_COLOR_BUFFER_BIT);
	int i;
	pos_y = 550;
	//int x = 0;
	int x = getDebugIdx();
	for (i = 0; i <= x; i++) {
		bbutil_render_text(font, getRPJDebugMsg(i), pos_x, pos_y, 0.35f, 0.35f, 0.35f, 1.0f);
		pos_y -= (text_height + 2);
	}
    bbutil_swap();
}


int main(int argc, char *argv[]) {
	int rc;
    int exit_application = 0;

    //Create a screen context that will be used to create an EGL surface to to receive libscreen events
    screen_create_context(&screen_cxt, 0);

    //Initialize BPS library
    bps_initialize();

    //Use utility code to initialize EGL for rendering with GL ES 2.0
    if (EXIT_SUCCESS != bbutil_init_egl(screen_cxt)) {
        fprintf(stderr, "bbutil_init_egl failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_cxt);
        return 0;
    }

    //Initialize application logic
    if (EXIT_SUCCESS != initialize()) {
        fprintf(stderr, "initialize failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_cxt);
        bps_shutdown();
        return 0;
    }

    //Signal BPS library that navigator and screen events will be requested
    if (BPS_SUCCESS != screen_request_events(screen_cxt)) {
        fprintf(stderr, "screen_request_events failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_cxt);
        bps_shutdown();
        return 0;
    }

    if (BPS_SUCCESS != navigator_request_events(0)) {
        fprintf(stderr, "navigator_request_events failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_cxt);
        bps_shutdown();
        return 0;
    }

    //Signal BPS library that navigator orientation is not to be locked
    if (BPS_SUCCESS != navigator_rotation_lock(false)) {
        fprintf(stderr, "navigator_rotation_lock failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_cxt);
        bps_shutdown();
        return 0;
    }

    conn = mmr_connect(NULL);
	ctxt = mmr_context_create(conn, DEFAULT_CONTEXT_NAME, 0, mode);
	int audio_oid = mmr_output_attach(ctxt, DEFAULT_AUDIO_OUT, "audio");

	debugMsg("Sending initial callback", -1);
	sendInitialCallback();

	userdata = (intptr_t)malloc(sizeof(intptr_t));
	mmr_monitor = NULL;
	mmr_monitor = mmrenderer_request_events(DEFAULT_CONTEXT_NAME, 0, userdata);

    while (!exit_application) {
        //Request and process all available BPS events
        bps_event_t *event = NULL;

        for(;;) {
            rc = bps_get_event(&event, 0);
            assert(rc == BPS_SUCCESS);

            if (event) {
                int domain = bps_event_get_domain(event);

                if (domain == screen_get_domain()) {
                    handleScreenEvent(event);
                } else if ((domain == navigator_get_domain())
                        && (NAVIGATOR_EXIT == bps_event_get_code(event))) {
                    exit_application = 1;
                } else if ((domain == mmrenderer_get_domain())) {
                	handleMMRendererEvent(event);
                }
            } else {
                break;
            }
        }
        render();
    }

    //Stop requesting events from libscreen
    screen_stop_events(screen_cxt);

    //Shut down BPS library for this process
    bps_shutdown();

    //Use utility code to terminate EGL setup
    bbutil_terminate();

    //Destroy libscreen context
    screen_destroy_context(screen_cxt);
    return 0;
}
