# POSIX shims Ruby needs on the Vita (dup2, mmap/munmap/mprotect as heap blocks, misc POSIX).
CC = arm-vita-eabi-gcc
AR = arm-vita-eabi-ar
CFLAGS = -O2 -Iinclude
OBJS = dup2.o mman.o vita_posix.o
libvitacompat.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)
clean:
	rm -f $(OBJS) libvitacompat.a
