#!/usr/bin/make -f
CC=gcc
LIBS=-L/opt/so2.7/lib -lsqlite3
OBJECTS=\
	main.o \
	wd_directory.o \
	list_sub_dirs.o \
	iterate_inotify_events.o \
	hash_cache.o

TEST_WD_OBJECTS=\
	wd_directory.o \
	test_wd_directory.o

CFLAGS=-Wall $(CFLAGS_DEBUG) $(CFLAGS_OPT)

all: release

release: CFLAGS_OPT=-O2
release: spideroak_inotify_dir_watcher

debug: CFLAGS_DEBUG = -ggdb -DDEBUG
debug: spideroak_inotify_dir_watcher

valgrind: CFLAGS_DEBUG = -ggdb -DDEBUG
valgrind: spideroak_inotify_dir_watcher

test_wd_directory: CFLAGS_DEBUG = -ggdb -D DEBUG
test_wd_directory: $(TEST_WD_OBJECTS)


spideroak_inotify_dir_watcher: $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $? $(LIBS)

clean:
	rm -f test_wd_directory spideroak_inotify_dir_watcher *.o

.PHONY: release debug valgrind clean all
