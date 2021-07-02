/*
 * Prototypes, structure definitions and macros.
 *
 * Copyright (c) 2000-2003 Johannes Erdfelt <johannes@erdfelt.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * This file (and only this file) may alternatively be licensed under the
 * BSD license. See the LICENSE file shipped with the libusb-compat-0.1 source
 * distribution for details.
 */

#ifndef __USB_H__
#define __USB_H__

#ifdef _MSC_VER
/* on MS environments, the inline keyword is available in C++ only */
#ifndef inline
#define inline __inline
#endif
/* ssize_t is also not available (copy/paste from MinGW) */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#undef ssize_t
#ifdef _WIN64
  typedef __int64 ssize_t;
#else
  typedef int ssize_t;
#endif /* _WIN64 */
#endif /* _SSIZE_T_DEFINED */
#endif /* _MSC_VER */

/* stdint.h is also not usually available on MS */
#if defined(_MSC_VER) && (_MSC_VER < 1600) && (!defined(_STDINT)) && (!defined(_STDINT_H))
typedef unsigned __int8   uint8_t;
typedef __int8  int8_t;
typedef unsigned __int16  uint16_t;
typedef __int16  int16_t;
typedef unsigned __int32  uint32_t;
typedef __int32  int32_t;
typedef unsigned __int64  uint64_t;
typedef __int64  int64_t;
#else
#include <stdint.h>
#endif

#include <sys/types.h>
#include <time.h>
#include <limits.h>

#if defined(__linux) || defined(__APPLE__) || defined(__CYGWIN__)
#include <sys/time.h>
#endif


#if defined(_WIN32) || defined(__CYGWIN__)
#  include <windows.h>
#  define LIBUSB_PATH_MAX 260
#  if defined(interface)
#    undef interface
#  endif
#  if !defined(API_EXPORTED)
#    define API_EXPORTED
#  endif
#else
#  define LIBUSB_PATH_MAX PATH_MAX
#  include <dirent.h>
#endif

/** \def USBAPI_DECL
 * \ingroup misc
 * libusb0's Windows calling convention.
 *
 * Under Windows, the selection of available compilers and configurations
 * means that, unlike other platforms, there is not <em>one true calling
 * convention</em> (calling convention: the manner in which parameters are
 * passed to funcions in the generated assembly code).
 *
 * Matching the Windows API itself, libusb0 uses the __cdecl convention (which
 * translates to the <tt>stdcall</tt> convention) and guarantees that the
 * library is compiled in this way. The public header file also includes
 * appropriate annotations so that your own software will use the right
 * convention, even if another convention is being used by default within
 * your codebase.
 *
 * The one consideration that you must apply in your software is to mark
 * all functions which you use as libusb0 callbacks with this USBAPI_DECL
 * annotation, so that they too get compiled for the correct calling
 * convention.
 *
 * On non-Windows operating systems, this macro is defined as nothing. This
 * means that you can apply it to your code without worrying about
 * cross-platform compatibility.
 */
/* USBAPI_DECL must be defined on both definition and declaration of libusb0
 * functions. You'd think that declaration would be enough, but cygwin will
 * complain about conflicting types unless both are marked this way.
 * The placement of this macro is important too; it must appear after the
 * return type, before the function name. See internal documentation for
 * API_EXPORTED.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#define USBAPI_DECL __cdecl
#define USB_CPU_TO_LE16(x) (x)
#define USB_LE16_TO_CPU(x) (x)

#else
#define USBAPI_DECL

/** \def USB_CPU_TO_LE16
 * \ingroup misc
 * Convert a 16-bit value from host-endian to little-endian format. On
 * little endian systems, this function does nothing. On big endian systems,
 * the bytes are swapped.
 * \param x the host-endian value to convert
 * \returns the value in little-endian byte order
 */
static inline uint16_t USB_CPU_TO_LE16(const uint16_t x)
{
	union {
		uint8_t  b8[2];
		uint16_t b16;
	} _tmp;
	_tmp.b8[1] = x >> 8;
	_tmp.b8[0] = x & 0xff;
	return _tmp.b16;
}

/** \def USB_LE16_TO_CPU
 * \ingroup misc
 * Convert a 16-bit value from little-endian to host-endian format. On
 * little endian systems, this function does nothing. On big endian systems,
 * the bytes are swapped.
 * \param x the little-endian value to convert
 * \returns the value in host-endian byte order
 */
#define USB_LE16_TO_CPU(x) USB_CPU_TO_LE16(x)

#endif

/*
 * USB spec information
 *
 * This is all stuff grabbed from various USB specs and is pretty much
 * not subject to change
 */

/*
 * Device and/or Interface Class codes
 */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PRINTER		7
