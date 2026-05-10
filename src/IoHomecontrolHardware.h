#pragma once

// Default ESP32 + SX1276 pin assignments
// Override these in the OAM's hardware.h before including IoHomecontrol.h

#ifndef IOHC_SPI_SCK
#define IOHC_SPI_SCK 18
#endif

#ifndef IOHC_SPI_MISO
#define IOHC_SPI_MISO 19
#endif

#ifndef IOHC_SPI_MOSI
#define IOHC_SPI_MOSI 23
#endif

#ifndef IOHC_SPI_CS
#define IOHC_SPI_CS 5
#endif

#ifndef IOHC_RADIO_RST
#define IOHC_RADIO_RST 14
#endif

#ifndef IOHC_RADIO_DIO0
#define IOHC_RADIO_DIO0 26
#endif

#ifndef IOHC_RADIO_DIO4
#define IOHC_RADIO_DIO4 27
#endif

// SX1262-specific pins (DIO1 replaces DIO0, BUSY replaces DIO4)
#if defined(RADIO_SX1262)
#ifndef IOHC_RADIO_DIO1
#define IOHC_RADIO_DIO1 26
#endif

#ifndef IOHC_RADIO_BUSY
#define IOHC_RADIO_BUSY 27
#endif
#endif
