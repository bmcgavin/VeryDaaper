/*
 * gui.c
 *
 *  Created on: 11 Feb 2013
 *      Author: Rich
 */

#include <GLES2/gl2.h>
#include <strings.h>

#define GUI_ITEMS_COUNT 14
#define SHUFFLE_IDX 9

int getGUIIdx() {
	return GUI_ITEMS_COUNT;
}

static char* messages[GUI_ITEMS_COUNT] = {
	"Play",
	"Pause/Resume",
	"Stop",
	"Prev Song",
	"Next Song",
	"Prev Album",
	"Next Album",
	"Prev Artist",
	"Next Artist",
	"Shuffle  ",
	"Goto M",

	"0%",
	"50%",
	"100%"
};

static float positions[GUI_ITEMS_COUNT * 2] = {
	900.0f, 550.0f,
	900.0f, 500.0f,
	900.0f, 450.0f,
	900.0f, 400.0f,
	900.0f, 350.0f,
	900.0f, 300.0f,
	900.0f, 250.0f,
	900.0f, 200.0f,
	900.0f, 150.0f,
	900.0f, 100.0f,
	900.0f,  50.0f,

	  0.0f,	 50.0f,
	200.0f,  50.0f,
	400.0f,	 50.0f
};

void toggleShuffle() {
	if (strncasecmp(messages[SHUFFLE_IDX], "Shuffle  ", 9) == 0) {
		messages[SHUFFLE_IDX] = "Shuffle X";
	} else {
		messages[SHUFFLE_IDX] = "Shuffle  ";
	}
}

char* getGUIMsg(int idx) {
	return (char*)messages[idx];
}

float getGUIPosX(int idx) {
	int new_idx = idx * 2;
	return positions[new_idx];
}

float getGUIPosY(int idx) {
	int new_idx = idx * 2;
	return positions[new_idx + 1];
}