#define USB_CLASS_PTP			6
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_DATA			10
#define USB_CLASS_VENDOR_SPEC		0xff

/*
 * Descriptor types
 */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05

#define USB_DT_HID			0x21
#define USB_DT_REPORT			0x22
#define USB_DT_PHYSICAL			0x23
#define USB_DT_HUB			0x29

/*
 * Descriptor sizes per descriptor type
 */
#define USB_DT_DEVICE_SIZE		18
#define USB_DT_CONFIG_SIZE		9
#define USB_DT_INTERFACE_SIZE		9
#define USB_DT_ENDPOINT_SIZE		7
#define USB_DT_ENDPOINT_AUDIO_SIZE	9	/* Audio extension */
#define USB_DT_HUB_NONVAR_SIZE		7

#if defined(_WIN32) || defined(__CYGWIN__)
/* ensure byte-packed structures */
#include <pshpack1.h>
#endif

/* All standard descriptors have these 2 fields in common */
struct usb_descriptor_header {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
};

/* String descriptor */
struct usb_string_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wData[1];
};

/* HID descriptor */
struct usb_hid_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdHID;
	uint8_t  bCountryCode;
	uint8_t  bNumDescriptors;
	/* uint8_t  bReportDescriptorType; */
	/* uint16_t wDescriptorLength; */
	/* ... */
};

/* Endpoint descriptor */
#define USB_MAXENDPOINTS	32
struct usb_endpoint_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bEndpointAddress;
	uint8_t  bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t  bInterval;
	uint8_t  bRefresh;
	uint8_t  bSynchAddress;

	unsigned char *extra;	/* Extra descriptors */
	int extralen;
};

#define USB_ENDPOINT_ADDRESS_MASK	0x0f    /* in bEndpointAddress */
#define USB_ENDPOINT_DIR_MASK		0x80

#define USB_ENDPOINT_TYPE_MASK		0x03    /* in bmAttributes */
#define USB_ENDPOINT_TYPE_CONTROL	0
#define USB_ENDPOINT_TYPE_ISOCHRONOUS	1
#define USB_ENDPOINT_TYPE_BULK		2
#define USB_ENDPOINT_TYPE_INTERRUPT	3

/* Interface descriptor */
#define USB_MAXINTERFACES	32
struct usb_interface_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bInterfaceNumber;
	uint8_t  bAlternateSetting;
	uint8_t  bNumEndpoints;
	uint8_t  bInterfaceClass;
	uint8_t  bInterfaceSubClass;
	uint8_t  bInterfaceProtocol;
	uint8_t  iInterface;

	struct usb_endpoint_descriptor *endpoint;

	unsigned char *extra;	/* Extra descriptors */
	int extralen;
};

#define USB_MAXALTSETTING	128	/* Hard limit */
struct usb_interface {
	struct usb_interface_descriptor *altsetting;

	int num_altsetting;
};

/* Configuration descriptor information.. */
#define USB_MAXCONFIG		8
struct usb_config_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wTotalLength;
	uint8_t  bNumInterfaces;
	uint8_t  bConfigurationValue;
	uint8_t  iConfiguration;
	uint8_t  bmAttributes;
	uint8_t  MaxPower;

	struct usb_interface *interface;

	unsigned char *extra;	/* Extra descriptors */
	int extralen;
};

/* Device descriptor */
struct usb_device_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdUSB;
	uint8_t  bDeviceClass;
	uint8_t  bDeviceSubClass;
	uint8_t  bDeviceProtocol;
	uint8_t  bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t  iManufacturer;
	uint8_t  iProduct;
	uint8_t  iSerialNumber;
	uint8_t  bNumConfigurations;
};

struct usb_ctrl_setup {
	uint8_t  bRequestType;
	uint8_t  bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
/* 0x02 is reserved */
#define USB_REQ_SET_FEATURE		0x03
/* 0x04 is reserved */
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

/*
 * Various libusb API related stuff
 */

#define USB_ENDPOINT_IN			0x80
#define USB_ENDPOINT_OUT		0x00

/* Error codes */
#define USB_ERROR_BEGIN			500000

/*
 * Device reset types for usb_reset_ex.
 * http://msdn.microsoft.com/en-us/library/ff537269%28VS.85%29.aspx
 * http://msdn.microsoft.com/en-us/library/ff537243%28v=vs.85%29.aspx
 */
#define USB_RESET_TYPE_RESET_PORT (1 << 0)
#define USB_RESET_TYPE_CYCLE_PORT (1 << 1)
#define USB_RESET_TYPE_FULL_RESET (USB_RESET_TYPE_CYCLE_PORT | USB_RESET_TYPE_RESET_PORT)

/* Data types */
struct usb_device;
struct usb_bus;

/*
 * To maintain compatibility with applications already built with libusb,
 * we must only add entries to the end of this structure. NEVER delete or
 * move members and only change types if you really know what you're doing.
 */
struct usb_device {
  struct usb_device *next, *prev;

