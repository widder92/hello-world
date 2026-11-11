/* USB: Tastaturen und Mäuse an xHCI-Controllern (USB 3) */
#pragma once

/* Sucht USB-Controller und startet für jeden einen Treiber-Task. */
void usb_init(void);
