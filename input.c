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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/time.h>

#include "input.h"

#define SV_IDLE 0
#define SV_SERVING 1
#define SV_RESCAN 2
#define SV_RESCANP 3
#define SV_STOP -1

InputDevices *openInputDevices(void) {
	// Generate default paths and call openInputDevicesPaths
	char *final_paths[MAX_INPUT_DEVICES];
	char paths[MAX_INPUT_DEVICES][INPUT_PATH_LEN];
	for(int dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		final_paths[dev] = paths[dev];
		sprintf(paths[dev], "%s%d", INPUT_PATH, dev);
	}
	return openInputDevicesPaths((const char **) final_paths, MAX_INPUT_DEVICES);
}

static void dummyCallback(const InputEvent *evt, const InputDevice *dev, void *data) {
#ifndef NDEBUG
	fprintf(stderr, "Event with type %hu from device %s\n", evt->type, dev ? dev->name : "loopback");
#endif
	return;
}

InputDevices *openInputDevicesPaths(const char **paths, size_t num_paths) {
	InputDevices *devices = malloc(sizeof(InputDevices));
	if(!devices) return NULL;

	if(pipe(devices->loopback) == -1) {
		devices->loopback[0] = -1;
		devices->loopback[1] = -1;
		devices->loopback[2] = errno;
	}

	for(size_t dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		// For each device:
		// 	set the name
		// 	if the name could be set (suficient memory and dev < num_paths) then open the file
		if(dev < num_paths) {
			size_t pathlen = strlen(paths[dev]) + 1;
			devices->devices[dev].name = malloc(pathlen);
			if(devices->devices[dev].name) {
				memcpy(devices->devices[dev].name, paths[dev], pathlen);
				devices->devices[dev].err = 0;
			} else {
				devices->devices[dev].fd = -1;
				devices->devices[dev].err = errno;
			}
		} else {
			devices->devices[dev].name = NULL;
			devices->devices[dev].fd = -1;
			devices->devices[dev].err = ENOENT;
		}
		if(!devices->devices[dev].err) {
			devices->devices[dev].fd = open(devices->devices[dev].name, O_RDONLY);
			if(devices->devices[dev].fd == -1) {
				devices->devices[dev].err = errno;
			}
		}
	}
	devices->serving = SV_IDLE;
	devices->callback = dummyCallback;
	devices->callback_data = NULL;
	// Success!
	return devices;
}

int closeInputDevices(InputDevices *devices) {
	if(devices->serving == SV_IDLE) {
		devices->serving = SV_STOP;
	} else if(devices->serving == SV_SERVING) {
		devices->serving = SV_STOP;
			InputEvent evt = {
			.type = EV_KEY,
			.code = KEY_EXIT,
		};
		loopbackEvent(devices, &evt);
		pthread_join(devices->serverthread, NULL);
	} else {
		return -1;
	}
	for(size_t dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		free(devices->devices[dev].name);
		if(devices->devices[dev].fd != -1) {
			close(devices->devices[dev].fd);
		}
	}
	if(devices->loopback[0] != -1) {
		close(devices->loopback[0]);
		close(devices->loopback[1]);
	}
	free(devices);
	return 0;
}

int rescanInputDevices(InputDevices *devices) {
	if((devices->serving != SV_IDLE) && (devices->serving != SV_RESCANP)) return -1;
	devices->serving = SV_RESCAN;
	for(size_t dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		if(devices->devices[dev].name) {
			if(devices->devices[dev].fd != -1) {
				close(devices->devices[dev].fd);
			}
			devices->devices[dev].fd = open(devices->devices[dev].name, O_RDONLY);
			devices->devices[dev].err = (devices->devices[dev].fd == -1) ? errno : 0;
		} else {
			if(devices->devices[dev].fd != -1) {
				devices->devices[dev].err = (close(devices->devices[dev].fd) == -1) ? errno : 0;
			}
			devices->devices[dev].fd == -1;
		}
	}
	if(devices->loopback[0] == -1) {
		devices->loopback[3] = (pipe(devices->loopback) == -1) ? errno : 0;
	}
	devices->serving = SV_IDLE;
}

int rescanInputDevicesPaths(InputDevices *devices, const char **paths, size_t num_paths) {
	if(devices->serving != SV_IDLE) return -1;
	devices->serving = SV_RESCANP;
	// Set names then call rescanInputDevices
	for(size_t dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		free(devices->devices[dev].name); // Freeing NULL is allowed
		if(dev < num_paths) {
			size_t pathlen = strlen(paths[dev]) + 1;
			devices->devices[dev].name = malloc(pathlen);
			if(devices->devices[dev].name) {
				memcpy(devices->devices[dev].name, paths[dev], pathlen);
			} else {
				devices->devices[dev].err = errno;
			}
		} else {
			devices->devices[dev].name = NULL;
		}
	}
	return rescanInputDevices(devices); // This should set devices->serving to SV_IDLE
}

