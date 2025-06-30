# Students' Makefile for the Malloc Lab
#
TEAM = bovik
VERSION = 1
HANDINDIR = /afs/cs.cmu.edu/academic/class/15213-f01/malloclab/handin

CC = gcc

# FLAG 분리
# for debugging
DEBUG_FLAGS = -Wall -Wextra -ggdb3 -pg -g -std=gnu99 -DDEBUG -DDRIVER -m32
# for release (제출용)
RELEASE_FLAGS = -Wall -O2 -std=gnu99 -DDRIVER -m32

# 기본 플래그 (기존 호환성을 위해)
CFLAGS = -Wall -O2 -m32

OBJS = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

# 기본 타겟 (기존 호환성 유지)
mdriver: $(OBJS)
	$(CC) $(CFLAGS) -o mdriver $(OBJS)

# 의존성 규칙들
mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
memlib.o: memlib.c memlib.h
mm.o: mm.c mm.h memlib.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h

# 디버그 및 릴리즈 타겟
debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(DEBUG_FLAGS)"

release:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(RELEASE_FLAGS)"

handin:
	cp mm.c $(HANDINDIR)/$(TEAM)-$(VERSION)-mm.c

clean:
	rm -f *~ *.o mdriver

# phony 타겟 선언 (권장사항)
.PHONY: clean handin debug release