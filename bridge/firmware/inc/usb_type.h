#ifndef __USB_TYPE_H
#define __USB_TYPE_H

#include "debug.h"
#include "usb_conf.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef enum {
    FALSE = 0,
    TRUE = 1
} usb_bool;

#define bool usb_bool

#endif
