/*
 * libusb-win32 extensions:
 * Copyright (C) 2012 Travis Robinson <libusbdotnet@gmail.com>
 *
 * Core functions for libusb-compat-0.1
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
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
 */

#include <config.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32) && !defined(__CYGWIN__)
#include <unistd.h>
#endif
#include <libusb.h>

#define USB0_LOG_APPNAME "Usb0"
#include "ud_error.h"

#include "mpl_threads.h"
#include "usb.h"
#include "usbi.h"

#ifndef API_EXPORTED
#define API_EXPORTED
#endif

#define ASYNC_TIMVAL_SEC	(1)
#define ALLOW_HANDLE_EVENTS_THREAD_IDLE

#if defined(_MSC_VER) && _MSC_VER >= 1310
// VS 2003 or greater.
#  pragma warning(disable:4100)	// unreferenced formal parameter
#  pragma warning(disable:4127)	// conditional expression is constant
#endif

/* libusb0 async transfer context */
typedef struct
{
    usb_dev_handle *dev;
    struct libusb_transfer *transfer;
	MPL_EVENT_T complete_event;
	volatile long ref_count;
	int legacy_iso_pktsize;

} usb_async_transfer_t;

/* libusb0 async thread handler members */
typedef struct
{
#ifdef ALLOW_HANDLE_EVENTS_THREAD_IDLE
	volatile long fly_count;
#endif
	volatile long is_run;

	MPL_THREAD_T handle;
	MPL_MUTEX_T init_mutex;
	MPL_EVENT_T event_running;
	MPL_EVENT_T event_terminated;
} usb_async_thread_t;

/* Globals: */
static libusb_context *ctx = NULL;
static int usb_debug = 0;
static usb_async_thread_t async_thread;

struct usb_bus *usb_busses = NULL;

static volatile long g_usb0_lib_init_lock = 0;

#define compat_err(e) -(errno=libusb_to_errno(e))
static int libusb_to_errno(int result)
{
	switch (result) {
	case LIBUSB_SUCCESS:
		return 0;
	case LIBUSB_ERROR_IO:
		return EIO;
	case LIBUSB_ERROR_INVALID_PARAM:
		return EINVAL;
	case LIBUSB_ERROR_ACCESS:
		return EACCES;
	case LIBUSB_ERROR_NO_DEVICE:
		return ENXIO;
	case LIBUSB_ERROR_NOT_FOUND:
		return ENOENT;
	case LIBUSB_ERROR_BUSY:
		return EBUSY;
	case LIBUSB_ERROR_TIMEOUT:
		return ETIMEDOUT;
	case LIBUSB_ERROR_OVERFLOW:
		return EOVERFLOW;
	case LIBUSB_ERROR_PIPE:
		return EPIPE;
	case LIBUSB_ERROR_INTERRUPTED:
		return EINTR;
	case LIBUSB_ERROR_NO_MEM:
		return ENOMEM;
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return ENOSYS;
	default:
		return ERANGE;
	}
}

API_EXPORTED void USBAPI_DECL usb_init(void)
{
	int r;
	UD_DBG("\n");

	if (!ctx) {
		r = libusb_init(&ctx);
		if (r < 0) {
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("libusb_init failed. ret=%d\n",r);
			return;
		}

		/* usb_set_debug can be called before usb_init */
		if (usb_debug)
			libusb_set_debug(ctx, 3);

		/* initialize the async thread members */
		memset(&async_thread,0,sizeof(async_thread));
	}
}

static int async_start_events(void);

API_EXPORTED int USBAPI_DECL usb_initex(void* reserved)
{
	int r=0;
	UD_DBG("\n");

	if (MPL_Atomic_Inc32(&g_usb0_lib_init_lock) == 1) {

		usb_init();
		if (!ctx) {
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("libusb_init failed. ret=%d\n",r);
				return -errno;
		}

		if ((r = Mpl_Init()) != MPL_SUCCESS) {
			libusb_exit(ctx); ctx = NULL;
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("Mpl_Init failed. ret=%d\n",r);
			return -(errno=r);
		}

		if ((r = Mpl_Mutex_Init(&async_thread.init_mutex)) != MPL_SUCCESS) {
			Mpl_Free();
			libusb_exit(ctx); ctx = NULL;
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("Mpl_Mutex_Init failed. ret=%d\n",r);
			return -(errno=r);
		}

		if ((r = Mpl_Event_Init(&async_thread.event_running,1,0)) != MPL_SUCCESS) {
			Mpl_Mutex_Free(&async_thread.init_mutex);
			Mpl_Free();
			libusb_exit(ctx); ctx = NULL;
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("Mpl_Event_Init failed. ret=%d",r);
			return -(errno=r);
		}
		if ((r = Mpl_Event_Init(&async_thread.event_terminated,0,0)) != MPL_SUCCESS) {
			Mpl_Event_Free(&async_thread.event_running);
			Mpl_Mutex_Free(&async_thread.init_mutex);
			Mpl_Free();
			libusb_exit(ctx); ctx = NULL;
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("Mpl_Mutex_Init failed. ret=%d\n",r);
			return -(errno=r);
		}

		if ((r = async_start_events()) != 0)
		{
			Mpl_Event_Free(&async_thread.event_terminated);
			Mpl_Event_Free(&async_thread.event_running);
			Mpl_Mutex_Free(&async_thread.init_mutex);
			Mpl_Free();
			libusb_exit(ctx); ctx = NULL;
			MPL_Atomic_Dec32(&g_usb0_lib_init_lock);
			UD_ERR("async_start_events failed. ret=%d\n",r);
			return -(errno=r);

		}
	}

	return r;
}

