#pragma once

// Radio chip selector
// Define RADIO_SX1276 or RADIO_SX1262 in build flags (e.g., -DRADIO_SX1276)
// Defaults to SX1276 if neither is defined (backward compatible)

#include "RadioTypes.h"

#if defined(TEST_NATIVE)
#include "RadioTestStub.h"
typedef RadioTestStub Radio;
#elif defined(RADIO_SX1262)
#include "RadioSX1262.h"
typedef RadioSX1262 Radio;
#else
#if !defined(RADIO_SX1276)
#warning "No radio chip selected (RADIO_SX1276 / RADIO_SX1262). Defaulting to SX1276."
#define RADIO_SX1276
#endif
#include "RadioSX1276.h"
typedef RadioSX1276 Radio;
#endif