  char filename[LIBUSB_PATH_MAX + 1];

  struct usb_bus *bus;

  struct usb_device_descriptor descriptor;
  struct usb_config_descriptor *config;

  void *dev;		/* Darwin support */

  uint8_t devnum;

  unsigned char num_children;
  struct usb_device **children;
};

struct usb_bus {
  struct usb_bus *next, *prev;

  char dirname[LIBUSB_PATH_MAX + 1];

  struct usb_device *devices;
  uint32_t location;

  struct usb_device *root_dev;
};

/* Version information, Windows specific */
struct usb_version
{
    struct
    {
        int major;
        int minor;
        int micro;
        int nano;
    } dll;
    struct
    {
        int major;
        int minor;
        int micro;
        int nano;
    } driver;
};

struct usb_dev_handle;
typedef struct usb_dev_handle usb_dev_handle;

/* Variables */
#if defined(_WIN32) || defined(__CYGWIN__)
#include <poppack.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Function prototypes */

/* usb.c */
usb_dev_handle* USBAPI_DECL usb_open(struct usb_device *dev);
int USBAPI_DECL usb_close(usb_dev_handle *dev);
int USBAPI_DECL usb_get_string(usb_dev_handle *dev, int index, int langid, char *buf, size_t buflen);
int USBAPI_DECL usb_get_string_simple(usb_dev_handle *dev, int index, char *buf, size_t buflen);

/* descriptors.c */
int USBAPI_DECL usb_get_descriptor_by_endpoint(usb_dev_handle *udev, int ep, unsigned char type, unsigned char index, void *buf, int size);
int USBAPI_DECL usb_get_descriptor(usb_dev_handle *udev, unsigned char type, unsigned char index, void *buf, int size);

/* <arch>.c */
int USBAPI_DECL usb_bulk_write(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout);
int USBAPI_DECL usb_bulk_read(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout);
int USBAPI_DECL usb_interrupt_write(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout);
int USBAPI_DECL usb_interrupt_read(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout);
int USBAPI_DECL usb_control_msg(usb_dev_handle *dev, int requesttype, int request, int value, int index, char *bytes, int size, int timeout);
int USBAPI_DECL usb_set_configuration(usb_dev_handle *dev, int configuration);
int USBAPI_DECL usb_claim_interface(usb_dev_handle *dev, int interface);
int USBAPI_DECL usb_release_interface(usb_dev_handle *dev, int interface);
int USBAPI_DECL usb_set_altinterface(usb_dev_handle *dev, int alternate);
int USBAPI_DECL usb_resetep(usb_dev_handle *dev, unsigned int ep);
int USBAPI_DECL usb_clear_halt(usb_dev_handle *dev, unsigned int ep);
int USBAPI_DECL usb_reset(usb_dev_handle *dev);

#define LIBUSB_HAS_GET_DRIVER_NP 1
int USBAPI_DECL usb_get_driver_np(usb_dev_handle *dev, int interface, char *name, unsigned int namelen);
#define LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP 1
int USBAPI_DECL usb_detach_kernel_driver_np(usb_dev_handle *dev, int interface);

char* USBAPI_DECL usb_strerror(void);

void USBAPI_DECL usb_init(void);
void USBAPI_DECL usb_set_debug(int level);
int USBAPI_DECL usb_find_busses(void);
int USBAPI_DECL usb_find_devices(void);
struct usb_device* USBAPI_DECL usb_device(usb_dev_handle *dev);
struct usb_bus* USBAPI_DECL usb_get_busses(void);

/* Asynchronous I/O */
int USBAPI_DECL usb_isochronous_setup_async(usb_dev_handle *dev, void **context, unsigned char ep, int pktsize);
int USBAPI_DECL usb_bulk_setup_async(usb_dev_handle *dev, void **context, unsigned char ep);
int USBAPI_DECL usb_interrupt_setup_async(usb_dev_handle *dev, void **context, unsigned char ep);
int USBAPI_DECL usb_submit_async(void *context, char *bytes, int size);
int USBAPI_DECL usb_reap_async(void *context, int timeout);
int USBAPI_DECL usb_reap_async_nocancel(void *context, int timeout);
int USBAPI_DECL usb_cancel_async(void *context);
int USBAPI_DECL usb_free_async(void **context);

int USBAPI_DECL usb_initex(void* reserved);
void USBAPI_DECL usb_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_H__ */

