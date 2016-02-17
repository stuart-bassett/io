/**
 *	Copyright (C) 2016 Stuart Bassett
 *
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// This program is an example: it probably has (many) bugs

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include "fb.h"
#include "input.h"

#ifndef DEFAULT_FB
#define DEFAULT_FB "/dev/fb0"
#endif

// Used to signal that the program should stop
volatile int stop;

void callback(const InputEvent *evt, const InputDevice *dev, void *data) {
	// Stop when escape is pressed
	if((evt->type == EV_KEY) && (evt->code == KEY_ESC)) stop = 1;
}

int main(void) {
	stop = 0;

// Open a framebuffer
	const char *fb_path = getenv("FB");
	FrameBufferDevice *fb = openFBDev(fb_path ? fb_path : DEFAULT_FB);
	if(!fb) return 1;

// Disable echoing if standard input is a tty (which it should be)
	struct termios original_ts;
	if(isatty(0)) {
		struct termios ts;
		tcgetattr(0, &ts);
		original_ts = ts;
		ts.c_lflag &= ~(ECHO|ECHOCTL);
		tcsetattr(0, TCSANOW, &ts);
	}

// Open input devices
	InputDevices *ids = openInputDevices();
	ids->callback = callback;
	serveInputEvents(ids);

// Make the screen red
	for(size_t p = 0; p < fb->numPixels; p++) {
		fb->nextFrame[p] = (Pixel) {
			.r = 0xff,
			.g = 0x00,
			.b = 0x00,
			.a = 0xff,
		};
		fb->lastFrame[p] = fb->nextFrame[p];
	}

	while(!stop) {
		fb->swap(fb);
		usleep(1000);
	}

	fb->close(fb);

	stopServingInputEvents(ids);
	closeInputDevices(ids);

// Restore tty settings
	if(isatty(0)) {
		tcsetattr(0, TCSANOW, &original_ts);
	}
	return 0;
}
