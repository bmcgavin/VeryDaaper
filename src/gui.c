/*
 * gui.c
 *
 *  Created on: 11 Feb 2013
 *      Author: Rich
 */

#include <GLES2/gl2.h>

#define GUI_ITEMS_COUNT 6

int getGUIIdx() {
	return GUI_ITEMS_COUNT;
}

static char* messages[GUI_ITEMS_COUNT] = {
	"Play",
	"Pause/Resume",
	"Stop",
	"Prev Song",
	"Prev Album",
	"Prev Artist"
};

static float positions[GUI_ITEMS_COUNT * 2] = {
	900.0f, 550.0f,
	900.0f, 500.0f,
	900.0f, 450.0f,
	900.0f, 400.0f,
	900.0f, 350.0f,
	900.0f, 300.0f
};

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