API_EXPORTED void USBAPI_DECL usb_set_debug(int level)
{
	usb_debug = level;

	/* usb_set_debug can be called before usb_init */
	if (ctx)
		libusb_set_debug(ctx, 3);
}

API_EXPORTED char* USBAPI_DECL usb_strerror(void)
{
	return strerror(errno);
}

static int find_busses(struct usb_bus **ret)
{
	libusb_device **dev_list = NULL;
	struct usb_bus *busses = NULL;
	struct usb_bus *bus;
	int dev_list_len = 0;
	int i;
	int r;

	r = libusb_get_device_list(ctx, &dev_list);
	if (r < 0) {
		UD_ERR("get_device_list failed with error %d\n", r);
		return compat_err(r);
	}

	if (r == 0) {
		libusb_free_device_list(dev_list, 1);
		/* no buses */
		return 0;
	}

	/* iterate over the device list, identifying the individual busses.
	 * we use the location field of the usb_bus structure to store the
	 * bus number. */

	dev_list_len = r;
	for (i = 0; i < dev_list_len; i++) {
		libusb_device *dev = dev_list[i];
		uint8_t bus_num = libusb_get_bus_number(dev);

		/* if we already know about it, continue */
		if (busses) {
			int found = 0;
			bus = busses;
			do {
				if (bus_num == bus->location) {
					found = 1;
					break;
				}
			} while ((bus = bus->next) != NULL);
			if (found)
				continue;
		}

		/* add it to the list of busses */
		bus = malloc(sizeof(*bus));
		if (!bus)
			goto err;

		memset(bus, 0, sizeof(*bus));
		bus->location = bus_num;
		sprintf(bus->dirname, "%03d", bus_num);
		LIST_ADD(busses, bus);
	}

	libusb_free_device_list(dev_list, 1);
	*ret = busses;
	return 0;

err:
	bus = busses;
	while (bus) {
		struct usb_bus *tbus = bus->next;
		free(bus);
		bus = tbus;
	}
	return -ENOMEM;
}

API_EXPORTED int USBAPI_DECL usb_find_busses(void)
{
	struct usb_bus *new_busses = NULL;
	struct usb_bus *bus;
	int changes = 0;
	int r;

	/* libusb-1.0 initialization might have failed, but we can't indicate
	 * this with libusb-0.1, so trap that situation here */
	if (!ctx)
		return 0;
	
	UD_DBG("\n");
	r = find_busses(&new_busses);
	if (r < 0) {
		UD_ERR("find_busses failed with error %d\n", r);
		return r;
	}

	/* walk through all busses we already know about, removing duplicates
	 * from the new list. if we do not find it in the new list, the bus
	 * has been removed. */

	bus = usb_busses;
	while (bus) {
		struct usb_bus *tbus = bus->next;
		struct usb_bus *nbus = new_busses;
		int found = 0;
		UD_DBG("in loop\n");

		while (nbus) {
			struct usb_bus *tnbus = nbus->next;

			if (bus->location == nbus->location) {
				LIST_DEL(new_busses, nbus);
				free(nbus);
				found = 1;
				break;
			}
			nbus = tnbus;
		}

		if (!found) {
			/* bus removed */
			UD_DBG("bus %d removed\n", bus->location);
			changes++;
			LIST_DEL(usb_busses, bus);
			free(bus);
		}

		bus = tbus;
	}

	/* anything remaining in new_busses is a new bus */
	bus = new_busses;
	while (bus) {
		struct usb_bus *tbus = bus->next;
		UD_DBG("bus %d added\n", bus->location);
		LIST_DEL(new_busses, bus);
		LIST_ADD(usb_busses, bus);
		changes++;
		bus = tbus;
	}

	return changes;
}

static int find_devices(libusb_device **dev_list, int dev_list_len,
	struct usb_bus *bus, struct usb_device **ret)
{
	struct usb_device *devices = NULL;
	struct usb_device *dev;
	int i;

	for (i = 0; i < dev_list_len; i++) {
		libusb_device *newlib_dev = dev_list[i];
		uint8_t bus_num = libusb_get_bus_number(newlib_dev);

		if (bus_num != bus->location)
			continue;

		dev = malloc(sizeof(*dev));
		if (!dev)
			goto err;

		/* No need to reference the device now, just take the pointer. We
		 * increase the reference count later if we keep the device. */
		dev->dev = newlib_dev;

		dev->bus = bus;
		dev->devnum = libusb_get_device_address(newlib_dev);
		sprintf(dev->filename, "%03d", dev->devnum);
		LIST_ADD(devices, dev);
	}

	*ret = devices;
	return 0;

err:
	dev = devices;
	while (dev) {
		struct usb_device *tdev = dev->next;
		free(dev);
		dev = tdev;
	}
	return -ENOMEM;
}

static void clear_endpoint_descriptor(struct usb_endpoint_descriptor *ep)
{
	if (ep->extra)
		free(ep->extra);
}

static void clear_interface_descriptor(struct usb_interface_descriptor *iface)
{
	if (iface->extra)
		free(iface->extra);
	if (iface->endpoint) {
		int i;
		for (i = 0; i < iface->bNumEndpoints; i++)
			clear_endpoint_descriptor(iface->endpoint + i);
		free(iface->endpoint);
	}
}

static void clear_interface(struct usb_interface *iface)
{
	if (iface->altsetting) {
		int i;
		for (i = 0; i < iface->num_altsetting; i++)
			clear_interface_descriptor(iface->altsetting + i);
		free(iface->altsetting);
	}
}

static void clear_config_descriptor(struct usb_config_descriptor *config)
{
	if (config->extra)
		free(config->extra);
	if (config->interface) {
		int i;
		for (i = 0; i < config->bNumInterfaces; i++)
			clear_interface(config->interface + i);
		free(config->interface);
	}
}

