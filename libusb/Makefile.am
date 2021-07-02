include_HEADERS = usb.h
lib_LTLIBRARIES = libusb.la

LINUX_USBFS_SRC = core_linux.c mpl_threads.c
DARWIN_USB_SRC = core_linux.c mpl_threads.c
OPENBSD_USB_SRC = core_linux.c mpl_threads.c
WINDOWS_USB_SRC = core_windows.c windows_usb.c windows_error.c windows_install.c \
	windows_descriptors.c windows_registry.c windows_resource.rc

if OS_LINUX
OS_SRC = $(LINUX_USBFS_SRC)
endif

if OS_DARWIN
OS_SRC = $(DARWIN_USB_SRC)
AM_CFLAGS_EXT = -no-cpp-precomp
endif

if OS_OPENBSD
OS_SRC = $(OPENBSD_USB_SRC)
endif

if OS_WINDOWS
OS_SRC = $(WINDOWS_USB_SRC)

.rc.lo:
	$(AM_V_GEN)$(LIBTOOL) $(AM_V_lt) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile $(RC) $(RCFLAGS) -i $< -o $@

windows_resource.rc: libusb-win32_version.h
endif

libusb_la_SOURCES = $(OS_SRC) usbi.h
libusb_la_CFLAGS = -fvisibility=hidden $(AM_CFLAGS) $(LIBUSB_1_0_CFLAGS)
libusb_la_LIBADD = $(LIBUSB_1_0_LIBS)
libusb_la_LDFLAGS = -version-info $(LT_MAJOR):$(LT_REVISION):$(LT_AGE) \
	-release 0.1

