#pragma once

// Ensure the module version is defined by the OAM's knxprod.h / versions.h
// If not provided, define a fallback for standalone compilation
#ifndef MODULE_IoHomecontrol_Version
#ifdef MODULE_IOHomeControl_Version
// OGM-Common generates this spelling from library.json's IOHomeControl name.
#define MODULE_IoHomecontrol_Version MODULE_IOHomeControl_Version
#else
#define MODULE_IoHomecontrol_Version "0.1.0-dev"
#endif
#endif