static void serveEvents(InputDevices *devices, int wait) {
	fd_set dev_fds;
	struct timeval tv = {0};
	FD_ZERO(&dev_fds);
	int maxfd = -1;
	for(size_t dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		if(devices->devices[dev].fd != -1) {
			FD_SET(devices->devices[dev].fd, &dev_fds);
			maxfd = (devices->devices[dev].fd > maxfd) ? devices->devices[dev].fd : maxfd;
		}
	}
	if(devices->loopback[0] != -1) {
		FD_SET(devices->loopback[0], &dev_fds);
		maxfd = (devices->loopback[0] > maxfd) ? devices->loopback[0] : maxfd;
	}
	select(maxfd + 1, &dev_fds, NULL, NULL, wait ? NULL : &tv);
	InputEvent evt;
	ssize_t r;
	for(size_t dev = 0; dev < MAX_INPUT_DEVICES; dev++) {
		if(FD_ISSET(devices->devices[dev].fd, &dev_fds)) {
			r = read(devices->devices[dev].fd, &evt, sizeof(InputEvent));
			if(r == -1) {
				// Error
				devices->devices[dev].err = errno;
				close(devices->devices[dev].fd);
				devices->devices[dev].fd = -1;
			} else if(r == 0) {
				// EOF (device disconnected)
				devices->devices[dev].err = (close(devices->devices[dev].fd) == 0) ? 0 : errno;
				devices->devices[dev].fd = -1;
			} else if(r == sizeof(InputEvent)) {
				// Success!
				devices->callback(&evt, &devices->devices[dev], devices->callback_data);
			} else {
				// Short read - it isn't safe to continue in this case
				// Treat it as an error
				close(devices->devices[dev].fd);
				devices->devices[dev].fd = -1;
				devices->devices[dev].err = EIO; // An IO error (probably) didn't actually occur, but pretend that it did.
			}
		}
	}
	if(FD_ISSET(devices->loopback[0], &dev_fds)) {
		r = read(devices->loopback[0], &evt, sizeof(InputEvent));
		if(r == -1) {
			devices->loopback[2] = errno;
			close(devices->loopback[0]);
			close(devices->loopback[1]);
			devices->loopback[0] = -1;
			devices->loopback[1] = -1;
		} else if(r == sizeof(InputEvent)) {
			devices->callback(&evt, NULL, devices->callback_data);
		} else {
			close(devices->loopback[0]);
			close(devices->loopback[1]);
			devices->loopback[0] = -1;
			devices->loopback[1] = -1;
			devices->loopback[2] = EIO;
		}
	}
}

static void *eventServerLoop(InputDevices *devices) {
	while(devices->serving == SV_SERVING) {
		serveEvents(devices, 1);
	}
	return NULL;
}

void getInputEvents(InputDevices *devices) {
	if(devices->serving != SV_IDLE) return;
	serveEvents(devices, 0);
}

int serveInputEvents(InputDevices *devices) {
	if(devices->serving != SV_IDLE) return 1;
	devices->serving = SV_SERVING;
	if(pthread_create(&devices->serverthread, NULL, (void *(*)(void *)) eventServerLoop, devices) != 0) {
		devices->serving = SV_IDLE;
		return -1;
	}
	return 0;
}

void stopServingInputEvents(InputDevices *devices) {
	if(devices->serving != SV_SERVING) return;
	devices->serving = SV_STOP;
	InputEvent evt = {
		.type = EV_KEY,
		.code = KEY_EXIT,
	};
	loopbackEvent(devices, &evt);
	pthread_join(devices->serverthread, NULL);
	devices->serving = SV_IDLE;
}

static void *doLoopbackEvent(void *arg) {
	InputDevices *devices = ((void **)arg)[0];
	const InputEvent *evt = ((void **)arg)[1];
	ssize_t w = write(devices->loopback[1], evt, sizeof(InputEvent));
	if(w == -1) {
		devices->loopback[2] = errno;
	}
	free(((void **)arg)[1]);
	free(arg);
	return NULL;
}

void loopbackEvent(InputDevices *devices, const InputEvent *evt) {
	InputEvent *e = malloc(sizeof(InputEvent));
	void **arg = malloc(sizeof(void *) * 2);
	if(!(arg && e)) {
		free(e);
		free(arg);
		return;
	}
	*e = *evt;
	arg[0] = devices;
	arg[1] = e;
	pthread_t t;
	pthread_create(&t, NULL, doLoopbackEvent, arg);
	pthread_detach(t);
}
