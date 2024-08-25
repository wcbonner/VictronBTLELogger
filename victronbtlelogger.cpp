/////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2024 William C Bonner
//
//	MIT License
//
//	Permission is hereby granted, free of charge, to any person obtaining a copy
//	of this software and associated documentation files(the "Software"), to deal
//	in the Software without restriction, including without limitation the rights
//	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//	copies of the Software, and to permit persons to whom the Software is
//	furnished to do so, subject to the following conditions :
//
//	The above copyright notice and this permission notice shall be included in all
//	copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//	SOFTWARE.
//
/////////////////////////////////////////////////////////////////////////////
 
#include <cstdio>
#include <iostream>
#include <dbus/dbus.h> //  sudo apt install libdbus-1-dev
#include "wimiso8601.h"

/////////////////////////////////////////////////////////////////////////////
#if __has_include("victronbtlelogger-version.h")
#include "victronbtlelogger-version.h"
#endif
#ifndef VictronBTLELogger_VERSION
#define VictronBTLELogger_VERSION "(non-CMake)"
#endif // !VictronBTLELogger_VERSION
/////////////////////////////////////////////////////////////////////////////
static const std::string ProgramVersionString("VictronBTLELogger Version " VictronBTLELogger_VERSION " Built on: " __DATE__ " at " __TIME__);

int ConsoleVerbosity(1);

int main(int argc, char** argv) 
{
	std::cout << "[" << getTimeISO8601(true) << "] " << ProgramVersionString << std::endl;

	DBusError dbus_error;
	dbus_error_init(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#ga8937f0b7cdf8554fa6305158ce453fbe

	// Connect to the system bus
	DBusConnection* dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusBus.html#ga77ba5250adb84620f16007e1b023cf26
	if (dbus_error_is_set(&dbus_error)) // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#gab0ed62e9fc2685897eb2d41467c89405
	{
		std::cout << "[" << getTimeISO8601(true) << "] Error connecting to the D-Bus system bus: " << dbus_error.message << std::endl;
		dbus_error_free(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#gaac6c14ead14829ee4e090f39de6a7568
	}
	else
	{
		if (ConsoleVerbosity > 0)
			std::cout << "[" << getTimeISO8601(true) << "] Connected to D-Bus as \"" << dbus_bus_get_unique_name(dbus_conn) << "\"" << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusBus.html#ga8c10339a1e62f6a2e5752d9c2270d37b
		else
			std::cerr << "Connected to D-Bus as \"" << dbus_bus_get_unique_name(dbus_conn) << "\"" << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusBus.html#ga8c10339a1e62f6a2e5752d9c2270d37b
		// Close the connection. When using the System Bus, unreference the connection instead of closing it
		dbus_connection_unref(dbus_conn);
	}
	std::cerr << ProgramVersionString << " (exiting)" << std::endl;
	return 0;
}