static void clear_device(struct usb_device *dev)
{
	int i;
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
		clear_config_descriptor(dev->config + i);
}

static int copy_endpoint_descriptor(struct usb_endpoint_descriptor *dest,
	const struct libusb_endpoint_descriptor *src)
{
	memcpy(dest, src, USB_DT_ENDPOINT_AUDIO_SIZE);

	dest->extralen = src->extra_length;
	if (src->extra_length) {
		dest->extra = malloc(src->extra_length);
		if (!dest->extra)
			return -ENOMEM;
		memcpy(dest->extra, src->extra, src->extra_length);
	}

	return 0;
}

static int copy_interface_descriptor(struct usb_interface_descriptor *dest,
	const struct libusb_interface_descriptor *src)
{
	int i;
	int num_endpoints = src->bNumEndpoints;
	size_t alloc_size = sizeof(struct usb_endpoint_descriptor) * num_endpoints;

	memcpy(dest, src, USB_DT_INTERFACE_SIZE);
	dest->endpoint = malloc(alloc_size);
	if (!dest->endpoint)
		return -ENOMEM;
	memset(dest->endpoint, 0, alloc_size);

	for (i = 0; i < num_endpoints; i++) {
		int r = copy_endpoint_descriptor(dest->endpoint + i, &src->endpoint[i]);
		if (r < 0) {
			clear_interface_descriptor(dest);
			return r;
		}
	}

	dest->extralen = src->extra_length;
	if (src->extra_length) {
		dest->extra = malloc(src->extra_length);
		if (!dest->extra) {
			clear_interface_descriptor(dest);
			return -ENOMEM;
		}
		memcpy(dest->extra, src->extra, src->extra_length);
	}

	return 0;
}

static int copy_interface(struct usb_interface *dest,
	const struct libusb_interface *src)
{
	int i;
	int num_altsetting = src->num_altsetting;
	size_t alloc_size = sizeof(struct usb_interface_descriptor)
		* num_altsetting;

	dest->num_altsetting = num_altsetting;
	dest->altsetting = malloc(alloc_size);
	if (!dest->altsetting)
		return -ENOMEM;
	memset(dest->altsetting, 0, alloc_size);

	for (i = 0; i < num_altsetting; i++) {
		int r = copy_interface_descriptor(dest->altsetting + i,
			&src->altsetting[i]);
		if (r < 0) {
			clear_interface(dest);
			return r;
		}
	}

	return 0;
}

static int copy_config_descriptor(struct usb_config_descriptor *dest,
	const struct libusb_config_descriptor *src)
{
	int i;
	int num_interfaces = src->bNumInterfaces;
	size_t alloc_size = sizeof(struct usb_interface) * num_interfaces;

	memcpy(dest, src, USB_DT_CONFIG_SIZE);
	dest->interface = malloc(alloc_size);
	if (!dest->interface)
		return -ENOMEM;
	memset(dest->interface, 0, alloc_size);

	for (i = 0; i < num_interfaces; i++) {
		int r = copy_interface(dest->interface + i, &src->interface[i]);
		if (r < 0) {
			clear_config_descriptor(dest);
			return r;
		}
	}

	dest->extralen = src->extra_length;
	if (src->extra_length) {
		dest->extra = malloc(src->extra_length);
		if (!dest->extra) {
			clear_config_descriptor(dest);
			return -ENOMEM;
		}
		memcpy(dest->extra, src->extra, src->extra_length);
	}

	return 0;
}

static int initialize_device(struct usb_device *dev)
{
	libusb_device *newlib_dev = dev->dev;
	int num_configurations;
	size_t alloc_size;
	int r;
	int i;

	/* device descriptor is identical in both libs */
	r = libusb_get_device_descriptor(newlib_dev,
		(struct libusb_device_descriptor *) &dev->descriptor);
	if (r < 0) {
		UD_ERR("error %d getting device descriptor\n", r);
		return compat_err(r);
	}

	num_configurations = dev->descriptor.bNumConfigurations;
	alloc_size = sizeof(struct usb_config_descriptor) * num_configurations;
	dev->config = malloc(alloc_size);
	if (!dev->config)
		return -ENOMEM;
	memset(dev->config, 0, alloc_size);

	/* even though structures are identical, we can't just use libusb-1.0's
	 * config descriptors because we have to store all configurations in
	 * a single flat memory area (libusb-1.0 provides separate allocations).
	 * we hand-copy libusb-1.0's descriptors into our own structures. */
	for (i = 0; i < num_configurations; i++) {
		struct libusb_config_descriptor *newlib_config;
		r = libusb_get_config_descriptor(newlib_dev,(uint8_t)i, &newlib_config);
		if (r < 0) {
			clear_device(dev);
			free(dev->config);
			return compat_err(r);
		}
		r = copy_config_descriptor(dev->config + i, newlib_config);
		libusb_free_config_descriptor(newlib_config);
		if (r < 0) {
			clear_device(dev);
			free(dev->config);
			return r;
		}
	}

	/* libusb doesn't implement this and it doesn't seem that important. If
	 * someone asks for it, we can implement it in v1.1 or later. */
	dev->num_children = 0;
	dev->children = NULL;

	libusb_ref_device(newlib_dev);
	return 0;
}

static void free_device(struct usb_device *dev)
{
	clear_device(dev);
	libusb_unref_device(dev->dev);
	free(dev);
}

