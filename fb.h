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

#ifndef FB_H
#define FB_H 1

#include <stdint.h>
#include <stddef.h>

typedef struct pixel {
	uint8_t r, g, b, a;
} Pixel;

typedef struct frameBufferDevice {
	int xres, yres;
	size_t numPixels;
	size_t frameSize;
	Pixel *nextFrame;
	Pixel *lastFrame;
	size_t directSize;
	size_t lineLen;
	void *direct;
	void *swapBuffer;
	int fd;
	int modeset;
	void (*close)(struct frameBufferDevice *fbd);
	void (*swap)(struct frameBufferDevice *fbd);
	uint32_t (*formatPx)(Pixel px);
} FrameBufferDevice;

FrameBufferDevice *openFBDev(const char *path);

#endif /* FB_H */
