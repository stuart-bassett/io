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

#ifndef INPUT_H
#define INPUT_H 1

#include <linux/input.h>
#include <stddef.h>
#include <pthread.h>

typedef struct input_event InputEvent;

typedef struct input_device {
	char *name;
	int fd;
	int err;
} InputDevice;

// Maximum number of input devices
#define MAX_INPUT_DEVICES 32

typedef struct input_devices {
	volatile int serving;
	pthread_t serverthread;
	InputDevice devices[MAX_INPUT_DEVICES];
	int loopback[2]; // loopback[0] is the read end, loopback[1] is the write end, loopback[2] is an error code
	void (*callback)(const InputEvent *evt, const InputDevice *dev, void *data);
	void *callback_data;
} InputDevices;

// Default path of input devices: device number is added at the end
#define INPUT_PATH "/dev/input/event"
#define INPUT_PATH_LEN 19

// Open all input devices: paths variant allows defining custom paths
// It is safe to free paths after calling this function (paths are copied)
InputDevices *openInputDevices(void);
InputDevices *openInputDevicesPaths(const char **paths, size_t num_paths);

// Will also free devices
// If this returns an error (-1) then devices will remain unmodified
int closeInputDevices(InputDevices *devices);

// Like openInputDevices but re-uses an existing InputDevices structure: useful if a new device is added
// Paths variant allows changing the paths: otherwise uses the paths previously specified
// return -1 if events are currently being served.
int rescanInputDevices(InputDevices *devices);
int rescanInputDevicesPaths(InputDevices *devices, const char **paths, size_t num_paths);

// The registered callback for devices is called for each available event
// If you use this method be sure to call it regularly
void getInputEvents(InputDevices *devices);

// Spawns a new thread that calls the registered callback when events arrive
// This is the preferred method of obtaining input
int serveInputEvents(InputDevices *devices);

// IMPORTANT: Don't forget to set callback before calling get/serveInputEvents

// You probably shouldn't call this without calling serveInputEvents first
void stopServingInputEvents(InputDevices *devices);

// Send an event to ourself
void loopbackEvent(InputDevices *devices, const InputEvent *evt);

#endif /* INPUT_H */