API_EXPORTED int USBAPI_DECL usb_find_devices(void)
{
	struct usb_bus *bus;
	libusb_device **dev_list;
	int dev_list_len;
	int r;
	int changes = 0;

	/* libusb-1.0 initialization might have failed, but we can't indicate
	 * this with libusb-0.1, so trap that situation here */
	if (!ctx)
		return 0;

	UD_DBG("\n");
	dev_list_len = libusb_get_device_list(ctx, &dev_list);
	if (dev_list_len < 0)
		return compat_err(dev_list_len);

	for (bus = usb_busses; bus; bus = bus->next) {
		struct usb_device *new_devices = NULL;
		struct usb_device *dev;

		r = find_devices(dev_list, dev_list_len, bus, &new_devices);
		if (r < 0) {
			libusb_free_device_list(dev_list, 1);
			return r;
		}

		/* walk through the devices we already know about, removing duplicates
		 * from the new list. if we do not find it in the new list, the device
		 * has been removed. */
		dev = bus->devices;
		while (dev) {
			int found = 0;
			struct usb_device *tdev = dev->next;
			struct usb_device *ndev = new_devices;

			while (ndev) {
				if (ndev->devnum == dev->devnum) {
					LIST_DEL(new_devices, ndev);
					free(ndev);
					found = 1;
					break;
				}
				ndev = ndev->next;
			}

			if (!found) {
				UD_DBG("device %d.%d removed\n",
					dev->bus->location, dev->devnum);
				LIST_DEL(bus->devices, dev);
				free_device(dev);
				changes++;
			}

			dev = tdev;
		}

		/* anything left in new_devices is a new device */
		dev = new_devices;
		while (dev) {
			struct usb_device *tdev = dev->next;
			r = initialize_device(dev);	
			if (r < 0) {
				UD_ERR("couldn't initialize device %d.%d (error %d)\n",
					dev->bus->location, dev->devnum, r);
				dev = tdev;
				continue;
			}
			UD_DBG("device %d.%d added\n", dev->bus->location, dev->devnum);
			LIST_DEL(new_devices, dev);
			LIST_ADD(bus->devices, dev);
			changes++;
			dev = tdev;
		}
	}

	libusb_free_device_list(dev_list, 1);
	return changes;
}

API_EXPORTED struct usb_bus* USBAPI_DECL usb_get_busses(void)
{
	return usb_busses;
}

API_EXPORTED usb_dev_handle* USBAPI_DECL usb_open(struct usb_device *dev)
{
	int r;
	usb_dev_handle *udev;
	UD_DBG("\n");

	udev = malloc(sizeof(*udev));
	if (!udev)
		return NULL;

	r = libusb_open((libusb_device *) dev->dev, &udev->handle);
	if (r < 0) {
		if (r == LIBUSB_ERROR_ACCESS) {
			UD_INFO("Device open failed due to a permission denied error.\n");
			UD_INFO("libusb requires write access to USB device nodes.\n");
		}
		UD_ERR("could not open device, error %d\n", r);
		free(udev);
		errno = libusb_to_errno(r);
		return NULL;
	}

	udev->last_claimed_interface = -1;
	udev->device = dev;

	return udev;
}

API_EXPORTED int USBAPI_DECL usb_close(usb_dev_handle *dev)
{
	UD_DBG("\n");
	libusb_close(dev->handle);
	free(dev);
	return 0;
}

API_EXPORTED struct usb_device* USBAPI_DECL usb_device(usb_dev_handle *dev)
{
	return dev->device;
}

API_EXPORTED int USBAPI_DECL usb_set_configuration(usb_dev_handle *dev, int configuration)
{
	UD_DBG("configuration %d\n", configuration);
	return compat_err(libusb_set_configuration(dev->handle, configuration));
}

