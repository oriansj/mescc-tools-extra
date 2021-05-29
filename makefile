## Copyright (C) 2017 Jeremiah Orians
## This file is part of mescc-tools.
##
## mescc-tools is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## mescc-tools is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with mescc-tools.  If not, see <http://www.gnu.org/licenses/>.

# Prevent rebuilding
VPATH = bin:test/results
PACKAGE = mescc-tools

all: get_machine kaem catm cp chmod ungz untar sha256sum
.NOTPARALLEL:
CC=gcc
CFLAGS:=$(CFLAGS) -D_GNU_SOURCE -std=c99 -ggdb -fno-common

# Building with GCC
get_machine: get_machine.c M2libc/bootstrappable.c | bin
	$(CC) $(CFLAGS) get_machine.c M2libc/bootstrappable.c -o bin/get_machine

kaem: Kaem/kaem.c Kaem/variable.c Kaem/kaem_globals.c M2libc/bootstrappable.c | bin
	cd Kaem && make kaem

catm: catm.c | bin
	$(CC) $(CFLAGS) catm.c -o bin/catm

cp: cp.c | bin
	$(CC) $(CFLAGS) cp.c M2libc/bootstrappable.c -o bin/cp

chmod: chmod.c | bin
	$(CC) $(CFLAGS) chmod.c M2libc/bootstrappable.c -o bin/chmod

ungz: ungz.c | bin
	$(CC) $(CFLAGS) ungz.c M2libc/bootstrappable.c -o bin/ungz

untar: untar.c | bin
	$(CC) $(CFLAGS) untar.c M2libc/bootstrappable.c -o bin/untar

sha256sum: sha256sum.c | bin
	$(CC) $(CFLAGS) sha256sum.c M2libc/bootstrappable.c -o bin/sha256sum

# Clean up after ourselves
.PHONY: clean
clean:
	rm -rf bin/
	cd Kaem && make clean

# A cleanup option we probably don't need
.PHONY: clean-hard
clean-hard: clean
	git reset --hard
	git clean -fd

# Directories
bin:
	mkdir -p bin

# tests
test:
	./test.sh


DESTDIR:=
PREFIX:=/usr/local
bindir:=$(DESTDIR)$(PREFIX)/bin
.PHONY: install
install: kaem get_machine
	mkdir -p $(bindir)
	cp $^ $(bindir)

###  dist
.PHONY: dist

COMMIT=$(shell git describe --dirty)
TARBALL_VERSION=$(COMMIT:Release_%=%)
TARBALL_DIR:=$(PACKAGE)-$(TARBALL_VERSION)
TARBALL=$(TARBALL_DIR).tar.gz
# Be friendly to Debian; avoid using EPOCH
MTIME=$(shell git show HEAD --format=%ct --no-patch)
# Reproducible tarball
TAR_FLAGS=--sort=name --mtime=@$(MTIME) --owner=0 --group=0 --numeric-owner --mode=go=rX,u+rw,a-s

$(TARBALL):
	(git ls-files					\
	    --exclude=$(TARBALL_DIR);			\
	    echo $^ | tr ' ' '\n')			\
	    | tar $(TAR_FLAGS)				\
	    --transform=s,^,$(TARBALL_DIR)/,S -T- -cf-	\
	    | gzip -c --no-name > $@

dist: $(TARBALL)
