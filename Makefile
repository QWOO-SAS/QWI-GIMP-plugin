

ifeq ($(shell uname -p),x86_64)
	CFLAGS +=-fPIC
  LIBDIR_PATH=/usr/lib/x86_64-linux-gnu
else
  LIBDIR_PATH=/usr/lib/i386-linux-gnu
endif

CFLAGS +=-O3

LIBS=-lpthread \
  -l:libqwi.a \
  -lglib-2.0 \
  -lgimp-2.0 \
  -lgimpbase-2.0 \
  -lgimpmodule-2.0 \
  -lgimpconfig-2.0 \
  -lgimpui-2.0 \
  -lgimpwidgets-2.0 \
  -lgobject-2.0 \
  -lcairo \
  -lgdk_pixbuf-2.0 \
  -lgtk-x11-2.0 \
  -lpango-1.0 \
  -latk-1.0

INCLUDES=-I/usr/include/glib-2.0\
  -I$(LIBDIR_PATH)/glib-2.0/include \
  -I/usr/include/gimp-2.0 \
  -I/usr/include/cairo \
  -I/usr/include/gdk-pixbuf-2.0 \
  -I/usr/include/gtk-2.0 \
  -I/usr/include/pango-1.0 \
  -I$(LIBDIR_PATH)/gtk-2.0/include \
  -I/usr/include/atk-1.0 

CFLAGS +=$(INCLUDES)
all: ex
	

C_SRCS += \
file-qwi.c \
qwi-write.c \
qwi-read.c 

OBJS += \
file-qwi.o \
qwi-write.o \
qwi-read.o

C_DEPS += \
file-qwi.d \
qwi-write.d \
qwi-read.d 

%.o: %.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -Wall -c -fmessage-length=0 $(CFLAGS) $(INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<" $(LIBS)
	@echo 'Finished building: $<'

ex: $(OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc $(CFLAGS) -o file-qwi  $(OBJS) $(LIBS)
	@echo 'Finished building shared target: $@'
	@echo ' '


install:
	-cp file-qwi ~/.gimp-2.8/plug-ins

clean:
	-rm -f *.o *.d file-qwi