API_EXPORTED int USBAPI_DECL usb_claim_interface(usb_dev_handle *dev, int interface)
{
	int r;
	UD_DBG("interface %d\n", interface);

	r = libusb_claim_interface(dev->handle, interface);
	if (r == 0) {
		dev->last_claimed_interface = interface;
		return 0;
	}

	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_release_interface(usb_dev_handle *dev, int interface)
{
	int r;
	UD_DBG("interface %d\n", interface);

	r = libusb_release_interface(dev->handle, interface);
	if (r == 0)
		dev->last_claimed_interface = -1;

	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_set_altinterface(usb_dev_handle *dev, int alternate)
{
	UD_DBG("alternate %d\n", alternate);
	if (dev->last_claimed_interface < 0)
		return -(errno=EINVAL);
	
	return compat_err(libusb_set_interface_alt_setting(dev->handle,
		dev->last_claimed_interface, alternate));
}

API_EXPORTED int USBAPI_DECL usb_resetep(usb_dev_handle *dev, unsigned int ep)
{
	return compat_err(usb_clear_halt(dev, ep));
}

API_EXPORTED int USBAPI_DECL usb_clear_halt(usb_dev_handle *dev, unsigned int ep)
{
	UD_DBG("endpoint %x\n", ep);
	return compat_err(libusb_clear_halt(dev->handle, ep & 0xff));
}

API_EXPORTED int USBAPI_DECL usb_reset(usb_dev_handle *dev)
{
	UD_DBG("\n");
	return compat_err(libusb_reset_device(dev->handle));
}

static int usb_bulk_io(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	int actual_length;
	int r;

	/* Travis: Fixed */
	if (errno==ETIMEDOUT) errno=0;

	UD_DBG("endpoint %x size %d timeout %d\n", ep, size, timeout);
	r = libusb_bulk_transfer(dev->handle, ep & 0xff, (unsigned char*)&bytes[0], size,
		&actual_length, timeout);
	
	/* if we timed out but did transfer some data, report as successful short
	 * read. FIXME: is this how libusb-0.1 works?
	 * - Travis: Fixed
	     This is a bug in libusb-win32 which will be fixed accordingly.	*/
	if (r == LIBUSB_SUCCESS) {
		return actual_length;
	}
	else if (r == LIBUSB_ERROR_TIMEOUT && actual_length > 0) {
		errno = ETIMEDOUT;
		return actual_length;
	}
	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_bulk_read(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	if (!(ep & USB_ENDPOINT_IN)) {
		/* libusb-0.1 will strangely fix up a read request from endpoint
		 * 0x01 to be from endpoint 0x81. do the same thing here, but
		 * warn about this silly behaviour. */
		UD_WRN("endpoint %x is missing IN direction bit, fixing\n");
		ep |= USB_ENDPOINT_IN;
	}

	return usb_bulk_io(dev, ep, bytes, size, timeout);
}

API_EXPORTED int USBAPI_DECL usb_bulk_write(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	if (ep & USB_ENDPOINT_IN) {
		/* libusb-0.1 on BSD strangely fix up a write request to endpoint
		 * 0x81 to be to endpoint 0x01. do the same thing here, but
		 * warn about this silly behaviour. */
		UD_WRN("endpoint %x has excessive IN direction bit, fixing\n");
		ep &= ~USB_ENDPOINT_IN;
	}

	return usb_bulk_io(dev, ep, (char *)bytes, size, timeout);
}

static int usb_interrupt_io(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	int actual_length;
	int r;
	UD_DBG("endpoint %x size %d timeout %d\n", ep, size, timeout);

	/* Travis: Fixed */
	if (errno==ETIMEDOUT) errno=0;

	r = libusb_interrupt_transfer(dev->handle, ep & 0xff, (unsigned char*)&bytes[0], size,
		&actual_length, timeout);
	
	/* if we timed out but did transfer some data, report as successful short
	 * read. FIXME: is this how libusb-0.1 works?
	 * - Travis: Fixed
	     This is a bug in libusb-win32 which will be fixed accordingly.	*/
	if (r == LIBUSB_SUCCESS) {
		return actual_length;
	}
	else if (r == LIBUSB_ERROR_TIMEOUT && actual_length > 0) {
		errno = ETIMEDOUT;
		return actual_length;
	}
	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_interrupt_read(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	if (!(ep & USB_ENDPOINT_IN)) {
		/* libusb-0.1 will strangely fix up a read request from endpoint
		 * 0x01 to be from endpoint 0x81. do the same thing here, but
		 * warn about this silly behaviour. */
		UD_WRN("endpoint %x is missing IN direction bit, fixing\n");
		ep |= USB_ENDPOINT_IN;
	}
	return usb_interrupt_io(dev, ep, bytes, size, timeout);
}

API_EXPORTED int USBAPI_DECL usb_interrupt_write(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	if (ep & USB_ENDPOINT_IN) {
		/* libusb-0.1 on BSD strangely fix up a write request to endpoint
		 * 0x81 to be to endpoint 0x01. do the same thing here, but
		 * warn about this silly behaviour. */
		UD_WRN("endpoint %x has excessive IN direction bit, fixing\n");
		ep &= ~USB_ENDPOINT_IN;
	}

	return usb_interrupt_io(dev, ep, (char *)bytes, size, timeout);
}

API_EXPORTED int USBAPI_DECL usb_control_msg(usb_dev_handle *dev, int bmRequestType,
	int bRequest, int wValue, int wIndex, char *bytes, int size, int timeout)
{
	int r;
	UD_DBG("RQT=%x RQ=%x V=%x I=%x len=%d timeout=%d\n", bmRequestType,
		bRequest, wValue, wIndex, size, timeout);

	r = libusb_control_transfer(dev->handle, bmRequestType & 0xff,
		bRequest & 0xff, wValue & 0xffff, wIndex & 0xffff, (unsigned char*)&bytes[0], size & 0xffff,
		timeout);

	if (r >= 0)
		return r;

	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_get_string(usb_dev_handle *dev, int desc_index, int langid,
	char *buf, size_t buflen)
{
	int r;
	r = libusb_get_string_descriptor(dev->handle, desc_index & 0xff,
		langid & 0xffff, (unsigned char*)&buf[0], (int) buflen);
	if (r >= 0)
		return r;
	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_get_string_simple(usb_dev_handle *dev, int desc_index,
	char *buf, size_t buflen)
{
	int r;
	r = libusb_get_string_descriptor_ascii(dev->handle, desc_index & 0xff,
		(unsigned char*)&buf[0], (int) buflen);
	if (r >= 0)
		return r;
	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_get_descriptor(usb_dev_handle *dev, unsigned char type,
	unsigned char desc_index, void *buf, int size)
{
	int r;
	r = libusb_get_descriptor(dev->handle, type, desc_index, buf, size);
	if (r >= 0)
		return r;
	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_get_descriptor_by_endpoint(usb_dev_handle *dev, int ep,
	unsigned char type, unsigned char desc_index, void *buf, int size)
{
	/* this function doesn't make much sense - the specs don't talk about
	 * getting a descriptor "by endpoint". libusb-1.0 does not provide this
	 * functionality so we just send a control message directly */
	int r;
	r = libusb_control_transfer(dev->handle,
		LIBUSB_ENDPOINT_IN | (ep & 0xff), LIBUSB_REQUEST_GET_DESCRIPTOR,
		(uint16_t)((type << 8) | desc_index), 0, buf, (uint16_t)size, 1000);
	if (r >= 0)
		return r;
	return compat_err(r);
}

API_EXPORTED int USBAPI_DECL usb_get_driver_np(usb_dev_handle *dev, int interface,
	char *name, unsigned int namelen)
{
	int r = libusb_kernel_driver_active(dev->handle, interface);
	if (r == 1) {
		/* libusb-1.0 doesn't expose driver name, so fill in a dummy value */
		snprintf(name, namelen, "dummy");
		return 0;
	} else if (r == 0) {
		return -(errno=ENODATA);
	} else {
		return compat_err(r);
	}
}

API_EXPORTED int USBAPI_DECL usb_detach_kernel_driver_np(usb_dev_handle *dev, int interface)
{
	int r = compat_err(libusb_detach_kernel_driver(dev->handle, interface));
	switch (r) {
	case LIBUSB_SUCCESS:
		return 0;
	case LIBUSB_ERROR_NOT_FOUND:
		return -ENODATA;
	case LIBUSB_ERROR_INVALID_PARAM:
		return -EINVAL;
	case LIBUSB_ERROR_NO_DEVICE:
		return -ENODEV;
	case LIBUSB_ERROR_OTHER:
		return -errno;
	/* default can be reached only in non-Linux implementations,
	 * mostly with LIBUSB_ERROR_NOT_SUPPORTED. */
	default:
		return -ENOSYS;
	}
}

///////////////////////////////////////
/* libusb0(M)ulti-platform Functions */
///////////////////////////////////////

#ifdef _MSC_VER

/* Workaround for testing on windows via libusbX.*/
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1

/*
 * time between jan 1, 1601 and jan 1, 1970 in units of 100 nanoseconds
 */
#ifndef PTW32_TIMESPEC_TO_FILETIME_OFFSET
#define PTW32_TIMESPEC_TO_FILETIME_OFFSET ( ((LONGLONG) 27111902 << 32) + (LONGLONG) 3577643008 )
#endif

static void filetime_to_timespec (const FILETIME * ft, struct timespec *ts)
     /*
      * -------------------------------------------------------------------
      * converts FILETIME (as set by GetSystemTimeAsFileTime), where the time is
      * expressed in 100 nanoseconds from Jan 1, 1601,
      * into struct timespec
      * where the time is expressed in seconds and nanoseconds from Jan 1, 1970.
      * -------------------------------------------------------------------
      */
{
  ts->tv_sec = (int) ((*(LONGLONG *) ft - PTW32_TIMESPEC_TO_FILETIME_OFFSET) / 10000000);
  ts->tv_nsec = (int) ((*(LONGLONG *) ft - PTW32_TIMESPEC_TO_FILETIME_OFFSET - ((LONGLONG) ts->tv_sec * (LONGLONG) 10000000)) * 100);
}

static int clock_gettime(int clock_id, struct timespec * abstime)
{
  const int64_t NANOSEC_PER_MILLISEC = 1000000;
  const int64_t MILLISEC_PER_SEC = 1000;
  int64_t absMilliseconds;
  FILETIME ft;

  /* get current system time */
  GetSystemTimeAsFileTime(&ft);
  filetime_to_timespec(&ft, abstime);
  absMilliseconds = (int64_t)abstime->tv_sec * MILLISEC_PER_SEC;
  absMilliseconds += ((int64_t)abstime->tv_nsec + (NANOSEC_PER_MILLISEC/2)) / NANOSEC_PER_MILLISEC;

  abstime->tv_nsec = (long)((absMilliseconds % 1000) * NANOSEC_PER_MILLISEC);
  abstime->tv_sec = (long)(absMilliseconds / MILLISEC_PER_SEC);

  return 0;
}

#endif
#endif // _MSC_VER

static void clock_add_rel_ms(int rel_ms, struct timespec * abstime)
{
  const int64_t NANOSEC_PER_MILLISEC = 1000000;
  const int64_t MILLISEC_PER_SEC = 1000;
  int64_t absMilliseconds;

  absMilliseconds = rel_ms;
  absMilliseconds += (int64_t)abstime->tv_sec * MILLISEC_PER_SEC;
  absMilliseconds += ((int64_t)abstime->tv_nsec + (NANOSEC_PER_MILLISEC/2)) / NANOSEC_PER_MILLISEC;

  abstime->tv_nsec = (long)((absMilliseconds % 1000) * NANOSEC_PER_MILLISEC);
  abstime->tv_sec = (long)(absMilliseconds / MILLISEC_PER_SEC);
}

static int libusb_transfer_to_errno(int status)
{
	switch (status) {

	case LIBUSB_TRANSFER_COMPLETED:
		return 0;
	case LIBUSB_TRANSFER_TIMED_OUT:
	case LIBUSB_TRANSFER_CANCELLED:
		return ETIMEDOUT;
	case LIBUSB_TRANSFER_STALL:
		return EIO;
	case LIBUSB_TRANSFER_NO_DEVICE:
		return ENODEV;
	case LIBUSB_TRANSFER_OVERFLOW:
		return EOVERFLOW;
	}

	return EFAULT;
}

static MPL_THDPROC_RETURN_TYPE MPL_THDPROC_CC async_event_handler(void *arg) 
{
	struct timeval event_timeout_timeval;
	int r;
	int events_locked=0;

	event_timeout_timeval.tv_sec  = ASYNC_TIMVAL_SEC;
	event_timeout_timeval.tv_usec = 0;

	if ((r=Mpl_Event_Wait(&async_thread.event_running, INFINITE)) != MPL_SUCCESS)
	{
		UD_ERR("Mpl_Event_Wait failed. ret=%d\n",r);
		goto Done;
	}

	while (async_thread.is_run > 0) {

		/* acquire the events lock */
		if (events_locked == 0) {
			libusb_lock_events(ctx);
			events_locked = 1;
		}

#ifdef ALLOW_HANDLE_EVENTS_THREAD_IDLE
		if (async_thread.fly_count == 0) {
			libusb_unlock_events(ctx);
			events_locked=0;

			if (async_thread.fly_count == 0)
				Mpl_Event_Wait(&async_thread.event_running, ASYNC_TIMVAL_SEC * 1000);

			continue;
		}
#endif
		/*
		checks that libusb is still happy for your thread to be performing event handling.
		Sometimes, libusb needs to interrupt the event handler.
		*/
		if (!libusb_event_handling_ok(ctx)) {
			libusb_unlock_events(ctx);
			events_locked = 0;
			continue;
		}
		r = libusb_handle_events_locked(ctx, &event_timeout_timeval);
	}
	
	if (events_locked)
		libusb_unlock_events(ctx);

Done:
	if ((r = Mpl_Event_Set(&async_thread.event_terminated)) != MPL_SUCCESS)
	{
		UD_ERR("Mpl_Event_Set failed. ret=%d\n", r);
	}
	UD_INFO("thread user-stopped\n");
	return (MPL_THDPROC_RETURN_TYPE)NULL;
}

static void async_free(usb_async_transfer_t* async_context)
{
	libusb_free_transfer(async_context->transfer);
	Mpl_Event_Free(&async_context->complete_event);
	free(async_context);
}

static int async_dec_ref(usb_async_transfer_t* async_context)
{
	int r = EAGAIN;
	if ((r=MPL_Atomic_Dec32(&async_context->ref_count)) == 0)
	{
		async_free(async_context);
		r = 0;
	} else if (r < 0) {
		UD_ERR("invalid transfer context; possible memory courruption\n");
		return EACCES;
	}

#ifdef ALLOW_HANDLE_EVENTS_THREAD_IDLE
	if (MPL_Atomic_Dec32(&async_thread.fly_count) == 0) {
		Mpl_Event_Reset(&async_thread.event_running);
	}
#endif
	return r;
}

static int async_inc_ref(usb_async_transfer_t* async_context) 
{
	int r;
	if ((r=MPL_Atomic_Inc32(&async_context->ref_count)) < 1)
	{
		MPL_Atomic_Dec32(&async_context->ref_count);
		UD_ERR("transfer is pending de-allocation\n");
		return EACCES;
	}
#ifdef ALLOW_HANDLE_EVENTS_THREAD_IDLE
	else {

		if (MPL_Atomic_Inc32(&async_thread.fly_count) == 1) {
			Mpl_Event_Set(&async_thread.event_running);
		}
	}
#endif
	return 0;
}

static int async_stop_events(unsigned char wait_for_terminate) 
{
	Mpl_Mutex_Wait(&async_thread.init_mutex);
	if (async_thread.is_run)
	{
		MPL_Atomic_Dec32(&async_thread.is_run);
		Mpl_Event_Set(&async_thread.event_running);

		Mpl_Event_Wait(&async_thread.event_terminated, INFINITE);

		Mpl_Event_Reset(&async_thread.event_running);

	}
	Mpl_Mutex_Release(&async_thread.init_mutex);
	return 0;
}

static int async_start_events(void) 
{
	int r = 0;
	if (async_thread.is_run) return 0;

	if ((r = MPL_Atomic_Inc32(&async_thread.is_run)) == 1)
	{
		Mpl_Mutex_Wait(&async_thread.init_mutex);

		/* This thread will run in the background; create it 'detached'. */
		r = Mpl_Thread_Init(&async_thread.handle, async_event_handler, &async_thread);
		if (r == MPL_SUCCESS) {
			Mpl_Event_Set(&async_thread.event_running);
			UD_INFO("thread started.\n");
		} else {
			MPL_Atomic_Dec32(&async_thread.is_run);
			UD_ERR("Mpl_Thread_Init() failed. ret=%d\n",r);
		}

		Mpl_Mutex_Release(&async_thread.init_mutex);

	} else {
		MPL_Atomic_Dec32(&async_thread.is_run);
		r=0;
	}

	return r;
}

/* libusb-1.0 callback proc for all asynchronous bulk and interrupt transfers */
#ifdef _WIN32
static void LIBUSB_CALL async_bulk_cb(struct libusb_transfer *transfer)
#else
static void async_bulk_cb(struct libusb_transfer *transfer)
#endif
{
	usb_async_transfer_t *async_context = (usb_async_transfer_t*)transfer->user_data;

	/* signal the complete event */
	Mpl_Event_Set(&async_context->complete_event);

	/* decrement ref */
	async_dec_ref(async_context);
}

static int async_submit(void *context, char *bytes, int size, unsigned int timeout)
{
	int r;
	usb_async_transfer_t *async_context = (usb_async_transfer_t*)context;
	if (!async_context || (!bytes && size > 0) || async_context->ref_count != 1) return -(errno=EINVAL);

	if (async_context->legacy_iso_pktsize && async_context->transfer->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
		int num_packets = size / async_context->legacy_iso_pktsize;
		int ipacket;
		if (num_packets==0 || num_packets > 1024) {
			UD_ERR("invalid number of iso packets. num_packets=%d\n",num_packets);
			 return -(errno=EINVAL);
		}

		async_context->transfer->num_iso_packets = num_packets;
		for (ipacket=0; ipacket < num_packets; ipacket++)
			async_context->transfer->iso_packet_desc[ipacket].length=async_context->legacy_iso_pktsize;
	}

	r = async_inc_ref(async_context);
	if (r != 0) return -(errno=r);

	async_context->transfer->buffer			= (unsigned char*)&bytes[0];
	async_context->transfer->length			= size;
	async_context->transfer->status			= LIBUSB_TRANSFER_ERROR;
	async_context->transfer->actual_length	= 0;
	async_context->transfer->timeout		= timeout;


	Mpl_Event_Reset(&async_context->complete_event);

	r = libusb_submit_transfer(async_context->transfer);
	if (r != LIBUSB_SUCCESS) {

		async_dec_ref(async_context);
		return compat_err(r);
	}

	return 0;
}

static int async_reap(void *context, int timeout, int cancel_on_timeout)
{
	int r=0;
	usb_async_transfer_t *async_context = (usb_async_transfer_t*)context;
	if (!async_context || async_context->ref_count < 1) return -(errno=EINVAL);

	if (async_inc_ref(async_context) != 0)
	{
		UD_ERR("transfer is pending de-allocation\n");
		return  -(errno=EACCES);
	}
reap_retry_for_cancel:
	r = Mpl_Event_Wait(&async_context->complete_event, timeout);
	if (r == ETIMEDOUT && cancel_on_timeout) {
		usb_cancel_async(context);
		cancel_on_timeout = 0;
		timeout = -1;
		goto reap_retry_for_cancel;
	}

	if (r == MPL_SUCCESS) {

		r = libusb_transfer_to_errno(async_context->transfer->status);
		if (r==0 || (r == ETIMEDOUT && async_context->transfer->actual_length > 0)) {
			async_dec_ref(async_context);
			return async_context->transfer->actual_length;
		}
	}

	async_dec_ref(async_context);
	return -(errno=r);
}

static int usb_setup_async(usb_dev_handle *dev, void **context, unsigned char transfer_type, unsigned char ep, int num_iso_packets)
{
	usb_async_transfer_t *async_context;

	async_context = malloc(sizeof(usb_async_transfer_t));
	if (!async_context) return -(errno=ENOMEM);
	memset(async_context,0,sizeof(usb_async_transfer_t));

	async_context->transfer	= libusb_alloc_transfer(num_iso_packets);
	if (!async_context->transfer) {
		free(async_context);
		return -(errno=ENOMEM);
	}

	Mpl_Event_Init(&async_context->complete_event,0,0);
	async_context->dev = dev;
	async_context->ref_count = 1;
	async_context->transfer->callback = async_bulk_cb;
	async_context->transfer->dev_handle = dev->handle;
	async_context->transfer->endpoint = ep;
	async_context->transfer->timeout = 0;
	async_context->transfer->type = transfer_type;
	async_context->transfer->status = LIBUSB_TRANSFER_ERROR;
	async_context->transfer->user_data = async_context;

	*context = async_context;

	return LIBUSB_SUCCESS;
}

///////////////////////////////////////////////////////////////////
/* libusb0(M)ulti-platform libusb-win32 async compatiblity layer */
///////////////////////////////////////////////////////////////////

API_EXPORTED int USBAPI_DECL usb_bulk_setup_async(usb_dev_handle *dev, void **context, unsigned char ep)
{
	return usb_setup_async(dev, context, LIBUSB_TRANSFER_TYPE_BULK, ep, 0);
}

API_EXPORTED int USBAPI_DECL usb_interrupt_setup_async(usb_dev_handle *dev, void **context, unsigned char ep)
{
	return usb_setup_async(dev, context, LIBUSB_TRANSFER_TYPE_INTERRUPT, ep, 0);
}

API_EXPORTED int USBAPI_DECL usb_isochronous_setup_async(usb_dev_handle *dev, void **context, unsigned char ep, int pktsize)
{
	int r;
	if ((r = usb_setup_async(dev, context, LIBUSB_TRANSFER_TYPE_ISOCHRONOUS, ep, 1024)) == 0)
	{
		usb_async_transfer_t *async_context = (usb_async_transfer_t*)*context;
		async_context->legacy_iso_pktsize = pktsize;
		async_context->transfer->num_iso_packets = 1024;
	}
	return r;
}

API_EXPORTED int USBAPI_DECL usb_submit_async(void *context, char *bytes, int size)
{
	return async_submit(context, bytes, size, 0);
}

API_EXPORTED int USBAPI_DECL usb_reap_async(void *context, int timeout)
{
	return async_reap(context,timeout, 1);
}

API_EXPORTED int USBAPI_DECL usb_reap_async_nocancel(void *context, int timeout)
{
	return async_reap(context,timeout, 0);
}

API_EXPORTED int USBAPI_DECL usb_cancel_async(void *context)
{
	int r;
	usb_async_transfer_t *async_context = (usb_async_transfer_t*)context;
	if (!async_context) return -(errno=EINVAL);

	if (async_context->ref_count > 1) {
		r = libusb_cancel_transfer(async_context->transfer);
		if (r != 0) return compat_err(r);
	}
	return 0;
}

API_EXPORTED int USBAPI_DECL usb_free_async(void **context)
{
	int r;
	usb_async_transfer_t *async_context;
	if (!context || !*context) return -(errno=EINVAL);
	async_context = (usb_async_transfer_t*)*context;
	*context = NULL;
	r = async_dec_ref(async_context);
	if (r!=0) return -(errno=r);

	return 0;
}

/////////////////////////////////////////////////
/* libusb0(M)ulti-platform Extension Functions */
/////////////////////////////////////////////////

API_EXPORTED void USBAPI_DECL usb_exit(void)
{
	if (MPL_Atomic_Dec32(&g_usb0_lib_init_lock) == 0) {

		async_stop_events(1);

		libusb_exit(ctx);
		ctx = NULL;

		Mpl_Event_Free(&async_thread.event_running);
		Mpl_Event_Free(&async_thread.event_terminated);
		Mpl_Mutex_Free(&async_thread.init_mutex);
		Mpl_Free();
	}
}
