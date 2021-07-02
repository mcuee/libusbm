INCLUDES = -I$(top_srcdir)/libusb
noinst_PROGRAMS = lsusb testlibusb benchmark mpl_test

lsusb_SOURCES = lsusb.c
lsusb_LDADD = ../libusb/libusb.la

testlibusb_SOURCES = testlibusb.c
testlibusb_LDADD = ../libusb/libusb.la

benchmark_SOURCES = benchmark.c ../libusb/mpl_threads.c
benchmark_LDADD = ../libusb/libusb.la

mpl_test_SOURCES = mpl_test.c ../libusb/mpl_threads.c
