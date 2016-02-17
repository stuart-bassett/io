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

#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include "fb.h"

#define TRY(predicate) if(!(predicate)) goto fail

static int activevt(void) {
	struct vt_stat vts;
	TRY(ioctl(0, VT_GETSTATE, &vts) == 0);
	return vts.v_active;
fail:
	perror("activevt");
	return -1;
}

static int setGraphics(void) {
	TRY(ioctl(0, KDSETMODE, KD_GRAPHICS) == 0);
	return 0;
fail:
	perror("setGraphics");
	return 1;
}

void closeFBDev(FrameBufferDevice *fbd) {
	assert(fbd);
	if((fbd->direct != MAP_FAILED) && (munmap(fbd->direct, fbd->directSize) == -1)) perror("closeFBDev");
	if(fbd->lastFrame) free(fbd->lastFrame);
	if(fbd->nextFrame) free(fbd->nextFrame);
	if(fbd->swapBuffer) free(fbd->swapBuffer);
	if((fbd->fd != -1) && (close(fbd->fd) == -1)) perror("closeFBDev");
	if((fbd->modeset) && ioctl(0, KDSETMODE, KD_TEXT)) perror("closeFBDev");
	free(fbd);
}

void swapFBDev(FrameBufferDevice *fbd) {
	assert(fbd);
	for(size_t line = 0; line < fbd->yres; line++) {
		uint32_t *l = (uint32_t *) (((char *)fbd->swapBuffer) + (line * fbd->lineLen));
		for(size_t px = 0; px < fbd->xres; px++) {
			l[px] = fbd->formatPx(fbd->nextFrame[line * fbd->xres + px]);
		}
	}
	memcpy(fbd->direct, fbd->swapBuffer, fbd->directSize);
	Pixel *tmp = fbd->nextFrame;
	fbd->nextFrame = fbd->lastFrame;
	fbd->lastFrame = tmp;
}

void debugFB(const struct fb_var_screeninfo vinfo, const struct fb_fix_screeninfo finfo) {
	printf("FINFO:\n");
	printf("id: %s\n", finfo.id);
	printf("smem_start: %lu\n", finfo.smem_start);
	printf("smem_len: %u\n", finfo.smem_len);
	printf("type: %u\n", finfo.type);
	printf("type_aux: %u\n", finfo.type_aux);
	printf("visual: %u\n", finfo.visual);
	printf("xpanstep: %hu\n", finfo.xpanstep);
	printf("ypanstep: %hu\n", finfo.ypanstep);
	printf("ywrapstep: %hu\n", finfo.ywrapstep);
	printf("line_length: %u\n", finfo.line_length);
	printf("mmio_start: %lu\n", finfo.mmio_start);
	printf("mmio_len: %u\n", finfo.mmio_len);
	printf("accel: %u\n", finfo.accel);
	printf("capabilities: %hu\n", finfo.capabilities);
	printf("\n");
	printf("VINFO:\n");
	printf("xres: %u\n", vinfo.xres);
	printf("yres: %u\n", vinfo.yres);
	printf("xres_virtual: %u\n", vinfo.xres_virtual);
	printf("yres_virtual: %u\n", vinfo.yres_virtual);
	printf("xoffset: %u\n", vinfo.xoffset);
	printf("yoffset: %u\n", vinfo.yoffset);
	printf("bits_per_pixel: %u\n", vinfo.bits_per_pixel);
	printf("grayscale: %u\n", vinfo.grayscale);
	printf("red: offset: %u, length %u, msb_right %u\n", vinfo.red.offset, vinfo.red.length, vinfo.red.msb_right);
	printf("green: offset: %u, length %u, msb_right %u\n", vinfo.green.offset, vinfo.green.length, vinfo.green.msb_right);
	printf("blue: offset: %u, length %u, msb_right %u\n", vinfo.blue.offset, vinfo.blue.length, vinfo.blue.msb_right);
	printf("transp: offset: %u, length %u, msb_right %u\n", vinfo.transp.offset, vinfo.transp.length, vinfo.transp.msb_right);
	printf("nonstd: %u\n", vinfo.nonstd);
	printf("activate: %u\n", vinfo.activate);
	printf("\n");
}

uint32_t formatPxARGB(Pixel px) {
	uint32_t out = 0;
	out |= ((uint32_t)px.r) << 16;
	out |= ((uint32_t)px.g) << 8;
	out |= ((uint32_t)px.b);
	out |= ((uint32_t)px.a) << 24;
	return out;
}

FrameBufferDevice *openFBDev(const char *path) {
	FrameBufferDevice *fbd = NULL;
	TRY(fbd = malloc(sizeof(FrameBufferDevice)));
	*fbd = (FrameBufferDevice) {
		.nextFrame = NULL,
		.lastFrame = NULL,
		.direct = MAP_FAILED,
		.fd = -1,
		.close = closeFBDev,
		.swap = swapFBDev,
		.modeset = 0,
		.formatPx = formatPxARGB, //TODO: Detect proper pixel placement from vinfo, my screen is using ARGB, so that's what's implemented
	};
	TRY((fbd->modeset = (!setGraphics())) == 1);
	TRY((fbd->fd = open(path, O_RDWR)) != -1);
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	TRY(ioctl(fbd->fd, FBIOGET_VSCREENINFO, &vinfo) == 0);
	TRY(ioctl(fbd->fd, FBIOGET_FSCREENINFO, &finfo) == 0);
//	debugFB(vinfo, finfo);
	fbd->xres = vinfo.xres;
	fbd->yres = vinfo.yres;
	fbd->numPixels = fbd->xres * fbd->yres;
	fbd->frameSize = fbd->numPixels * sizeof(Pixel);
	fbd->directSize = finfo.smem_len;
	fbd->lineLen = finfo.line_length;
	TRY(fbd->nextFrame = calloc(1, fbd->frameSize));
	TRY(fbd->lastFrame = calloc(1, fbd->frameSize));
	TRY(fbd->swapBuffer = malloc(fbd->directSize));
	TRY((fbd->direct = mmap(NULL, fbd->directSize, PROT_WRITE, MAP_SHARED, fbd->fd, 0)) != MAP_FAILED);
	return fbd;
fail:
	perror("openFBDev");
	if(fbd) fbd->close(fbd);
	return NULL;
}
