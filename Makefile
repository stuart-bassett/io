# Makefile for libio

# Copyright (C) 2016 Stuart Bassett
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

.PHONY: all clean example

all: libio.so libio.a

libio.so: fb.o input.o
	$(CC) -shared -fPIC $(CFLAGS) $(LDFLAGS) -pthread fb.o input.o $(LDLIBS) -o libio.so

libio.a: fb.o input.o
	$(AR) sq libio.a fb.o input.o

fb.o:
	$(CC) -c -fPIC --std=gnu99 $(CFLAGS) fb.c -o fb.o

input.o:
	$(CC) -c -fPIC --std=gnu99 $(CFLAGS) input.c -o input.o

example: libio.a
	$(CC) --std=gnu99 $(CFLAGS) $(LDFLAGS) example.c libio.a -pthread $(LDLIBS) -o example

clean:
	$(RM) libio.so libio.a fb.o input.o example
