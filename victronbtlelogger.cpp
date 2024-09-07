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
 
#include <cfloat>
#include <cstdio>
#include <csignal>
#include <dbus/dbus.h> //  sudo apt install libdbus-1-dev
#include <getopt.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <openssl/evp.h> // sudo apt install libssl-dev
#include <queue>
#include <regex>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <vector>
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
std::filesystem::path LogDirectory;	// If this remains empty, log Files are not created.
std::filesystem::path CacheDirectory;	// If this remains empty, cache Files are not used. Cache Files should greatly speed up startup of the program if logged data runs multiple years over many devices.
std::filesystem::path SVGDirectory;	// If this remains empty, SVG Files are not created. If it's specified, _day, _week, _month, and _year.svg files are created for each bluetooth address seen.
bool SVGFahrenheit(true);
/////////////////////////////////////////////////////////////////////////////
// The following details were taken from https://github.com/oetiker/mrtg
const size_t DAY_COUNT(600);			/* 400 samples is 33.33 hours */
const size_t WEEK_COUNT(600);			/* 400 samples is 8.33 days */
const size_t MONTH_COUNT(600);			/* 400 samples is 33.33 days */
const size_t YEAR_COUNT(2 * 366);		/* 1 sample / day, 366 days, 2 years */
const size_t DAY_SAMPLE(5 * 60);		/* Sample every 5 minutes */
const size_t WEEK_SAMPLE(30 * 60);		/* Sample every 30 minutes */
const size_t MONTH_SAMPLE(2 * 60 * 60);	/* Sample every 2 hours */
const size_t YEAR_SAMPLE(24 * 60 * 60);	/* Sample every 24 hours */
/////////////////////////////////////////////////////////////////////////////
volatile bool bRun = true; // This is declared volatile so that the compiler won't optimized it out of loops later in the code
void SignalHandlerSIGINT(int signal)
{
	bRun = false;
	std::cerr << "***************** SIGINT: Caught Ctrl-C, finishing loop and quitting. *****************" << std::endl;
}
void SignalHandlerSIGHUP(int signal)
{
	bRun = false;
	std::cerr << "***************** SIGHUP: Caught HangUp, finishing loop and quitting. *****************" << std::endl;
}
void SignalHandlerSIGALRM(int signal)
{
	if (ConsoleVerbosity > 0)
		std::cout << "[" << getTimeISO8601(true) << "] ***************** SIGALRM: Caught Alarm. *****************" << std::endl;
}
/////////////////////////////////////////////////////////////////////////////
#ifndef bdaddr_t
/* BD Address */
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;
#endif // !bdaddr_t
bool operator <(const bdaddr_t& a, const bdaddr_t& b)
{
	unsigned long long A = a.b[5];
	A = A << 8 | a.b[4];
	A = A << 8 | a.b[3];
	A = A << 8 | a.b[2];
	A = A << 8 | a.b[1];
	A = A << 8 | a.b[0];
	unsigned long long B = b.b[5];
	B = B << 8 | b.b[4];
	B = B << 8 | b.b[3];
	B = B << 8 | b.b[2];
	B = B << 8 | b.b[1];
	B = B << 8 | b.b[0];
	return(A < B);
}
bool operator ==(const bdaddr_t& a, const bdaddr_t& b)
{
	unsigned long long A = a.b[5];
	A = A << 8 | a.b[4];
	A = A << 8 | a.b[3];
	A = A << 8 | a.b[2];
	A = A << 8 | a.b[1];
	A = A << 8 | a.b[0];
	unsigned long long B = b.b[5];
	B = B << 8 | b.b[4];
	B = B << 8 | b.b[3];
	B = B << 8 | b.b[2];
	B = B << 8 | b.b[1];
	B = B << 8 | b.b[0];
	return(A == B);
}
std::string ba2string(const bdaddr_t& TheBlueToothAddress)
{
	std::ostringstream oss;
	for (auto i = 5; i >= 0; i--)
	{
		oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(TheBlueToothAddress.b[i]);
		if (i > 0)
			oss << ":";
	}
	return (oss.str());
}
const std::regex BluetoothAddressRegex("((([[:xdigit:]]{2}:){5}))[[:xdigit:]]{2}");
bdaddr_t string2ba(const std::string& TheBlueToothAddressString)
{ 
	bdaddr_t TheBlueToothAddress({ 0 });
	if (std::regex_match(TheBlueToothAddressString, BluetoothAddressRegex))
	{
		#ifdef TEST_CODE
		std::stringstream ss1(TheBlueToothAddressString);
		unsigned int test1[6]{ 0 };
		char ignore;
		ss1 >> std::setbase(16)
			>> test1[5] >> ignore
			>> test1[4] >> ignore
			>> test1[3] >> ignore
			>> test1[2] >> ignore
			>> test1[1] >> ignore
			>> test1[0];
		#endif // TEST_CODE

		std::stringstream ss(TheBlueToothAddressString);
		std::string byteString;
		int index(5);
		// Because I've verified the string format with regex I can safely run this loop knowing it'll get 6 bytes
		while (std::getline(ss, byteString, ':'))
			TheBlueToothAddress.b[index--] = std::stoi(byteString, nullptr, 16);
	}
	return(TheBlueToothAddress); 
}
/////////////////////////////////////////////////////////////////////////////
bool ValidateDirectory(const std::filesystem::path& DirectoryName)
{
	bool rval = false;
	// https://linux.die.net/man/2/stat
	struct stat64 StatBuffer;
	if (0 == stat64(DirectoryName.c_str(), &StatBuffer))
		if (S_ISDIR(StatBuffer.st_mode))
		{
			// https://linux.die.net/man/2/access
			if (0 == access(DirectoryName.c_str(), R_OK | W_OK))
				rval = true;
			else
			{
				switch (errno)
				{
				case EACCES:
					std::cerr << DirectoryName << " (" << errno << ") The requested access would be denied to the file, or search permission is denied for one of the directories in the path prefix of pathname." << std::endl;
					break;
				case ELOOP:
					std::cerr << DirectoryName << " (" << errno << ") Too many symbolic links were encountered in resolving pathname." << std::endl;
					break;
				case ENAMETOOLONG:
					std::cerr << DirectoryName << " (" << errno << ") pathname is too long." << std::endl;
					break;
				case ENOENT:
					std::cerr << DirectoryName << " (" << errno << ") A component of pathname does not exist or is a dangling symbolic link." << std::endl;
					break;
				case ENOTDIR:
					std::cerr << DirectoryName << " (" << errno << ") A component used as a directory in pathname is not, in fact, a directory." << std::endl;
					break;
				case EROFS:
					std::cerr << DirectoryName << " (" << errno << ") Write permission was requested for a file on a read-only file system." << std::endl;
					break;
				case EFAULT:
					std::cerr << DirectoryName << " (" << errno << ") pathname points outside your accessible address space." << std::endl;
					break;
				case EINVAL:
					std::cerr << DirectoryName << " (" << errno << ") mode was incorrectly specified." << std::endl;
					break;
				case EIO:
					std::cerr << DirectoryName << " (" << errno << ") An I/O error occurred." << std::endl;
					break;
				case ENOMEM:
					std::cerr << DirectoryName << " (" << errno << ") Insufficient kernel memory was available." << std::endl;
					break;
				case ETXTBSY:
					std::cerr << DirectoryName << " (" << errno << ") Write access was requested to an executable which is being executed." << std::endl;
					break;
				default:
					std::cerr << DirectoryName << " (" << errno << ") An unknown error." << std::endl;
				}
			}
		}
	return(rval);
}
// Create a standardized logfile name for this program based on a Bluetooth address and the global parameter of the log file directory.
std::filesystem::path GenerateLogFileName(const bdaddr_t& a, time_t timer = 0)
{
	std::ostringstream OutputFilename;
	OutputFilename << "victron-";
	std::string btAddress(ba2string(a));
	for (auto pos = btAddress.find(':'); pos != std::string::npos; pos = btAddress.find(':'))
		btAddress.erase(pos, 1);
	OutputFilename << btAddress;
	if (timer == 0)
		time(&timer);
	struct tm UTC;
	if (0 != gmtime_r(&timer, &UTC))
		if (!((UTC.tm_year == 70) && (UTC.tm_mon == 0) && (UTC.tm_mday == 1)))
			OutputFilename << "-" << std::dec << UTC.tm_year + 1900 << "-" << std::setw(2) << std::setfill('0') << UTC.tm_mon + 1;
	OutputFilename << ".txt";
	std::filesystem::path NewFormatFileName(LogDirectory / OutputFilename.str());
	return(NewFormatFileName);
}
/////////////////////////////////////////////////////////////////////////////
std::map<bdaddr_t, std::queue<std::string>> VictronVirtualLog;
std::filesystem::path VictronEncryptionKeyFilename("victronencryptionkeys.txt");
std::map<bdaddr_t, std::string> VictronEncryptionKeys;
bool ReadVictronEncryptionKeys(const std::filesystem::path& VictronEncryptionKeysFilename)
{
	bool rval = false;
	static time_t LastModified = 0;
	struct stat64 VictronEncryptionKeysFileStat;
	VictronEncryptionKeysFileStat.st_mtim.tv_sec = 0;
	if (0 == stat64(VictronEncryptionKeysFilename.c_str(), &VictronEncryptionKeysFileStat))
	{
		rval = true;
		if (VictronEncryptionKeysFileStat.st_mtim.tv_sec > LastModified)	// only read the file if it's modified
		{
			std::ifstream TheFile(VictronEncryptionKeysFilename);
			if (TheFile.is_open())
			{
				LastModified = VictronEncryptionKeysFileStat.st_mtim.tv_sec;	// only update our time if the file is actually read
				if (ConsoleVerbosity > 0)
					std::cout << "[" << getTimeISO8601(true) << "] Reading: " << VictronEncryptionKeysFilename.string() << std::endl;
				else
					std::cerr << "Reading: " << VictronEncryptionKeysFilename.string() << std::endl;
				std::string TheLine;

				while (std::getline(TheFile, TheLine))
				{
					std::smatch BluetoothAddress;
					if (std::regex_search(TheLine, BluetoothAddress, BluetoothAddressRegex))
					{
						bdaddr_t theBlueToothAddress(string2ba(BluetoothAddress.str()));
						const std::string delimiters(" \t");
						auto i = TheLine.find_first_of(delimiters);		// Find first delimiter
						i = TheLine.find_first_not_of(delimiters, i);	// Move past consecutive delimiters
						std::string theEncryptionKey((i == std::string::npos) ? "" : TheLine.substr(i));
						VictronEncryptionKeys.insert(std::make_pair(theBlueToothAddress, theEncryptionKey));
						if (ConsoleVerbosity > 1)
							std::cout << "[                   ] [" << ba2string(theBlueToothAddress) << "] " << theEncryptionKey << std::endl;
					}
				}
				TheFile.close();
			}
		}
	}
	return(rval);
}
bool GenerateLogFile(std::map<bdaddr_t, std::queue<std::string>>& AddressTemperatureMap)
{
	bool rval = false;
	if (!LogDirectory.empty())
	{
		if (ConsoleVerbosity > 1)
			std::cout << "[" << getTimeISO8601() << "] GenerateLogFile: " << LogDirectory << std::endl;
		for (auto it = AddressTemperatureMap.begin(); it != AddressTemperatureMap.end(); ++it)
		{
			if (!it->second.empty()) // Only open the log file if there are entries to add
			{
				std::filesystem::path filename(GenerateLogFileName(it->first));
				std::ofstream LogFile(filename, std::ios_base::out | std::ios_base::app | std::ios_base::ate);
				if (LogFile.is_open())
				{
					//time_t MostRecentData(0);
					while (!it->second.empty())
					{
						LogFile << it->second.front() << std::endl;
						//MostRecentData = std::max(it->second.front().Time, MostRecentData);
						it->second.pop();
					}
					LogFile.close();
					//struct utimbuf Log_ut;
					//Log_ut.actime = MostRecentData;
					//Log_ut.modtime = MostRecentData;
					//utime(filename.c_str(), &Log_ut);
					rval = true;
				}
			}
		}
	}
	else
	{
		// clear the queued data if LogDirectory not specified
		for (auto it = AddressTemperatureMap.begin(); it != AddressTemperatureMap.end(); ++it)
		{
			while (!it->second.empty())
			{
				it->second.pop();
			}
		}
	}
	return(rval);
}
/////////////////////////////////////////////////////////////////////////////
const char* dbus_message_iter_type_to_string(const int type)
{
	switch (type)
	{
	case DBUS_TYPE_INVALID:
		return "Invalid";
	case DBUS_TYPE_VARIANT:
		return "Variant";
	case DBUS_TYPE_ARRAY:
		return "Array";
	case DBUS_TYPE_BYTE:
		return "Byte";
	case DBUS_TYPE_BOOLEAN:
		return "Boolean";
	case DBUS_TYPE_INT16:
		return "Int16";
	case DBUS_TYPE_UINT16:
		return "UInt16";
	case DBUS_TYPE_INT32:
		return "Int32";
	case DBUS_TYPE_UINT32:
		return "UInt32";
	case DBUS_TYPE_INT64:
		return "Int64";
	case DBUS_TYPE_UINT64:
		return "UInt64";
	case DBUS_TYPE_DOUBLE:
		return "Double";
	case DBUS_TYPE_STRING:
		return "String";
	case DBUS_TYPE_OBJECT_PATH:
		return "ObjectPath";
	case DBUS_TYPE_SIGNATURE:
		return "Signature";
	case DBUS_TYPE_STRUCT:
		return "Struct";
	case DBUS_TYPE_DICT_ENTRY:
		return "DictEntry";
	default:
		return "Unknown Type";
	}
}
bool bluez_find_adapters(DBusConnection* dbus_conn, std::map<bdaddr_t, std::string>& AdapterMap)
{
	std::ostringstream ssOutput;
	// Initialize D-Bus error
	DBusError dbus_error;
	dbus_error_init(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#ga8937f0b7cdf8554fa6305158ce453fbe
	DBusMessage* dbus_msg = dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
	if (!dbus_msg)
	{
		if (ConsoleVerbosity > 0)
			ssOutput << "[                   ] ";
		ssOutput << "Can't allocate dbus_message_new_method_call: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
	}
	else
	{
		dbus_error_init(&dbus_error);
		DBusMessage* dbus_reply = dbus_connection_send_with_reply_and_block(dbus_conn, dbus_msg, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
		dbus_message_unref(dbus_msg);
		if (!dbus_reply)
		{
			if (ConsoleVerbosity > 0)
				ssOutput << "[                   ] ";
			ssOutput << "Can't get bluez managed objects" << std::endl;
			if (dbus_error_is_set(&dbus_error))
			{
				if (ConsoleVerbosity > 0)
					ssOutput << "[                   ] ";
				ssOutput << dbus_error.message << std::endl;
				dbus_error_free(&dbus_error);
			}
		}
		else
		{
			if (dbus_message_get_type(dbus_reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
			{
				const std::string dbus_reply_Signature(dbus_message_get_signature(dbus_reply));
				int indent(16);
				if (ConsoleVerbosity > 1)
				{
					ssOutput << "[                   ] " << std::right << std::setw(indent) << "Message Type: " << std::string(dbus_message_type_to_string(dbus_message_get_type(dbus_reply))) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaed63e4c2baaa50d782e8ebb7643def19
					ssOutput << "[                   ] " << std::right << std::setw(indent) << "Signature: " << dbus_reply_Signature << std::endl;
					ssOutput << "[                   ] " << std::right << std::setw(indent) << "Destination: " << std::string(dbus_message_get_destination(dbus_reply)) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaed63e4c2baaa50d782e8ebb7643def19
					ssOutput << "[                   ] " << std::right << std::setw(indent) << "Sender: " << std::string(dbus_message_get_sender(dbus_reply)) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaed63e4c2baaa50d782e8ebb7643def19
					//if (NULL != dbus_message_get_path(dbus_reply)) std::cout << std::right << std::setw(indent) << "Path: " << std::string(dbus_message_get_path(dbus_reply)) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga18adf731bb42d324fe2624407319e4af
					//if (NULL != dbus_message_get_interface(dbus_reply)) std::cout << std::right << std::setw(indent) << "Interface: " << std::string(dbus_message_get_interface(dbus_reply)) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga1ad192bd4538cae556121a71b4e09d42
					//if (NULL != dbus_message_get_member(dbus_reply)) std::cout << std::right << std::setw(indent) << "Member: " << std::string(dbus_message_get_member(dbus_reply)) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaf5c6b705c53db07a5ae2c6b76f230cf9
					//if (NULL != dbus_message_get_container_instance(dbus_reply)) std::cout << std::right << std::setw(indent) << "Container Instance: " << std::string(dbus_message_get_container_instance(dbus_reply)) << std::endl; // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaed63e4c2baaa50d782e8ebb7643def19
				}
				if (!dbus_reply_Signature.compare("a{oa{sa{sv}}}"))
				{
					DBusMessageIter root_iter;
					dbus_message_iter_init(dbus_reply, &root_iter);
					do {
						DBusMessageIter array1_iter;
						dbus_message_iter_recurse(&root_iter, &array1_iter);
						do {
							indent += 4;
							DBusMessageIter dict1_iter;
							dbus_message_iter_recurse(&array1_iter, &dict1_iter);
							DBusBasicValue value;
							dbus_message_iter_get_basic(&dict1_iter, &value);
							std::string dict1_object_path(value.str);
							if (ConsoleVerbosity > 1)
								ssOutput << "[                   ] " << std::right << std::setw(indent) << "Object Path: " << dict1_object_path << std::endl;
							dbus_message_iter_next(&dict1_iter);
							DBusMessageIter array2_iter;
							dbus_message_iter_recurse(&dict1_iter, &array2_iter);
							do
							{
								DBusMessageIter dict2_iter;
								dbus_message_iter_recurse(&array2_iter, &dict2_iter);
								dbus_message_iter_get_basic(&dict2_iter, &value);
								std::string dict2_string(value.str);
								if (ConsoleVerbosity > 1)
									ssOutput << "[                   ] " << std::right << std::setw(indent) << "String: " << dict2_string << std::endl;
								if (!dict2_string.compare("org.bluez.Adapter1"))
								{
									indent += 4;
									dbus_message_iter_next(&dict2_iter);
									DBusMessageIter array3_iter;
									dbus_message_iter_recurse(&dict2_iter, &array3_iter);
									do {
										DBusMessageIter dict3_iter;
										dbus_message_iter_recurse(&array3_iter, &dict3_iter);
										dbus_message_iter_get_basic(&dict3_iter, &value);
										std::string dict3_string(value.str);
										if (!dict3_string.compare("Address"))
										{
											dbus_message_iter_next(&dict3_iter);
											if (DBUS_TYPE_VARIANT == dbus_message_iter_get_arg_type(&dict3_iter))
											{
												// recurse into variant to get string
												DBusMessageIter variant_iter;
												dbus_message_iter_recurse(&dict3_iter, &variant_iter);
												if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&variant_iter))
												{
													dbus_message_iter_get_basic(&variant_iter, &value);
													std::string Address(value.str);
													if (ConsoleVerbosity > 1)
														ssOutput << "[                   ] " << std::right << std::setw(indent) << "Address: " << Address << std::endl;
													bdaddr_t localBTAddress(string2ba(Address));
													AdapterMap.insert(std::pair<bdaddr_t, std::string>(localBTAddress, dict1_object_path));
												}
											}
										}
									} while (dbus_message_iter_next(&array3_iter));
									indent -= 4;
								}
							} while (dbus_message_iter_next(&array2_iter));
							indent -= 4;
						} while (dbus_message_iter_next(&array1_iter));
					} while (dbus_message_iter_next(&root_iter));
				}
			}
			dbus_message_unref(dbus_reply);
		}
	}
	for (const auto& [key, value] : AdapterMap)
	{
		if (ConsoleVerbosity > 0)
			ssOutput << "[                   ] ";
		ssOutput << "Host Controller Address: " << ba2string(key) << " BlueZ Adapter Path: " << value << std::endl;
	}
	if (ConsoleVerbosity > 0)
		std::cout << ssOutput.str();
	else
		std::cerr << ssOutput.str();
	return(!AdapterMap.empty());
}
void bluez_power_on(DBusConnection* dbus_conn, const char* adapter_path, const bool PowerOn = true)
{

	// This was hacked from looking at https://git.kernel.org/pub/scm/network/connman/connman.git/tree/gdbus/client.c#n667
	// https://www.mankier.com/5/org.bluez.Adapter#Interface-boolean_Powered_%5Breadwrite%5D
	DBusMessage* dbus_msg = dbus_message_new_method_call("org.bluez", adapter_path, "org.freedesktop.DBus.Properties", "Set"); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga98ddc82450d818138ef326a284201ee0
	if (!dbus_msg)
	{
		if (ConsoleVerbosity > 0)
			std::cout << "[                   ] Can't allocate dbus_message_new_method_call: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
		else
			std::cerr << "Can't allocate dbus_message_new_method_call: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
	}
	else
	{
		DBusMessageIter iterParameter;
		dbus_message_iter_init_append(dbus_msg, &iterParameter); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaf733047c467ce21f4a53b65a388f1e9d
		const char* adapter = "org.bluez.Adapter1";
		dbus_message_iter_append_basic(&iterParameter, DBUS_TYPE_STRING, &adapter); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga17491f3b75b3203f6fc47dcc2e3b221b
		const char* powered = "Powered";
		dbus_message_iter_append_basic(&iterParameter, DBUS_TYPE_STRING, &powered);
		DBusMessageIter variant;
		dbus_message_iter_open_container(&iterParameter, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &variant); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga943150f4e87fd8507da224d22c266100
		dbus_bool_t cpTrue = PowerOn ? TRUE : FALSE;
		dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &cpTrue);
		dbus_message_iter_close_container(&iterParameter, &variant); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaf00482f63d4af88b7851621d1f24087a
		dbus_connection_send(dbus_conn, dbus_msg, NULL); // https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#gae1cb64f4cf550949b23fd3a756b2f7d0
		if (ConsoleVerbosity > 0)
			std::cout << "[" << getTimeISO8601(true) << "] " << dbus_message_get_path(dbus_msg) << ": " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg) << powered << ": " << std::boolalpha << PowerOn << std::endl;
		else
			std::cerr << dbus_message_get_path(dbus_msg) << ": " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg) << powered << ": " << std::boolalpha << PowerOn << std::endl;
		dbus_message_unref(dbus_msg); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gab69441efe683918f6a82469c8763f464
	}
}
void bluez_filter_le(DBusConnection* dbus_conn, const char* adapter_path, const bool DuplicateData = true, const bool bFilter = true)
{
	std::ostringstream ssOutput;
	// https://www.mankier.com/5/org.bluez.Adapter#Interface-void_SetDiscoveryFilter(dict_filter)
	DBusMessage* dbus_msg = dbus_message_new_method_call("org.bluez", adapter_path, "org.bluez.Adapter1", "SetDiscoveryFilter");
	if (!dbus_msg)
	{
		if (ConsoleVerbosity > 0)
			ssOutput << "[                   ] ";
		ssOutput << "Can't allocate dbus_message_new_method_call: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
	}
	else
	{
		if (bFilter)
		{
			DBusMessageIter iterParameter;
			dbus_message_iter_init_append(dbus_msg, &iterParameter);
			DBusMessageIter iterArray;
			dbus_message_iter_open_container(&iterParameter, DBUS_TYPE_ARRAY, "{sv}", &iterArray);
			DBusMessageIter iterDict;

			dbus_message_iter_open_container(&iterArray, DBUS_TYPE_DICT_ENTRY, NULL, &iterDict);
				const char* cpTransport = "Transport";
				dbus_message_iter_append_basic(&iterDict, DBUS_TYPE_STRING, &cpTransport);
				DBusMessageIter iterVariant;
				dbus_message_iter_open_container(&iterDict, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &iterVariant);
				const char* cpBTLE = "le";
				dbus_message_iter_append_basic(&iterVariant, DBUS_TYPE_STRING, &cpBTLE);
				dbus_message_iter_close_container(&iterDict, &iterVariant);
			dbus_message_iter_close_container(&iterArray, &iterDict);

			dbus_message_iter_open_container(&iterArray, DBUS_TYPE_DICT_ENTRY, NULL, &iterDict);
				const char* cpDuplicateData = "DuplicateData";
				dbus_message_iter_append_basic(&iterDict, DBUS_TYPE_STRING, &cpDuplicateData);
				dbus_message_iter_open_container(&iterDict, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &iterVariant);
				dbus_bool_t cpTrue = DuplicateData ? TRUE : FALSE;
				dbus_message_iter_append_basic(&iterVariant, DBUS_TYPE_BOOLEAN, &cpTrue);
				dbus_message_iter_close_container(&iterDict, &iterVariant);
			dbus_message_iter_close_container(&iterArray, &iterDict);

			dbus_message_iter_open_container(&iterArray, DBUS_TYPE_DICT_ENTRY, NULL, &iterDict);
				const char* cpRSSI = "RSSI";
				dbus_message_iter_append_basic(&iterDict, DBUS_TYPE_STRING, &cpRSSI);
				dbus_message_iter_open_container(&iterDict, DBUS_TYPE_VARIANT, DBUS_TYPE_INT16_AS_STRING, &iterVariant);
				//dbus_int16_t cpRSSIValue = std::numeric_limits<dbus_int16_t>::min();
				dbus_int16_t cpRSSIValue = -100;
				dbus_message_iter_append_basic(&iterVariant, DBUS_TYPE_INT16, &cpRSSIValue);
				dbus_message_iter_close_container(&iterDict, &iterVariant);
			dbus_message_iter_close_container(&iterArray, &iterDict);

			dbus_message_iter_close_container(&iterParameter, &iterArray);
		}
		else
		{
			DBusMessageIter iterParameter;
			dbus_message_iter_init_append(dbus_msg, &iterParameter);
			DBusMessageIter iterArray;
			dbus_message_iter_open_container(&iterParameter, DBUS_TYPE_ARRAY, "{}", &iterArray);
			DBusMessageIter iterDict;
			dbus_message_iter_open_container(&iterArray, DBUS_TYPE_DICT_ENTRY, NULL, &iterDict);
			dbus_message_iter_close_container(&iterArray, &iterDict);
			dbus_message_iter_close_container(&iterParameter, &iterArray);
		}
		// Initialize D-Bus error
		DBusError dbus_error;
		dbus_error_init(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#ga8937f0b7cdf8554fa6305158ce453fbe
		DBusMessage* dbus_reply = dbus_connection_send_with_reply_and_block(dbus_conn, dbus_msg, DBUS_TIMEOUT_INFINITE, &dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#ga8d6431f17a9e53c9446d87c2ba8409f0
		if (ConsoleVerbosity > 0)
			ssOutput << "[" << getTimeISO8601(true) << "] ";
		ssOutput << dbus_message_get_path(dbus_msg) << ": " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg) << std::endl;
		if (!dbus_reply)
		{
			if (ConsoleVerbosity > 0)
				ssOutput << "[                   ] ";
			ssOutput << "Error: " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg);
			if (dbus_error_is_set(&dbus_error))
			{
				ssOutput << ": " << dbus_error.message;
				dbus_error_free(&dbus_error);
			}
			ssOutput << " " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
		}
		else
			dbus_message_unref(dbus_reply);
		dbus_message_unref(dbus_msg);
	}
	if (ConsoleVerbosity > 0)
		std::cout << ssOutput.str();
	else
		std::cerr << ssOutput.str();
}
bool bluez_discovery(DBusConnection* dbus_conn, const char* adapter_path, const bool bStartDiscovery = true)
{
	bool bStarted = false;
	// https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/adapter-api.txt
	DBusMessage* dbus_msg = dbus_message_new_method_call("org.bluez", adapter_path, "org.bluez.Adapter1", bStartDiscovery ? "StartDiscovery" : "StopDiscovery");
	if (!dbus_msg)
	{
		if (ConsoleVerbosity > 0)
			std::cout << "[                   ] Can't allocate dbus_message_new_method_call: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
		else
			std::cerr << "Can't allocate dbus_message_new_method_call: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
	}
	else
	{
		DBusError dbus_error;
		dbus_error_init(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#ga8937f0b7cdf8554fa6305158ce453fbe
		DBusMessage* dbus_reply = dbus_connection_send_with_reply_and_block(dbus_conn, dbus_msg, DBUS_TIMEOUT_INFINITE, &dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#ga8d6431f17a9e53c9446d87c2ba8409f0
		if (ConsoleVerbosity > 0)
			std::cout << "[" << getTimeISO8601(true) << "] " << dbus_message_get_path(dbus_msg) << ": " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg) << std::endl;
		else
			std::cerr << dbus_message_get_path(dbus_msg) << ": " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg) << std::endl;
		if (!dbus_reply)
		{
			std::cout << __FILE__ << "(" << __LINE__ << "): Error: " << dbus_message_get_interface(dbus_msg) << ": " << dbus_message_get_member(dbus_msg);
			if (dbus_error_is_set(&dbus_error))
			{
				std::cout << ": " << dbus_error.message;
				dbus_error_free(&dbus_error);
			}
			std::cout << std::endl;
		}
		else
		{
			bStarted = true;
			dbus_message_unref(dbus_reply);
		}
		dbus_message_unref(dbus_msg);
	}
	return(bStarted);
}
/////////////////////////////////////////////////////////////////////////////
union VictronExtraData_t {
	uint8_t rawbytes[20];
	struct __attribute__((packed))
	{
		unsigned int device_state : 8;
		unsigned int charger_error : 8;
		int battery_voltage : 16;
		int battery_current : 16;
		unsigned int yield_today : 16;
		unsigned int pv_power : 16;
		unsigned int load_current : 9;
		//unsigned int unused : 39;
	} SolarCharger;  // 0x01
	struct __attribute__((packed))
	{
		unsigned int device_state : 8;
		unsigned int charger_error : 8;
		unsigned int input_voltage : 16;
		unsigned int output_voltage : 16;
		unsigned int off_reason : 32;
		//unsigned int unused : 48;
	} DCDCConverter; // 0x04
	struct __attribute__((packed))
	{
		unsigned int bms_flags : 32;
		unsigned int smartlithium_error : 16;
		unsigned int cell_1 : 7;
		unsigned int cell_2 : 7;
		unsigned int cell_3 : 7;
		unsigned int cell_4 : 7;
		unsigned int cell_5 : 7;
		unsigned int cell_6 : 7;
		unsigned int cell_7 : 7;
		unsigned int cell_8 : 7;
		unsigned int battery_voltage : 12;
		unsigned int balancer_status : 4;
		unsigned int battery_temperature : 7;
		//unsigned int unused : 1;
	} SmartLithium;  // 0x05
	struct __attribute__((packed))
	{
		unsigned int error : 8;
		unsigned int ttg : 16;
		unsigned int battery_voltage : 16;
		unsigned int battery_current : 16;
		unsigned int io_status : 16;
		unsigned int warnings : 18;
		unsigned int soc : 10;
		unsigned int consumed_ah : 20;
		unsigned int temperature : 7;
		//unsigned int unused : 1;
	} LynxSmartBMS; // 0x0a
	struct __attribute__((packed))
	{
		unsigned int device_state : 8;
		unsigned int charger_error : 8;
		unsigned int output_voltage : 16;
		unsigned int output_current : 16;
		unsigned int input_voltage : 16;
		unsigned int input_current : 16;
		unsigned int off_reason : 32;
		//unsigned int unused : 16;
	} OrionXS; // 0x0F
};
/////////////////////////////////////////////////////////////////////////////
class VictronSmartLithium
{
public:
	VictronSmartLithium() : Time(0), Cell { 0 }, Voltage(0), Temperature(0), TemperatureMin(DBL_MAX), TemperatureMax(-DBL_MAX), Averages(0) { };
	VictronSmartLithium(const std::string& data); // This is for reading from log file
	time_t Time;
	bool ReadManufacturerData(const std::vector<uint8_t> & ManufacturerData, const time_t newtime = 0);
	bool ReadManufacturerData(const std::string& data, const time_t newtime = 0);
	std::string WriteConsole(void) const;
	std::string WriteCache(void) const;
	bool ReadCache(const std::string& data);
	bool IsValid(void) const { return(Averages > 0); };
	enum granularity { day, week, month, year };
	void NormalizeTime(granularity type);
	granularity GetTimeGranularity(void) const;
	VictronSmartLithium& operator +=(const VictronSmartLithium& b);
	unsigned GetCellCount(void) const { return (4); }; //TODO: calculate this
	double GetCellVoltage(const unsigned index) const { return(Cell[std::max(index, GetCellCount()-1)]); };
	double GetVoltage(void) const { return(Voltage); };
	double GetTemperature(const bool Fahrenheit = false) const { if (Fahrenheit) return((Temperature * 9.0 / 5.0) + 32.0); return(Temperature); };
	double GetTemperatureMin(const bool Fahrenheit = false) const { if (Fahrenheit) return(std::min(((Temperature * 9.0 / 5.0) + 32.0), ((TemperatureMin * 9.0 / 5.0) + 32.0))); return(std::min(Temperature, TemperatureMin)); };
	double GetTemperatureMax(const bool Fahrenheit = false) const { if (Fahrenheit) return(std::max(((Temperature * 9.0 / 5.0) + 32.0), ((TemperatureMax * 9.0 / 5.0) + 32.0))); return(std::max(Temperature, TemperatureMax)); };
protected:
	double Cell[8];
	double Voltage;
	double Temperature;
	double TemperatureMin;
	double TemperatureMax;
	int Averages;
};
VictronSmartLithium::VictronSmartLithium(const std::string& data)
{
	for (auto& a : Cell)
		a = 0;
	Averages = 0;
	std::istringstream TheLine(data);
	// erase any nulls from the data. these are occasionally in the log file when the platform crashed during a write to the logfile.
	while (TheLine.peek() == '\000')
		TheLine.get();
	std::string theDate;
	TheLine >> theDate;
	Time = ISO8601totime(theDate);
	std::string ManufacturerData;
	TheLine >> ManufacturerData;
	ReadManufacturerData(ManufacturerData);
}
bool VictronSmartLithium::ReadManufacturerData(const std::vector<uint8_t>& ManufacturerData, const time_t newtime)
{
	bool rval = false;
	if (ManufacturerData.size() >= 8 + sizeof(VictronExtraData_t::SmartLithium)) // Make sure data is big enough to be valid
	{
		if ((ManufacturerData[4] == 0x05) && // make sure it's a smartlithium device
			(ManufacturerData[5] == 0) &&
			(ManufacturerData[6] == 0) &&
			(ManufacturerData[7] == 0)) // make sure it's already been decrypted
		{
			if (newtime != 0)
				Time = newtime;
			VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data() + 8);
			if (ExtraDataPtr->SmartLithium.cell_1 != 0x7f) Cell[0] = double(ExtraDataPtr->SmartLithium.cell_1) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_2 != 0x7f) Cell[1] = double(ExtraDataPtr->SmartLithium.cell_2) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_3 != 0x7f) Cell[2] = double(ExtraDataPtr->SmartLithium.cell_3) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_4 != 0x7f) Cell[3] = double(ExtraDataPtr->SmartLithium.cell_4) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_5 != 0x7f) Cell[4] = double(ExtraDataPtr->SmartLithium.cell_5) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_6 != 0x7f) Cell[5] = double(ExtraDataPtr->SmartLithium.cell_6) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_7 != 0x7f) Cell[6] = double(ExtraDataPtr->SmartLithium.cell_7) * 0.01 + 2.60;
			if (ExtraDataPtr->SmartLithium.cell_8 != 0x7f) Cell[7] = double(ExtraDataPtr->SmartLithium.cell_8) * 0.01 + 2.60;
			Voltage = double(ExtraDataPtr->SmartLithium.battery_voltage) * 0.01;
			Temperature = ExtraDataPtr->SmartLithium.battery_temperature - 40;
			TemperatureMin = TemperatureMax = Temperature;
			Averages = 1;
			rval = true;
		}
	}
	return(rval);
}
bool VictronSmartLithium::ReadManufacturerData(const std::string& data, const time_t newtime)
{
	std::vector<uint8_t> ManufacturerData;
	for (auto i = 0; i < data.length(); i += 2) 
		ManufacturerData.push_back(std::stoi(data.substr(i, 2), nullptr, 16));
	return(ReadManufacturerData(ManufacturerData, newtime));
}
std::string VictronSmartLithium::WriteConsole(void) const
{
	std::ostringstream ssValue;
	ssValue << " (SmartLithium)";
	for (auto& a : Cell)
		if (a != 0)
			ssValue << " Cell: " << a << "V";
	ssValue << " Voltage: " << Voltage << "V";
	ssValue << " Temperature: " << Temperature << "\u00B0" << "C";
	return(ssValue.str());
}
std::string VictronSmartLithium::WriteCache(void) const
{
	std::ostringstream ssValue;
	ssValue << Time;
	ssValue << "\t" << Averages;
	for (auto& a : Cell)
		ssValue << "\t" << a;
	ssValue << "\t" << Voltage;
	ssValue << "\t" << Temperature;
	ssValue << "\t" << TemperatureMin;
	ssValue << "\t" << TemperatureMax;
	return(ssValue.str());
}
bool VictronSmartLithium::ReadCache(const std::string& data)
{
	bool rval = false;
	std::istringstream ssValue(data);
	ssValue >> Time;
	ssValue >> Averages;
	for (auto& a : Cell)
		ssValue >> a;
	ssValue >> Voltage;
	ssValue >> Temperature;
	ssValue >> TemperatureMin;
	ssValue >> TemperatureMax;
	return(rval);
}
void VictronSmartLithium::NormalizeTime(granularity type)
{
	if (type == day)
		Time = (Time / DAY_SAMPLE) * DAY_SAMPLE;
	else if (type == week)
		Time = (Time / WEEK_SAMPLE) * WEEK_SAMPLE;
	else if (type == month)
		Time = (Time / MONTH_SAMPLE) * MONTH_SAMPLE;
	else if (type == year)
	{
		struct tm UTC;
		if (0 != localtime_r(&Time, &UTC))
		{
			UTC.tm_hour = 0;
			UTC.tm_min = 0;
			UTC.tm_sec = 0;
			Time = mktime(&UTC);
		}
	}
}
VictronSmartLithium::granularity VictronSmartLithium::GetTimeGranularity(void) const
{
	granularity rval = granularity::day;
	struct tm UTC;
	if (0 != localtime_r(&Time, &UTC))
	{
		//if (((UTC.tm_hour == 0) && (UTC.tm_min == 0)) || ((UTC.tm_hour == 23) && (UTC.tm_min == 0) && (UTC.tm_isdst == 1)))
		if ((UTC.tm_hour == 0) && (UTC.tm_min == 0))
			rval = granularity::year;
		else if ((UTC.tm_hour % 2 == 0) && (UTC.tm_min == 0))
			rval = granularity::month;
		else if ((UTC.tm_min == 0) || (UTC.tm_min == 30))
			rval = granularity::week;
	}
	return(rval);
}
VictronSmartLithium& VictronSmartLithium::operator +=(const VictronSmartLithium& b)
{
	if (b.IsValid())
	{
		Time = std::max(Time, b.Time); // Use the maximum time (newest time)
		for (unsigned long index = 0; index < (sizeof(Cell) / sizeof(Cell[0])); index++)
			Cell[index] = ((Cell[index] * Averages) + (b.Cell[index] * b.Averages)) / (Averages + b.Averages);
		Voltage = ((Voltage * Averages) + (b.Voltage * b.Averages)) / (Averages + b.Averages);
		Temperature = ((Temperature * Averages) + (b.Temperature * b.Averages)) / (Averages + b.Averages);
		TemperatureMin = std::min(std::min(Temperature, TemperatureMin), b.TemperatureMin);
		TemperatureMax = std::max(std::max(Temperature, TemperatureMax), b.TemperatureMax);
		Averages += b.Averages; // existing average + new average
	}
	return(*this);
}
/////////////////////////////////////////////////////////////////////////////
class VictronOrionXS
{
public:
	VictronOrionXS() : Time(0), OutputVoltage(0), OutputCurrent(0), InputVoltage(0), InputCurrent(0), Averages(0) { };
	VictronOrionXS(const std::string& data); // This is for reading from log file
	time_t Time;
	bool ReadManufacturerData(const std::vector<uint8_t>& ManufacturerData, const time_t newtime = 0);
	bool ReadManufacturerData(const std::string& data, const time_t newtime = 0);
	std::string WriteConsole(void) const;
	std::string WriteCache(void) const;
	bool ReadCache(const std::string& data);
	bool IsValid(void) const { return(Averages > 0); };
	enum granularity { day, week, month, year };
	void NormalizeTime(granularity type);
	granularity GetTimeGranularity(void) const;
	VictronOrionXS& operator +=(const VictronOrionXS& b);
protected:
	double OutputVoltage;
	double OutputCurrent;
	double InputVoltage;
	double InputCurrent;
	int Averages;
};
VictronOrionXS::VictronOrionXS(const std::string& data) : Time(0), OutputVoltage(0), OutputCurrent(0), InputVoltage(0), InputCurrent(0), Averages(0)
{
	//Averages = 0;
	std::istringstream TheLine(data);
	// erase any nulls from the data. these are occasionally in the log file when the platform crashed during a write to the logfile.
	while (TheLine.peek() == '\000')
		TheLine.get();
	std::string theDate;
	TheLine >> theDate;
	Time = ISO8601totime(theDate);
	std::string ManufacturerData;
	TheLine >> ManufacturerData;
	ReadManufacturerData(ManufacturerData);
}
bool VictronOrionXS::ReadManufacturerData(const std::vector<uint8_t>& ManufacturerData, const time_t newtime)
{
	bool rval = false;
	if (ManufacturerData.size() >= 8 + sizeof(VictronExtraData_t::OrionXS)) // Make sure data is big enough to be valid
	{
		if ((ManufacturerData[4] == 0x0f) && // make sure it's an Orion XS
			(ManufacturerData[5] == 0) &&
			(ManufacturerData[6] == 0) &&
			(ManufacturerData[7] == 0)) // make sure it's already been decrypted
		{
			if (newtime != 0)
				Time = newtime;
			VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data() + 8);
			if (ExtraDataPtr->OrionXS.output_voltage != 0x7FFF) OutputVoltage = double(ExtraDataPtr->OrionXS.output_voltage) * 0.01;
			if (ExtraDataPtr->OrionXS.output_current != 0x7FFF) OutputCurrent = double(ExtraDataPtr->OrionXS.output_current) * 0.01;
			if (ExtraDataPtr->OrionXS.input_voltage != 0xFFFF) InputVoltage = double(ExtraDataPtr->OrionXS.input_voltage) * 0.01;
			if (ExtraDataPtr->OrionXS.input_current != 0xFFFF) InputCurrent = double(ExtraDataPtr->OrionXS.input_current) * 0.01;
			Averages = 1;
			rval = true;
		}
	}
	return(rval);
}
bool VictronOrionXS::ReadManufacturerData(const std::string& data, const time_t newtime)
{
	std::vector<uint8_t> ManufacturerData;
	for (auto i = 0; i < data.length(); i += 2)
		ManufacturerData.push_back(std::stoi(data.substr(i, 2), nullptr, 16));
	return(ReadManufacturerData(ManufacturerData, newtime));
}
std::string VictronOrionXS::WriteConsole(void) const
{
	std::ostringstream ssValue;
	ssValue << " (Orion XS)";
	ssValue << " OutputVoltage: " << OutputVoltage << "V";
	ssValue << " OutputCurrent: " << OutputCurrent << "A";
	ssValue << " InputVoltage: " << InputVoltage << "V";
	ssValue << " InputCurrent: " << InputCurrent << "A";
	return(ssValue.str());
}
std::string VictronOrionXS::WriteCache(void) const
{
	std::ostringstream ssValue;
	ssValue << Time;
	ssValue << "\t" << Averages;
	ssValue << "\t" << OutputVoltage;
	ssValue << "\t" << OutputCurrent;
	ssValue << "\t" << InputVoltage;
	ssValue << "\t" << InputCurrent;
	return(ssValue.str());
}
bool VictronOrionXS::ReadCache(const std::string& data)
{
	bool rval = false;
	std::istringstream ssValue(data);
	ssValue >> Time;
	ssValue >> Averages;
	ssValue >> OutputVoltage;
	ssValue >> OutputCurrent;
	ssValue >> InputVoltage;
	ssValue >> InputCurrent;
	return(rval);
}
void VictronOrionXS::NormalizeTime(granularity type)
{
	if (type == day)
		Time = (Time / DAY_SAMPLE) * DAY_SAMPLE;
	else if (type == week)
		Time = (Time / WEEK_SAMPLE) * WEEK_SAMPLE;
	else if (type == month)
		Time = (Time / MONTH_SAMPLE) * MONTH_SAMPLE;
	else if (type == year)
	{
		struct tm UTC;
		if (0 != localtime_r(&Time, &UTC))
		{
			UTC.tm_hour = 0;
			UTC.tm_min = 0;
			UTC.tm_sec = 0;
			Time = mktime(&UTC);
		}
	}
}
VictronOrionXS::granularity VictronOrionXS::GetTimeGranularity(void) const
{
	granularity rval = granularity::day;
	struct tm UTC;
	if (0 != localtime_r(&Time, &UTC))
	{
		//if (((UTC.tm_hour == 0) && (UTC.tm_min == 0)) || ((UTC.tm_hour == 23) && (UTC.tm_min == 0) && (UTC.tm_isdst == 1)))
		if ((UTC.tm_hour == 0) && (UTC.tm_min == 0))
			rval = granularity::year;
		else if ((UTC.tm_hour % 2 == 0) && (UTC.tm_min == 0))
			rval = granularity::month;
		else if ((UTC.tm_min == 0) || (UTC.tm_min == 30))
			rval = granularity::week;
	}
	return(rval);
}
VictronOrionXS& VictronOrionXS::operator +=(const VictronOrionXS& b)
{
	if (b.IsValid())
	{
		Time = std::max(Time, b.Time); // Use the maximum time (newest time)
		OutputVoltage = ((OutputVoltage * Averages) + (b.OutputVoltage * b.Averages)) / (Averages + b.Averages);
		OutputCurrent = ((OutputCurrent * Averages) + (b.OutputCurrent * b.Averages)) / (Averages + b.Averages);
		InputVoltage = ((InputVoltage * Averages) + (b.InputVoltage * b.Averages)) / (Averages + b.Averages);
		InputCurrent = ((InputCurrent * Averages) + (b.InputCurrent * b.Averages)) / (Averages + b.Averages);
		Averages += b.Averages; // existing average + new average
	}
	return(*this);
}
/////////////////////////////////////////////////////////////////////////////
std::map<bdaddr_t, std::vector<VictronSmartLithium>> VictronSmartLithiumMRTGLogs; // memory map of BT addresses and vector structure similar to MRTG Log Files
std::map<bdaddr_t, std::vector<VictronOrionXS>> VictronOrionXSMRTGLogs; // memory map of BT addresses and vector structure similar to MRTG Log Files
std::map<bdaddr_t, std::string> VictronNames;
void UpdateMRTGData(const bdaddr_t& TheAddress, VictronSmartLithium& TheValue)
{
	std::vector<VictronSmartLithium> foo;
	auto ret = VictronSmartLithiumMRTGLogs.insert(std::pair<bdaddr_t, std::vector<VictronSmartLithium>>(TheAddress, foo));
	std::vector<VictronSmartLithium>& FakeMRTGFile = ret.first->second;
	if (FakeMRTGFile.empty())
	{
		FakeMRTGFile.resize(2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT);
		FakeMRTGFile[0] = TheValue;	// current value
		FakeMRTGFile[1] = TheValue;
		for (auto index = 0; index < DAY_COUNT; index++)
			FakeMRTGFile[index + 2].Time = FakeMRTGFile[index + 1].Time - DAY_SAMPLE;
		for (auto index = 0; index < WEEK_COUNT; index++)
			FakeMRTGFile[index + 2 + DAY_COUNT].Time = FakeMRTGFile[index + 1 + DAY_COUNT].Time - WEEK_SAMPLE;
		for (auto index = 0; index < MONTH_COUNT; index++)
			FakeMRTGFile[index + 2 + DAY_COUNT + WEEK_COUNT].Time = FakeMRTGFile[index + 1 + DAY_COUNT + WEEK_COUNT].Time - MONTH_SAMPLE;
		for (auto index = 0; index < YEAR_COUNT; index++)
			FakeMRTGFile[index + 2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT].Time = FakeMRTGFile[index + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT].Time - YEAR_SAMPLE;
	}
	else
	{
		if (TheValue.Time > FakeMRTGFile[0].Time)
		{
			FakeMRTGFile[0] = TheValue;	// current value
			FakeMRTGFile[1] += TheValue; // averaged value up to DAY_SAMPLE size
		}
	}
	bool ZeroAccumulator = false;
	auto DaySampleFirst = FakeMRTGFile.begin() + 2;
	auto DaySampleLast = FakeMRTGFile.begin() + 1 + DAY_COUNT;
	auto WeekSampleFirst = FakeMRTGFile.begin() + 2 + DAY_COUNT;
	auto WeekSampleLast = FakeMRTGFile.begin() + 1 + DAY_COUNT + WEEK_COUNT;
	auto MonthSampleFirst = FakeMRTGFile.begin() + 2 + DAY_COUNT + WEEK_COUNT;
	auto MonthSampleLast = FakeMRTGFile.begin() + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT;
	auto YearSampleFirst = FakeMRTGFile.begin() + 2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT;
	auto YearSampleLast = FakeMRTGFile.begin() + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT;
	// For every time difference between FakeMRTGFile[1] and FakeMRTGFile[2] that's greater than DAY_SAMPLE we shift that data towards the back.
	while (difftime(FakeMRTGFile[1].Time, DaySampleFirst->Time) > DAY_SAMPLE)
	{
		ZeroAccumulator = true;
		// shuffle all the day samples toward the end
		std::copy_backward(DaySampleFirst, DaySampleLast - 1, DaySampleLast);
		*DaySampleFirst = FakeMRTGFile[1];
		DaySampleFirst->NormalizeTime(VictronSmartLithium::granularity::day);
		if (difftime(DaySampleFirst->Time, (DaySampleFirst + 1)->Time) > DAY_SAMPLE)
			DaySampleFirst->Time = (DaySampleFirst + 1)->Time + DAY_SAMPLE;
		if (DaySampleFirst->GetTimeGranularity() == VictronSmartLithium::granularity::year)
		{
			if (ConsoleVerbosity > 2)
				std::cout << "[" << getTimeISO8601() << "] shuffling year " << timeToExcelLocal(DaySampleFirst->Time) << " > " << timeToExcelLocal(YearSampleFirst->Time) << std::endl;
			// shuffle all the year samples toward the end
			std::copy_backward(YearSampleFirst, YearSampleLast - 1, YearSampleLast);
			*YearSampleFirst = VictronSmartLithium();
			for (auto iter = DaySampleFirst; (iter->IsValid() && ((iter - DaySampleFirst) < (12 * 24))); iter++) // One Day of day samples
				*YearSampleFirst += *iter;
		}
		if ((DaySampleFirst->GetTimeGranularity() == VictronSmartLithium::granularity::year) ||
			(DaySampleFirst->GetTimeGranularity() == VictronSmartLithium::granularity::month))
		{
			if (ConsoleVerbosity > 2)
				std::cout << "[" << getTimeISO8601() << "] shuffling month " << timeToExcelLocal(DaySampleFirst->Time) << std::endl;
			// shuffle all the month samples toward the end
			std::copy_backward(MonthSampleFirst, MonthSampleLast - 1, MonthSampleLast);
			*MonthSampleFirst = VictronSmartLithium();
			for (auto iter = DaySampleFirst; (iter->IsValid() && ((iter - DaySampleFirst) < (12 * 2))); iter++) // two hours of day samples
				*MonthSampleFirst += *iter;
		}
		if ((DaySampleFirst->GetTimeGranularity() == VictronSmartLithium::granularity::year) ||
			(DaySampleFirst->GetTimeGranularity() == VictronSmartLithium::granularity::month) ||
			(DaySampleFirst->GetTimeGranularity() == VictronSmartLithium::granularity::week))
		{
			if (ConsoleVerbosity > 2)
				std::cout << "[" << getTimeISO8601() << "] shuffling week " << timeToExcelLocal(DaySampleFirst->Time) << std::endl;
			// shuffle all the month samples toward the end
			std::copy_backward(WeekSampleFirst, WeekSampleLast - 1, WeekSampleLast);
			*WeekSampleFirst = VictronSmartLithium();
			for (auto iter = DaySampleFirst; (iter->IsValid() && ((iter - DaySampleFirst) < 6)); iter++) // Half an hour of day samples
				*WeekSampleFirst += *iter;
		}
	}
	if (ZeroAccumulator)
		FakeMRTGFile[1] = VictronSmartLithium();
}
enum class GraphType { daily, weekly, monthly, yearly };
// Returns a curated vector of data points specific to the requested graph type from the internal memory structure map keyed off the Bluetooth address.
void ReadMRTGData(const bdaddr_t& TheAddress, std::vector<VictronSmartLithium>& TheValues, const GraphType graph = GraphType::daily)
{
	auto it = VictronSmartLithiumMRTGLogs.find(TheAddress);
	if (it != VictronSmartLithiumMRTGLogs.end())
	{
		if (it->second.size() > 0)
		{
			auto DaySampleFirst = it->second.begin() + 2;
			auto DaySampleLast = it->second.begin() + 1 + DAY_COUNT;
			auto WeekSampleFirst = it->second.begin() + 2 + DAY_COUNT;
			auto WeekSampleLast = it->second.begin() + 1 + DAY_COUNT + WEEK_COUNT;
			auto MonthSampleFirst = it->second.begin() + 2 + DAY_COUNT + WEEK_COUNT;
			auto MonthSampleLast = it->second.begin() + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT;
			auto YearSampleFirst = it->second.begin() + 2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT;
			auto YearSampleLast = it->second.begin() + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT;
			if (graph == GraphType::daily)
			{
				TheValues.resize(DAY_COUNT);
				std::copy(DaySampleFirst, DaySampleLast, TheValues.begin());
				auto iter = TheValues.begin();
				while (iter->IsValid() && (iter != TheValues.end()))
					iter++;
				TheValues.resize(iter - TheValues.begin());
				TheValues.begin()->Time = it->second.begin()->Time; //HACK: include the most recent time sample
			}
			else if (graph == GraphType::weekly)
			{
				TheValues.resize(WEEK_COUNT);
				std::copy(WeekSampleFirst, WeekSampleLast, TheValues.begin());
				auto iter = TheValues.begin();
				while (iter->IsValid() && (iter != TheValues.end()))
					iter++;
				TheValues.resize(iter - TheValues.begin());
			}
			else if (graph == GraphType::monthly)
			{
				TheValues.resize(MONTH_COUNT);
				std::copy(MonthSampleFirst, MonthSampleLast, TheValues.begin());
				auto iter = TheValues.begin();
				while (iter->IsValid() && (iter != TheValues.end()))
					iter++;
				TheValues.resize(iter - TheValues.begin());
			}
			else if (graph == GraphType::yearly)
			{
				TheValues.resize(YEAR_COUNT);
				std::copy(YearSampleFirst, YearSampleLast, TheValues.begin());
				auto iter = TheValues.begin();
				while (iter->IsValid() && (iter != TheValues.end()))
					iter++;
				TheValues.resize(iter - TheValues.begin());
			}
		}
	}
}
/////////////////////////////////////////////////////////////////////////////
void WriteSVG(std::vector<VictronSmartLithium>& TheValues, const std::filesystem::path& SVGFileName, const std::string& Title = "", const GraphType graph = GraphType::daily, const bool Fahrenheit = true, const bool DarkStyle = false)
{
	const bool DrawVoltage = true;
	if (!TheValues.empty())
	{
		// By declaring these items here, I'm then basing all my other dimensions on these
		const int SVGWidth(500);
		const int SVGHeight(135);
		const int FontSize(12);
		const int TickSize(2);
		int GraphWidth = SVGWidth - (FontSize * 5);
		struct stat64 SVGStat({ 0 });	// Zero the stat64 structure on allocation
		if (-1 == stat64(SVGFileName.c_str(), &SVGStat))
			if (ConsoleVerbosity > 3)
				std::cout << "[" << getTimeISO8601(true) << "] " << std::strerror(errno) << ": " << SVGFileName << std::endl;
		if (TheValues.begin()->Time > SVGStat.st_mtim.tv_sec)	// only write the file if we have new data
		{
			std::ofstream SVGFile(SVGFileName);
			if (SVGFile.is_open())
			{
				if (ConsoleVerbosity > 0)
					std::cout << "[" << getTimeISO8601() << "] Writing: " << SVGFileName.string() << " With Title: " << Title << std::endl;
				else
					std::cerr << "Writing: " << SVGFileName.string() << " With Title: " << Title << std::endl;
				std::ostringstream tempOString;
				tempOString << "Temperature (" << std::fixed << std::setprecision(1) << TheValues[0].GetTemperature(Fahrenheit) << "\u00B0" << (Fahrenheit ? "F)" : "C)");
				std::string YLegendTemperature(tempOString.str());
				tempOString = std::ostringstream();
				tempOString << "Voltage (" << TheValues[0].GetVoltage() << "V)";
				std::string YLegendVoltage(tempOString.str());
				int GraphTop = FontSize + TickSize;
				int GraphBottom = SVGHeight - GraphTop;
				int GraphRight = SVGWidth - GraphTop;
				if (DrawVoltage) // this also has to add the secondary axis
				{
					GraphWidth -= FontSize * 2;
					GraphRight -= FontSize + TickSize * 2;
				}
				int GraphLeft = GraphRight - GraphWidth;
				int GraphVerticalDivision = (GraphBottom - GraphTop) / 4;
				double TempMin = DBL_MAX;
				double TempMax = -DBL_MAX;
				double VoltMin = DBL_MAX;
				double VoltMax = -DBL_MAX;
				for (auto index = 0; index < (GraphWidth < TheValues.size() ? GraphWidth : TheValues.size()); index++)
				{
					TempMin = std::min(TempMin, TheValues[index].GetTemperature(Fahrenheit));
					TempMax = std::max(TempMax, TheValues[index].GetTemperature(Fahrenheit));
					VoltMin = std::min(VoltMin, TheValues[index].GetVoltage());
					for (auto cell = 0; cell < (TheValues[index].GetCellCount() - 1); cell++)
						VoltMin = std::min(VoltMin, TheValues[index].GetCellVoltage(cell));
					VoltMax = std::max(VoltMax, TheValues[index].GetVoltage());
					for (auto cell = 0; cell < (TheValues[index].GetCellCount() - 1); cell++)
						VoltMax = std::max(VoltMax, TheValues[index].GetCellVoltage(cell));
				}

				double TempVerticalDivision = (TempMax - TempMin) / 4;
				double TempVerticalFactor = (GraphBottom - GraphTop) / (TempMax - TempMin);
				double VoltVerticalDivision = (VoltMax - VoltMin) / 4;
				double VoltVerticalFactor = (GraphBottom - GraphTop) / (VoltMax - VoltMin);
				int FreezingLine = 0; // outside the range of the graph
				if (Fahrenheit)
				{
					if ((TempMin < 32) && (32 < TempMax))
						FreezingLine = ((TempMax - 32.0) * TempVerticalFactor) + GraphTop;
				}
				else
				{
					if ((TempMin < 0) && (0 < TempMax))
						FreezingLine = (TempMax * TempVerticalFactor) + GraphTop;
				}

				SVGFile << "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>" << std::endl;
				SVGFile << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" << SVGWidth << "\" height=\"" << SVGHeight << "\">" << std::endl;
				SVGFile << "\t<!-- Created by: " << ProgramVersionString << " -->" << std::endl;
				SVGFile << "\t<clipPath id=\"GraphRegion\"><polygon points=\"" << GraphLeft << "," << GraphTop << " " << GraphRight << "," << GraphTop << " " << GraphRight << "," << GraphBottom << " " << GraphLeft << "," << GraphBottom << "\" /></clipPath>" << std::endl;
				SVGFile << "\t<style>" << std::endl;
				SVGFile << "\t\ttext { font-family: sans-serif; font-size: " << FontSize << "px; fill: black; }" << std::endl;
				SVGFile << "\t\tline { stroke: black; }" << std::endl;
				SVGFile << "\t\tpolygon { fill-opacity: 0.5; }" << std::endl;
				if (DarkStyle)
				{
					SVGFile << "\t@media only screen and (prefers-color-scheme: dark) {" << std::endl;
					SVGFile << "\t\ttext { fill: grey; }" << std::endl;
					SVGFile << "\t\tline { stroke: grey; }" << std::endl;
					SVGFile << "\t}" << std::endl;
				}
				SVGFile << "\t</style>" << std::endl;
#ifdef DEBUG
				SVGFile << "<!-- VoltMax: " << VoltMax << " -->" << std::endl;
				SVGFile << "<!-- VoltMin: " << VoltMin << " -->" << std::endl;
				SVGFile << "<!-- VoltVerticalFactor: " << VoltVerticalFactor << " -->" << std::endl;
#endif // DEBUG
				SVGFile << "\t<rect style=\"fill-opacity:0;stroke:grey;stroke-width:2\" width=\"" << SVGWidth << "\" height=\"" << SVGHeight << "\" />" << std::endl;

				// Legend Text
				int LegendIndex = 1;
				SVGFile << "\t<text x=\"" << GraphLeft << "\" y=\"" << GraphTop - 2 << "\">" << Title << "</text>" << std::endl;
				SVGFile << "\t<text style=\"text-anchor:end\" x=\"" << GraphRight << "\" y=\"" << GraphTop - 2 << "\">" << timeToExcelLocal(TheValues[0].Time) << "</text>" << std::endl;
				SVGFile << "\t<text style=\"fill:blue;text-anchor:middle\" x=\"" << FontSize * LegendIndex << "\" y=\"50%\" transform=\"rotate(270 " << FontSize * LegendIndex << "," << (GraphTop + GraphBottom) / 2 << ")\">" << YLegendTemperature << "</text>" << std::endl;
				if (DrawVoltage)
				{
					LegendIndex++;
					SVGFile << "\t<text style=\"fill:green;text-anchor:middle\" x=\"" << FontSize * LegendIndex << "\" y=\"50%\" transform=\"rotate(270 " << FontSize * LegendIndex << "," << (GraphTop + GraphBottom) / 2 << ")\">" << YLegendVoltage << "</text>" << std::endl;
				}

				// Top Line
				SVGFile << "\t<line x1=\"" << GraphLeft - TickSize << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphRight + TickSize << "\" y2=\"" << GraphTop << "\"/>" << std::endl;
				SVGFile << "\t<text style=\"fill:blue;text-anchor:end;dominant-baseline:middle\" x=\"" << GraphLeft - TickSize << "\" y=\"" << GraphTop << "\">" << std::fixed << std::setprecision(1) << TempMax << "</text>" << std::endl;
				if (DrawVoltage)
					SVGFile << "\t<text style=\"fill:green;dominant-baseline:middle\" x=\"" << GraphRight + TickSize << "\" y=\"" << GraphTop << "\">" << std::fixed << std::setprecision(1) << VoltMax << "</text>" << std::endl;

				// Bottom Line
				SVGFile << "\t<line x1=\"" << GraphLeft - TickSize << "\" y1=\"" << GraphBottom << "\" x2=\"" << GraphRight + TickSize << "\" y2=\"" << GraphBottom << "\"/>" << std::endl;
				SVGFile << "\t<text style=\"fill:blue;text-anchor:end;dominant-baseline:middle\" x=\"" << GraphLeft - TickSize << "\" y=\"" << GraphBottom << "\">" << std::fixed << std::setprecision(1) << TempMin << "</text>" << std::endl;
				if (DrawVoltage)
					SVGFile << "\t<text style=\"fill:green;dominant-baseline:middle\" x=\"" << GraphRight + TickSize << "\" y=\"" << GraphBottom << "\">" << std::fixed << std::setprecision(1) << VoltMin << "</text>" << std::endl;

				// Left Line
				SVGFile << "\t<line x1=\"" << GraphLeft << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft << "\" y2=\"" << GraphBottom << "\"/>" << std::endl;

				// Right Line
				SVGFile << "\t<line x1=\"" << GraphRight << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphRight << "\" y2=\"" << GraphBottom << "\"/>" << std::endl;

				// Vertical Division Dashed Lines
				for (auto index = 1; index < 4; index++)
				{
					SVGFile << "\t<line style=\"stroke-dasharray:1\" x1=\"" << GraphLeft - TickSize << "\" y1=\"" << GraphTop + (GraphVerticalDivision * index) << "\" x2=\"" << GraphRight + TickSize << "\" y2=\"" << GraphTop + (GraphVerticalDivision * index) << "\" />" << std::endl;
					SVGFile << "\t<text style=\"fill:blue;text-anchor:end;dominant-baseline:middle\" x=\"" << GraphLeft - TickSize << "\" y=\"" << GraphTop + (GraphVerticalDivision * index) << "\">" << std::fixed << std::setprecision(1) << TempMax - (TempVerticalDivision * index) << "</text>" << std::endl;
					if (DrawVoltage)
						SVGFile << "\t<text style=\"fill:green;dominant-baseline:middle\" x=\"" << GraphRight + TickSize << "\" y=\"" << GraphTop + (GraphVerticalDivision * index) << "\">" << std::fixed << std::setprecision(1) << VoltMax - (VoltVerticalDivision * index) << "</text>" << std::endl;
				}

				// Horizontal Division Dashed Lines
				for (auto index = 0; index < (GraphWidth < TheValues.size() ? GraphWidth : TheValues.size()); index++)
				{
					struct tm UTC;
					if (0 != localtime_r(&TheValues[index].Time, &UTC))
					{
						if (graph == GraphType::daily)
						{
							if (UTC.tm_min == 0)
							{
								if (UTC.tm_hour == 0)
									SVGFile << "\t<line style=\"stroke:red\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
								else
									SVGFile << "\t<line style=\"stroke-dasharray:1\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
								if (UTC.tm_hour % 2 == 0)
									SVGFile << "\t<text style=\"text-anchor:middle\" x=\"" << GraphLeft + index << "\" y=\"" << SVGHeight - 2 << "\">" << UTC.tm_hour << "</text>" << std::endl;
							}
						}
						else if (graph == GraphType::weekly)
						{
							const std::string Weekday[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
							if ((UTC.tm_hour == 0) && (UTC.tm_min == 0))
							{
								if (UTC.tm_wday == 0)
									SVGFile << "\t<line style=\"stroke:red\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
								else
									SVGFile << "\t<line style=\"stroke-dasharray:1\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
							}
							else if ((UTC.tm_hour == 12) && (UTC.tm_min == 0))
								SVGFile << "\t<text style=\"text-anchor:middle\" x=\"" << GraphLeft + index << "\" y=\"" << SVGHeight - 2 << "\">" << Weekday[UTC.tm_wday] << "</text>" << std::endl;
						}
						else if (graph == GraphType::monthly)
						{
							if ((UTC.tm_mday == 1) && (UTC.tm_hour == 0) && (UTC.tm_min == 0))
								SVGFile << "\t<line style=\"stroke:red\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
							if ((UTC.tm_wday == 0) && (UTC.tm_hour == 0) && (UTC.tm_min == 0))
								SVGFile << "\t<line style=\"stroke-dasharray:1\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
							else if ((UTC.tm_wday == 3) && (UTC.tm_hour == 12) && (UTC.tm_min == 0))
								SVGFile << "\t<text style=\"text-anchor:middle\" x=\"" << GraphLeft + index << "\" y=\"" << SVGHeight - 2 << "\">Week " << UTC.tm_yday / 7 + 1 << "</text>" << std::endl;
						}
						else if (graph == GraphType::yearly)
						{
							const std::string Month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
							if ((UTC.tm_yday == 0) && (UTC.tm_mday == 1) && (UTC.tm_hour == 0) && (UTC.tm_min == 0))
								SVGFile << "\t<line style=\"stroke:red\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
							else if ((UTC.tm_mday == 1) && (UTC.tm_hour == 0) && (UTC.tm_min == 0))
								SVGFile << "\t<line style=\"stroke-dasharray:1\" x1=\"" << GraphLeft + index << "\" y1=\"" << GraphTop << "\" x2=\"" << GraphLeft + index << "\" y2=\"" << GraphBottom + TickSize << "\" />" << std::endl;
							else if ((UTC.tm_mday == 15) && (UTC.tm_hour == 0) && (UTC.tm_min == 0))
								SVGFile << "\t<text style=\"text-anchor:middle\" x=\"" << GraphLeft + index << "\" y=\"" << SVGHeight - 2 << "\">" << Month[UTC.tm_mon] << "</text>" << std::endl;
						}
					}
				}

				// Directional Arrow
				SVGFile << "\t<polygon style=\"fill:red;stroke:red;fill-opacity:1;\" points=\"" << GraphLeft - 3 << "," << GraphBottom << " " << GraphLeft + 3 << "," << GraphBottom - 3 << " " << GraphLeft + 3 << "," << GraphBottom + 3 << "\" />" << std::endl;

				{
					// Temperature Values as a continuous line
					SVGFile << "\t<!-- Temperature -->" << std::endl;
					SVGFile << "\t<polyline style=\"fill:none;stroke:blue;clip-path:url(#GraphRegion)\" points=\"";
					for (auto index = 1; index < (GraphWidth < TheValues.size() ? GraphWidth : TheValues.size()); index++)
						SVGFile << index + GraphLeft << "," << int(((TempMax - TheValues[index].GetTemperature(Fahrenheit)) * TempVerticalFactor) + GraphTop) << " ";
					SVGFile << "\" />" << std::endl;
				}

				if (DrawVoltage)
				{
					// Voltage Graphic as a continuous line
					SVGFile << "\t<!-- Voltage -->" << std::endl;
					SVGFile << "\t<polyline style=\"fill:lime;stroke:green;clip-path:url(#GraphRegion)\" points=\"";
					for (auto index = 1; index < (GraphWidth < TheValues.size() ? GraphWidth : TheValues.size()); index++)
						SVGFile << index + GraphLeft << "," << int(((VoltMax - TheValues[index].GetVoltage()) * VoltVerticalFactor) + GraphTop) << " ";
					SVGFile << "\" />" << std::endl;

					for (auto cell = 0; cell < (TheValues.begin()->GetCellCount() - 1); cell++)
					{
						// Cell Voltage Graphic as a continuous line
						SVGFile << "\t<!-- Cell " << cell << " Voltage -->" << std::endl;
						SVGFile << "\t<polyline style=\"fill:lime;stroke:green;clip-path:url(#GraphRegion)\" points=\"";
						for (auto index = 1; index < (GraphWidth < TheValues.size() ? GraphWidth : TheValues.size()); index++)
							SVGFile << index + GraphLeft << "," << int(((VoltMax - TheValues[index].GetCellVoltage(cell)) * VoltVerticalFactor) + GraphTop) << " ";
						SVGFile << "\" />" << std::endl;
					}
				}

				SVGFile << "</svg>" << std::endl;
				SVGFile.close();
				struct utimbuf SVGut;
				SVGut.actime = TheValues.begin()->Time;
				SVGut.modtime = TheValues.begin()->Time;
				utime(SVGFileName.c_str(), &SVGut);
			}
		}
	}
}
void WriteAllSVG()
{
	//ReadTitleMap(SVGTitleMapFilename);
	for (auto it = VictronSmartLithiumMRTGLogs.begin(); it != VictronSmartLithiumMRTGLogs.end(); it++)
	{
		const bdaddr_t TheAddress = it->first;
		std::string btAddress(ba2string(TheAddress));
		for (auto pos = btAddress.find(':'); pos != std::string::npos; pos = btAddress.find(':'))
			btAddress.erase(pos, 1);
		std::string ssTitle(btAddress);
		if (VictronNames.find(TheAddress) != VictronNames.end())
			ssTitle = VictronNames.find(TheAddress)->second + " (" + ba2string(TheAddress) + ")";
		std::filesystem::path OutputPath;
		std::ostringstream OutputFilename;
		OutputFilename.str("");
		OutputFilename << "victron-";
		OutputFilename << btAddress;
		OutputFilename << "-day.svg";
		OutputPath = SVGDirectory / OutputFilename.str();
		std::vector<VictronSmartLithium> TheValues;
		ReadMRTGData(TheAddress, TheValues, GraphType::daily);
		WriteSVG(TheValues, OutputPath, ssTitle, GraphType::daily, SVGFahrenheit);
		OutputFilename.str("");
		OutputFilename << "victron-";
		OutputFilename << btAddress;
		OutputFilename << "-week.svg";
		OutputPath = SVGDirectory / OutputFilename.str();
		ReadMRTGData(TheAddress, TheValues, GraphType::weekly);
		WriteSVG(TheValues, OutputPath, ssTitle, GraphType::weekly, SVGFahrenheit);
		OutputFilename.str("");
		OutputFilename << "victron-";
		OutputFilename << btAddress;
		OutputFilename << "-month.svg";
		OutputPath = SVGDirectory / OutputFilename.str();
		ReadMRTGData(TheAddress, TheValues, GraphType::monthly);
		WriteSVG(TheValues, OutputPath, ssTitle, GraphType::monthly, SVGFahrenheit);
		OutputFilename.str("");
		OutputFilename << "victron-";
		OutputFilename << btAddress;
		OutputFilename << "-year.svg";
		OutputPath = SVGDirectory / OutputFilename.str();
		ReadMRTGData(TheAddress, TheValues, GraphType::yearly);
		WriteSVG(TheValues, OutputPath, ssTitle, GraphType::yearly, SVGFahrenheit);
	}
}
/////////////////////////////////////////////////////////////////////////////
void ReadLoggedData(const std::filesystem::path& filename)
{
	const std::regex BluetoothAddressRegex("[[:xdigit:]]{12}");
	std::smatch BluetoothAddressInFilename;
	std::string Stem(filename.stem().string());
	if (std::regex_search(Stem, BluetoothAddressInFilename, BluetoothAddressRegex))
	{
		std::string ssBTAddress(BluetoothAddressInFilename.str());
		for (auto index = ssBTAddress.length() - 2; index > 0; index -= 2)
			ssBTAddress.insert(index, ":");
		bdaddr_t TheBlueToothAddress(string2ba(ssBTAddress));

		// Only read the file if it's newer than what we may have cached
		bool bReadFile = true;
		struct stat64 FileStat;
		FileStat.st_mtim.tv_sec = 0;
		if (0 == stat64(filename.c_str(), &FileStat))	// returns 0 if the file-status information is obtained
		{
			auto it = VictronSmartLithiumMRTGLogs.find(TheBlueToothAddress);
			if (it != VictronSmartLithiumMRTGLogs.end())
				if (!it->second.empty())
					if (FileStat.st_mtim.tv_sec < (it->second.begin()->Time))	// only read the file if it more recent than existing data
						bReadFile = false;
		}

		if (bReadFile)
		{
			if (ConsoleVerbosity > 0)
				std::cout << "[" << getTimeISO8601() << "] Reading: " << filename.string() << std::endl;
			else
				std::cerr << "Reading: " << filename.string() << std::endl;
			std::ifstream TheFile(filename);
			if (TheFile.is_open())
			{
				std::vector<std::string> SortableFile;
				std::string TheLine;
				while (std::getline(TheFile, TheLine))
					SortableFile.push_back(TheLine);
				TheFile.close();
				sort(SortableFile.begin(), SortableFile.end());
				for (auto iter = SortableFile.begin(); iter != SortableFile.end(); iter++)
				{
					VictronSmartLithium TheValue(*iter);
					if (TheValue.IsValid())
						UpdateMRTGData(TheBlueToothAddress, TheValue);
				}
			}
		}
	}
}
// Finds log files specific to this program then reads the contents into the memory mapped structure simulating MRTG log files.
void ReadLoggedData(void)
{
	const std::regex LogFileRegex("victron-[[:xdigit:]]{12}-[[:digit:]]{4}-[[:digit:]]{2}.txt");
	if (!LogDirectory.empty())
	{
		if (ConsoleVerbosity > 1)
			std::cout << "[" << getTimeISO8601() << "] ReadLoggedData: " << LogDirectory << std::endl;
		std::deque<std::filesystem::path> files;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ LogDirectory })
			if (dir_entry.is_regular_file())
				if (std::regex_match(dir_entry.path().filename().string(), LogFileRegex))
					files.push_back(dir_entry);
		if (!files.empty())
		{
			sort(files.begin(), files.end());
			while (!files.empty())
			{
				ReadLoggedData(*files.begin());
				files.pop_front();
			}
		}
	}
}
/////////////////////////////////////////////////////////////////////////////
std::filesystem::path GenerateCacheFileName(const bdaddr_t& a)
{
	std::string btAddress(ba2string(a));
	for (auto pos = btAddress.find(':'); pos != std::string::npos; pos = btAddress.find(':'))
		btAddress.erase(pos, 1);
	std::ostringstream OutputFilename;
	OutputFilename << "victron-";
	OutputFilename << btAddress;
	OutputFilename << "-cache.txt";
	std::filesystem::path CacheFileName(CacheDirectory / OutputFilename.str());
	return(CacheFileName);
}
bool GenerateCacheFile(const bdaddr_t& a, const std::vector<VictronSmartLithium>& MRTGLog)
{
	bool rval(false);
	if (!MRTGLog.empty())
	{
		std::filesystem::path MRTGCacheFile(GenerateCacheFileName(a));
		struct stat64 Stat({ 0 });	// Zero the stat64 structure when it's allocated
		stat64(MRTGCacheFile.c_str(), &Stat);	// This shouldn't change Stat if the file doesn't exist.
		if (difftime(MRTGLog[0].Time, Stat.st_mtim.tv_sec) > 60 * 60) // If Cache File has data older than 60 minutes, write it
		{
			std::ofstream CacheFile(MRTGCacheFile, std::ios_base::out | std::ios_base::trunc);
			if (CacheFile.is_open())
			{
				if (ConsoleVerbosity > 0)
					std::cout << "[" << getTimeISO8601(true) << "] Writing: " << MRTGCacheFile.string() << std::endl;
				else
					std::cerr << "Writing: " << MRTGCacheFile.string() << std::endl;
				CacheFile << "Cache: " << ba2string(a) << " " << ProgramVersionString << std::endl;
				for (auto i : MRTGLog)
					CacheFile << i.WriteCache() << std::endl;
				CacheFile.close();
				struct utimbuf ut;
				ut.actime = MRTGLog[0].Time;
				ut.modtime = MRTGLog[0].Time;
				utime(MRTGCacheFile.c_str(), &ut);
				rval = true;
			}
		}
	}
	return(rval);
}
void GenerateCacheFile(std::map<bdaddr_t, std::vector<VictronSmartLithium>>& MRTGLogMap)
{
	if (!CacheDirectory.empty())
	{
		if (ConsoleVerbosity > 1)
			std::cout << "[" << getTimeISO8601() << "] GenerateCacheFile: " << CacheDirectory << std::endl;
		for (auto it = MRTGLogMap.begin(); it != MRTGLogMap.end(); ++it)
			GenerateCacheFile(it->first, it->second);
	}
}
void ReadCacheDirectory(void)
{
	const std::regex CacheFileRegex("^victron-[[:xdigit:]]{12}-cache.txt");
	if (!CacheDirectory.empty())
	{
		if (ConsoleVerbosity > 1)
			std::cout << "[" << getTimeISO8601() << "] ReadCacheDirectory: " << CacheDirectory << std::endl;
		std::deque<std::filesystem::path> files;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ CacheDirectory })
			if (dir_entry.is_regular_file())
				if (std::regex_match(dir_entry.path().filename().string(), CacheFileRegex))
					files.push_back(dir_entry);
		if (!files.empty())
		{
			sort(files.begin(), files.end());
			while (!files.empty())
			{
				std::ifstream TheFile(*files.begin());
				if (TheFile.is_open())
				{
					if (ConsoleVerbosity > 0)
						std::cout << "[" << getTimeISO8601(true) << "] Reading: " << files.begin()->string() << std::endl;
					else
						std::cerr << "Reading: " << files.begin()->string() << std::endl;
					std::string TheLine;
					if (std::getline(TheFile, TheLine))
					{
						const std::regex CacheFirstLineRegex("^Cache: ((([[:xdigit:]]{2}:){5}))[[:xdigit:]]{2}.*");
						// every Cache File should have a start line with the name Cache, the Bluetooth Address, and the creator version. 
						// TODO: check to make sure the version is compatible
						if (std::regex_match(TheLine, CacheFirstLineRegex))
						{
							const std::regex BluetoothAddressRegex("((([[:xdigit:]]{2}:){5}))[[:xdigit:]]{2}");
							std::smatch BluetoothAddress;
							if (std::regex_search(TheLine, BluetoothAddress, BluetoothAddressRegex))
							{
								bdaddr_t TheBlueToothAddress(string2ba(BluetoothAddress.str()));
								std::vector<VictronSmartLithium> FakeMRTGFile;
								FakeMRTGFile.reserve(2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT); // this might speed things up slightly
								while (std::getline(TheFile, TheLine))
								{
									VictronSmartLithium value;
									value.ReadCache(TheLine);
									FakeMRTGFile.push_back(value);
								}
								if (FakeMRTGFile.size() == (2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT)) // simple check to see if we are the right size
									VictronSmartLithiumMRTGLogs.insert(std::pair<bdaddr_t, std::vector<VictronSmartLithium>>(TheBlueToothAddress, FakeMRTGFile));
							}
						}
					}
					TheFile.close();
				}
				files.pop_front();
			}
		}
	}
}
/////////////////////////////////////////////////////////////////////////////
std::string bluez_dbus_msg_iter(DBusMessageIter& array_iter, const bdaddr_t& dbusBTAddress)
{
	std::ostringstream ssOutput;
	time_t TimeNow;
	time(&TimeNow);
	auto Device = VictronEncryptionKeys.find(dbusBTAddress);
	if (Device != VictronEncryptionKeys.end())
	do
	{
		DBusMessageIter dict2_iter;
		dbus_message_iter_recurse(&array_iter, &dict2_iter);
		DBusBasicValue value;
		dbus_message_iter_get_basic(&dict2_iter, &value);
		std::string Key(value.str);
		dbus_message_iter_next(&dict2_iter);
		DBusMessageIter variant_iter;
		dbus_message_iter_recurse(&dict2_iter, &variant_iter);
		do
		{
			auto dbus_message_Type = dbus_message_iter_get_arg_type(&variant_iter);
			if (!Key.compare("Name"))
			{
				if ((DBUS_TYPE_STRING == dbus_message_Type) || (DBUS_TYPE_OBJECT_PATH == dbus_message_Type))
				{
					dbus_message_iter_get_basic(&variant_iter, &value);
					std::string Name(value.str);
					auto ElementInserted = VictronNames.insert(std::make_pair(dbusBTAddress, Name)); // Either get the existing record or insert a new one
					if (!ElementInserted.second) // true if inserted, false if already exists
						ElementInserted.first->second = Name;
					ssOutput << "[" << timeToISO8601(TimeNow, true) << "] [" << ba2string(dbusBTAddress) << "] " << Key << ": " << Name << std::endl;
				}
			}
			else if (!Key.compare("UUIDs"))
			{
				DBusMessageIter array3_iter;
				dbus_message_iter_recurse(&variant_iter, &array3_iter);
				do
				{
					if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&array3_iter))
					{
						dbus_message_iter_get_basic(&array3_iter, &value);
						ssOutput << "[                   ] [" << ba2string(dbusBTAddress) << "] " << Key << ": " << value.str << std::endl;
					}
				} while (dbus_message_iter_next(&array3_iter));
			}
			else if (!Key.compare("ManufacturerData"))
			{
				if (DBUS_TYPE_ARRAY == dbus_message_Type)
				{
					DBusMessageIter array3_iter;
					dbus_message_iter_recurse(&variant_iter, &array3_iter);
					do
					{
						if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&array3_iter))
						{
							DBusMessageIter dict1_iter;
							dbus_message_iter_recurse(&array3_iter, &dict1_iter);
							if (DBUS_TYPE_UINT16 == dbus_message_iter_get_arg_type(&dict1_iter))
							{
								DBusBasicValue value;
								dbus_message_iter_get_basic(&dict1_iter, &value);
								uint16_t ManufacturerID(value.u16);
								dbus_message_iter_next(&dict1_iter);
								if (DBUS_TYPE_VARIANT == dbus_message_iter_get_arg_type(&dict1_iter))
								{
									DBusMessageIter variant2_iter;
									dbus_message_iter_recurse(&dict1_iter, &variant2_iter);
									if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&variant2_iter))
									{
										std::vector<uint8_t> ManufacturerData;
										DBusMessageIter array4_iter;
										dbus_message_iter_recurse(&variant2_iter, &array4_iter);
										do
										{
											if (DBUS_TYPE_BYTE == dbus_message_iter_get_arg_type(&array4_iter))
											{
												dbus_message_iter_get_basic(&array4_iter, &value);
												ManufacturerData.push_back(value.byt);
											}
										} while (dbus_message_iter_next(&array4_iter));

										ssOutput << "[" << timeToISO8601(TimeNow, true) << "] [" << ba2string(dbusBTAddress) << "] " << Key << ": " << std::setfill('0') << std::hex << std::setw(4) << ManufacturerID << ":";
										for (auto& Data : ManufacturerData)
											ssOutput << std::setw(2) << int(Data);
										if (ConsoleVerbosity > 4)
										{
											// https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/company_identifiers/company_identifiers.yaml
											ssOutput << " ";
											if (0x0001 == ManufacturerID)
												ssOutput << "'Nokia Mobile Phones'";
											if (0x0006 == ManufacturerID)
												ssOutput << "'Microsoft'";
											if (0x004c == ManufacturerID)
												ssOutput << "'Apple, Inc.'";
											if (0x058e == ManufacturerID)
												ssOutput << "'Meta Platforms Technologies, LLC'";
											if (0x02E1 == ManufacturerID)
												ssOutput << "'Victron Energy BV'";
										}
										std::vector<uint8_t> EncryptionKey;
										for (size_t i = 0; i < Device->second.length(); i += 2)
										{
											std::string byteString(Device->second.substr(i, 2));
											uint8_t byteValue(static_cast<uint8_t>(std::stoi(byteString, nullptr, 16)));
											EncryptionKey.push_back(byteValue);
										}
										if (ManufacturerData[7] == EncryptionKey[0]) // if stored key doesnt start with this data, we need to update stored key
										{
											uint8_t DecryptedData[32] { 0 };
											if (sizeof(DecryptedData) >= (ManufacturerData.size() - 8)) // simple check to make sure we don't buffer overflow
											{
												//[2024-09-04T04:47:30] [CE:A5:D7:7B:CD:81] Name: S/V Sola Batt 1
												//                                                                 0 1 2 3  4  5 6  7  8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 
												//[2024-09-03T21:47:30] [CE:A5:D7:7B:CD:81] ManufacturerData: 02e1:1000eba0 05 35a2 d9 2d331d1ab2f30574993493ead132be09 'Victron Energy BV'
												//[2024-09-03T21:47:37] [CE:A5:D7:7B:CD:81] ManufacturerData: 02e1:1000eba0 05 3ba2 d9 53fefc2f4ce0fac5905e13b24c6ef6c2 'Victron Energy BV'
												//[2024-09-03T21:47:37] [CE:A5:D7:7B:CD:81] ManufacturerData: 02e1:1000eba0 05 3ba2 d9 53fefc2f4ce0fac5905e13b24c6ef6c2 'Victron Energy BV'										
												//                                                                          0  1 2  3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9     
												//[2024-09-04T16:56:06] [D3:D1:90:54:EB:F0] Name: S/V Sola Orion XS
												//[2024-09-04T09:56:06] [D3:D1:90:54:EB:F0] ManufacturerData: 02e1:1000f0a3 0f 526d 4a 75a80473b5ec702716a85f2db193 'Victron Energy BV'
												// https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html
												//Byte [0] is the Manufacturer Data Record type and is always 0x10.
												//Byte [1] and [2] are the model id. In my case the 0x02 0x57 means I have the MPPT 100/50 (I forget where I found this info but it's always 2 bytes and it's not really needed for decryption)
												//Byte [3] is the "read out type" which was always 0xA0 in my case but i didn't use this byte at all
												//
												//The first 4 bytes aren't mentioned in the provided documentation so it was difficult to figure out where the "extra data" started. Now we get into the bytes documented:
												//Byte [4] is the record type. In my case it was always 0x01 because I have a "Solar Charger"
												//Byte [5] and [6] are the Nonce/Data Counter used for decryption (more on this later)
												//Byte [7] should match the first byte of your devices encryption key. In my case this was 0x20.
												//
												//The rest of the bytes are the encrypted data of which there are 12 bytes for my Victron device.
												EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
												if (ctx != 0)
												{
													uint8_t InitializationVector[16] { ManufacturerData[5], ManufacturerData[6], 0}; // The first two bytes are assigned, the rest of the 16 are padded with zero

													if (1 == EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, EncryptionKey.data(), InitializationVector))
													{
														int len(0);
														if (1 == EVP_DecryptUpdate(ctx, DecryptedData, &len, ManufacturerData.data() + 8, ManufacturerData.size() - 8))
														{
															if (1 == EVP_DecryptFinal_ex(ctx, DecryptedData + len, &len))
															{
																// We have decrypted data!
																ManufacturerData[5] = ManufacturerData[6] = ManufacturerData[7] = 0; // I'm writing a zero here to remind myself I've decoded the data already
																for (auto index = 0; index < ManufacturerData.size() - 8; index++) // copy the decoded data over the original data
																	ManufacturerData[index+8] = DecryptedData[index];
																std::ostringstream ssLogEntry;
																ssLogEntry << timeToISO8601(TimeNow) << "\t";
																for (auto &a : ManufacturerData)
																	ssLogEntry << std::setfill('0') << std::hex << std::setw(2) << int(a);
																std::queue<std::string> foo;
																auto ret = VictronVirtualLog.insert(std::pair<bdaddr_t, std::queue<std::string>>(dbusBTAddress, foo)); // Either get the existing record or insert a new one
																ret.first->second.push(ssLogEntry.str());	// puts the measurement in the queue to be written to the log file
																//UpdateMRTGData(localBTAddress, localTemp);	// puts the measurement in the fake MRTG data structure
																//GoveeLastDownload.insert(std::pair<bdaddr_t, time_t>(localBTAddress, 0));	// Makes sure the Bluetooth Address is in the list to get downloaded historical data
																if (ManufacturerData[4] == 0x01) // Solar Charger
																{
																	if (ConsoleVerbosity > 0)
																	{
																		VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data() + 8);
																		ssOutput << std::dec;
																		ssOutput << " (Solar)";
																		ssOutput << " battery_current:" << float(ExtraDataPtr->SolarCharger.battery_current) * 0.01 << "V";
																		ssOutput << " battery_voltage:" << float(ExtraDataPtr->SolarCharger.battery_voltage) * 0.01 << "V";
																		ssOutput << " load_current:" << float(ExtraDataPtr->SolarCharger.load_current) * 0.01 << "V";
																	}
																}
																else if (ManufacturerData[4] == 0x04) // DC/DC converter
																{
																	if (ConsoleVerbosity > 0)
																	{
																		VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data() + 8);
																		ssOutput << std::dec;
																		ssOutput << " (DC/DC)";
																		ssOutput << " input_voltage:" << float(ExtraDataPtr->DCDCConverter.input_voltage) * 0.01 + 2.60 << "V";
																		ssOutput << " output_voltage:" << float(ExtraDataPtr->DCDCConverter.output_voltage) * 0.01 + 2.60 << "V";
																	}
																}
																else if (ManufacturerData[4] == 0x05) // SmartLithium
																{
																	VictronSmartLithium local;
																	if (local.ReadManufacturerData(ManufacturerData, TimeNow))
																	{
																		UpdateMRTGData(dbusBTAddress, local);	// puts the measurement in the fake MRTG data structure
																		if (ConsoleVerbosity > 0)
																			ssOutput << local.WriteConsole();
																	}
																	else if (ConsoleVerbosity > 0)
																	{
																		VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data() + 8);
																		ssOutput << std::dec;
																		ssOutput << " (SmartLithium)";
																		ssOutput << " cell_1:" << float(ExtraDataPtr->SmartLithium.cell_1) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_2:" << float(ExtraDataPtr->SmartLithium.cell_2) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_3:" << float(ExtraDataPtr->SmartLithium.cell_3) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_4:" << float(ExtraDataPtr->SmartLithium.cell_4) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_5:" << float(ExtraDataPtr->SmartLithium.cell_5) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_6:" << float(ExtraDataPtr->SmartLithium.cell_6) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_7:" << float(ExtraDataPtr->SmartLithium.cell_7) * 0.01 + 2.60 << "V";
																		ssOutput << " cell_8:" << float(ExtraDataPtr->SmartLithium.cell_8) * 0.01 + 2.60 << "V";
																		ssOutput << " battery_voltage:" << float(ExtraDataPtr->SmartLithium.battery_voltage) * 0.01 << "V";
																		ssOutput << " battery_temperature:" << ExtraDataPtr->SmartLithium.battery_temperature - 40 << "\u00B0" << "C";
																	}
																}
																else if (ManufacturerData[4] == 0x0F) // OrionXS
																{
																	VictronOrionXS local;
																	if (local.ReadManufacturerData(ManufacturerData, TimeNow))
																	{
																		//UpdateMRTGData(dbusBTAddress, local);	// puts the measurement in the fake MRTG data structure
																		if (ConsoleVerbosity > 0)
																			ssOutput << local.WriteConsole();
																	}
																	else if (ConsoleVerbosity > 0)
																	{
																		VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data() + 8);
																		ssOutput << std::dec;
																		ssOutput << " (Orion XS)";
																		ssOutput << " output_voltage:" << float(ExtraDataPtr->OrionXS.output_voltage) * 0.01 << "V";
																		ssOutput << " output_current:" << float(ExtraDataPtr->OrionXS.output_current) * 0.01 << "A";
																		ssOutput << " input_voltage:" << float(ExtraDataPtr->OrionXS.input_voltage) * 0.01 << "V";
																		ssOutput << " input_current:" << float(ExtraDataPtr->OrionXS.input_current) * 0.01 << "A";
																	}
																}
															}
														}
													}
													EVP_CIPHER_CTX_free(ctx);
												}
											}
											ssOutput << std::endl;
										}
									}
								}
							}
						}
					} while (dbus_message_iter_next(&array3_iter));
				}
			}
		} while (dbus_message_iter_next(&variant_iter));
	} while (dbus_message_iter_next(&array_iter));
	return(ssOutput.str());
}
void bluez_dbus_FindExistingDevices(DBusConnection* dbus_conn)
{
	// This function is mainly useful after a rapid restart of the program. BlueZ keeps around information on devices for three minutes after scanning has been stopped.
	std::ostringstream ssOutput;
	DBusMessage* dbus_msg = dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
	if (dbus_msg)
	{
		// Initialize D-Bus error
		DBusError dbus_error;
		dbus_error_init(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#ga8937f0b7cdf8554fa6305158ce453fbe
		DBusMessage* dbus_reply = dbus_connection_send_with_reply_and_block(dbus_conn, dbus_msg, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
		dbus_message_unref(dbus_msg);
		if (dbus_reply)
		{
			if (dbus_message_get_type(dbus_reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
			{
				const std::string dbus_reply_Signature(dbus_message_get_signature(dbus_reply));
				int indent(16);
				if (!dbus_reply_Signature.compare("a{oa{sa{sv}}}"))
				{
					DBusMessageIter root_iter;
					dbus_message_iter_init(dbus_reply, &root_iter);
					do {
						DBusMessageIter array1_iter;
						dbus_message_iter_recurse(&root_iter, &array1_iter);
						do {
							indent += 4;
							DBusMessageIter dict1_iter;
							dbus_message_iter_recurse(&array1_iter, &dict1_iter);
							DBusBasicValue value;
							dbus_message_iter_get_basic(&dict1_iter, &value);
							std::string dict1_object_path(value.str);
							dbus_message_iter_next(&dict1_iter);
							DBusMessageIter array2_iter;
							dbus_message_iter_recurse(&dict1_iter, &array2_iter);
							do
							{
								DBusMessageIter dict2_iter;
								dbus_message_iter_recurse(&array2_iter, &dict2_iter);
								dbus_message_iter_get_basic(&dict2_iter, &value);
								std::string dict2_string(value.str);
								if (!dict2_string.compare("org.bluez.Device1"))
								{
									if (ConsoleVerbosity > 1)
										ssOutput << "[" << getTimeISO8601() << "] " << std::right << std::setw(indent) << "Object Path: " << dict1_object_path << std::endl;
									dbus_message_iter_next(&dict2_iter);
									DBusMessageIter array3_iter;
									dbus_message_iter_recurse(&dict2_iter, &array3_iter);
									bdaddr_t localBTAddress({ 0 });
									const std::regex BluetoothAddressRegex("((([[:xdigit:]]{2}_){5}))[[:xdigit:]]{2}");
									std::smatch AddressMatch;
									if (std::regex_search(dict1_object_path, AddressMatch, BluetoothAddressRegex))
									{
										std::string BluetoothAddress(AddressMatch.str());
										std::replace(BluetoothAddress.begin(), BluetoothAddress.end(), '_', ':');
										localBTAddress = string2ba(BluetoothAddress);
									}
									ssOutput << bluez_dbus_msg_iter(array3_iter, localBTAddress);
								}
							} while (dbus_message_iter_next(&array2_iter));
							indent -= 4;
						} while (dbus_message_iter_next(&array1_iter));
					} while (dbus_message_iter_next(&root_iter));
				}
			}
			dbus_message_unref(dbus_reply);
		}
	}
	if (ConsoleVerbosity > 0)
		std::cout << ssOutput.str();
}
void bluez_dbus_msg_InterfacesAdded(DBusMessage* dbus_msg, bdaddr_t& dbusBTAddress)
{
	std::ostringstream ssOutput;
	if (std::string(dbus_message_get_signature(dbus_msg)).compare("oa{sa{sv}}"))
		ssOutput << "Invalid Signature: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
	else
	{
		DBusMessageIter root_iter;
		dbus_message_iter_init(dbus_msg, &root_iter);
		DBusBasicValue value;
		dbus_message_iter_get_basic(&root_iter, &value);
		std::string root_object_path(value.str);

		std::string BluetoothAddress;
		const std::regex BluetoothAddressRegex("((([[:xdigit:]]{2}_){5}))[[:xdigit:]]{2}");
		std::smatch AddressMatch;
		if (std::regex_search(root_object_path, AddressMatch, BluetoothAddressRegex))
		{
			BluetoothAddress = AddressMatch.str();
			std::replace(BluetoothAddress.begin(), BluetoothAddress.end(), '_', ':');
			dbusBTAddress = string2ba(BluetoothAddress);
		}
		dbus_message_iter_next(&root_iter);
		DBusMessageIter array1_iter;
		dbus_message_iter_recurse(&root_iter, &array1_iter);
		do
		{
			DBusMessageIter dict1_iter;
			dbus_message_iter_recurse(&array1_iter, &dict1_iter);
			DBusBasicValue value;
			dbus_message_iter_get_basic(&dict1_iter, &value);
			std::string val(value.str);
			if (!val.compare("org.bluez.Device1"))
			{
				dbus_message_iter_next(&dict1_iter);
				DBusMessageIter array2_iter;
				dbus_message_iter_recurse(&dict1_iter, &array2_iter);
				ssOutput << bluez_dbus_msg_iter(array2_iter, dbusBTAddress);
			}
		} while (dbus_message_iter_next(&array1_iter));
	}
	if (ConsoleVerbosity > 1)
		std::cout << ssOutput.str();
}
void bluez_dbus_msg_PropertiesChanged(DBusMessage* dbus_msg, bdaddr_t& dbusBTAddress)
{
	std::ostringstream ssOutput;
	if (std::string(dbus_message_get_signature(dbus_msg)).compare("sa{sv}as"))
		ssOutput << "Invalid Signature: " << __FILE__ << "(" << __LINE__ << ")" << std::endl;
	else
	{
		// TODO: convert dbus_msg_Path to dbusBTAddress using regex
		const std::string dbus_msg_Path(dbus_message_get_path(dbus_msg)); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga18adf731bb42d324fe2624407319e4af
		std::string BluetoothAddress;
		const std::regex BluetoothAddressRegex("((([[:xdigit:]]{2}_){5}))[[:xdigit:]]{2}");
		std::smatch AddressMatch;
		if (std::regex_search(dbus_msg_Path, AddressMatch, BluetoothAddressRegex))
		{
			BluetoothAddress = AddressMatch.str();
			std::replace(BluetoothAddress.begin(), BluetoothAddress.end(), '_', ':');
			dbusBTAddress = string2ba(BluetoothAddress);
		}
		DBusMessageIter root_iter;
		std::string root_object_path;
		dbus_message_iter_init(dbus_msg, &root_iter);
		DBusBasicValue value;
		dbus_message_iter_get_basic(&root_iter, &value);
		root_object_path = std::string(value.str);
		dbus_message_iter_next(&root_iter);
		DBusMessageIter array_iter;
		dbus_message_iter_recurse(&root_iter, &array_iter);
		ssOutput << bluez_dbus_msg_iter(array_iter, dbusBTAddress);
	}
	if (ConsoleVerbosity > 1)
		std::cout << ssOutput.str();
}
/////////////////////////////////////////////////////////////////////////////
static void usage(int argc, char** argv)
{
	std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
	std::cout << "  " << ProgramVersionString << std::endl;
	std::cout << "  Options:" << std::endl;
	std::cout << "    -h | --help          Print this message" << std::endl;
	std::cout << "    -v | --verbose level stdout verbosity level [" << ConsoleVerbosity << "]" << std::endl;
	std::cout << "    -k | --keyfile filename [" << VictronEncryptionKeyFilename << "]" << std::endl;
	std::cout << "    -l | --log name      Logging Directory [" << LogDirectory << "]" << std::endl;
	std::cout << "    -f | --cache name    cache file directory [" << CacheDirectory << "]" << std::endl;
	std::cout << "    -s | --svg name      SVG output directory [" << SVGDirectory << "]" << std::endl;
	std::cout << "    -C | --controller XX:XX:XX:XX:XX:XX use the controller with this address" << std::endl;
	std::cout << std::endl;
}
static const char short_options[] = "hv:k:l:f:s:C:";
static const struct option long_options[] = {
		{ "help",   no_argument,       NULL, 'h' },
		{ "verbose",required_argument, NULL, 'v' },
		{ "keyfile",required_argument, NULL, 'k' },
		{ "log",    required_argument, NULL, 'l' },
		{ "cache",	required_argument, NULL, 'f' },
		{ "svg",	required_argument, NULL, 's' },
		{ "controller", required_argument, NULL, 'C' },
		{ 0, 0, 0, 0 }
};
int main(int argc, char** argv) 
{
	std::string ControllerAddress;
	for (;;)
	{
		std::filesystem::path TempPath;
		int idx;
		int c = getopt_long(argc, argv, short_options, long_options, &idx);
		if (-1 == c)
			break;
		switch (c)
		{
		case 0: /* getopt_long() flag */
			break;
		case '?':
		case 'h':	// --help
			usage(argc, argv);
			exit(EXIT_SUCCESS);
		case 'v':	// --verbose
			try { ConsoleVerbosity = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		case 'k':	// --keyfile
			TempPath = std::string(optarg);
			if (ReadVictronEncryptionKeys(TempPath))
				VictronEncryptionKeyFilename = TempPath;
			break;
		case 'l':	// --log
			TempPath = std::string(optarg);
			while (TempPath.filename().empty() && (TempPath != TempPath.root_directory())) // This gets rid of the "/" on the end of the path
				TempPath = TempPath.parent_path();
			if (ValidateDirectory(TempPath))
				LogDirectory = TempPath;
			break;
		case 'f':	// --cache
			TempPath = std::string(optarg);
			while (TempPath.filename().empty() && (TempPath != TempPath.root_directory())) // This gets rid of the "/" on the end of the path
				TempPath = TempPath.parent_path();
			if (ValidateDirectory(TempPath))
				CacheDirectory = TempPath;
			break;
		case 's':	// --svg
			TempPath = std::string(optarg);
			while (TempPath.filename().empty() && (TempPath != TempPath.root_directory())) // This gets rid of the "/" on the end of the path
				TempPath = TempPath.parent_path();
			if (ValidateDirectory(TempPath))
				SVGDirectory = TempPath;
			break;
		case 'C':	// --controller
			ControllerAddress = std::string(optarg);
			break;
		default:
			usage(argc, argv);
			exit(EXIT_FAILURE);
		}
	}
	
	if (ConsoleVerbosity > 0)
		std::cout << "[" << getTimeISO8601(true) << "] " << ProgramVersionString << std::endl;
	else
		std::cerr << ProgramVersionString << std::endl;

	if (!SVGDirectory.empty())
	{
		//if (SVGTitleMapFilename.empty()) // If this wasn't set as a parameter, look in the SVG Directory for a default titlemap
		//	SVGTitleMapFilename = std::filesystem::path(SVGDirectory / "gvh-titlemap.txt");
		//ReadTitleMap(SVGTitleMapFilename);
		ReadCacheDirectory(); // if cache directory is configured, read it before reading all the normal logs
		ReadLoggedData(); // only read the logged data if creating SVG files
		GenerateCacheFile(VictronSmartLithiumMRTGLogs); // update cache files if any new data was in logs
		WriteAllSVG();
	}

	ReadVictronEncryptionKeys(VictronEncryptionKeyFilename);

	if (VictronEncryptionKeys.empty())
	{
		if (ConsoleVerbosity > 0)
			std::cout << "[" << getTimeISO8601(true) << "] No Victron Encryption Keys Found! Exiting." << std::endl;
		else
			std::cerr << "No Victron Encryption Keys Found! Exiting." << std::endl;
		exit(EXIT_FAILURE);
	}

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

		std::map<bdaddr_t, std::string> BlueZAdapterMap;
		bool bUse_HCI_Interface = !bluez_find_adapters(dbus_conn, BlueZAdapterMap);
		if (bUse_HCI_Interface && BlueZAdapterMap.empty())
		{
			if (ConsoleVerbosity > 0)
				std::cout << "[" << getTimeISO8601() << "] Could not get list of adapters from BlueZ over DBus. Reverting to HCI interface." << std::endl;
			else
				std::cerr << "Could not get list of adapters from BlueZ over DBus. Reverting to HCI interface." << std::endl;
		}
		if (!BlueZAdapterMap.empty())
		{
			std::string BlueZAdapter(BlueZAdapterMap.cbegin()->second);
			if (!ControllerAddress.empty())
				if (auto search = BlueZAdapterMap.find(string2ba(ControllerAddress)); search != BlueZAdapterMap.end())
					BlueZAdapter = search->second;

			bluez_power_on(dbus_conn, BlueZAdapter.c_str());
			bluez_filter_le(dbus_conn, BlueZAdapter.c_str());
			bluez_dbus_FindExistingDevices(dbus_conn); // This pulls data from BlueZ on devices that BlueZ is already keeping track of
			if (bluez_discovery(dbus_conn, BlueZAdapter.c_str(), true))
			{
				dbus_connection_flush(dbus_conn); // https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#ga10e68d9d2f41d655a4151ddeb807ff54
				std::vector<std::string> MatchRules;
				MatchRules.push_back("type='signal',sender='org.bluez',member='InterfacesAdded'");
				MatchRules.push_back("type='signal',sender='org.bluez',member='PropertiesChanged'");
				for (auto& MatchRule : MatchRules)
				{
					dbus_error_init(&dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusErrors.html#ga8937f0b7cdf8554fa6305158ce453fbe
					dbus_bus_add_match(dbus_conn, MatchRule.c_str(), &dbus_error); // https://dbus.freedesktop.org/doc/api/html/group__DBusBus.html#ga4eb6401ba014da3dbe3dc4e2a8e5b3ef
					if (dbus_error_is_set(&dbus_error))
					{
						std::cout << "Error adding a match rule on the D-Bus system bus: " << dbus_error.message << std::endl;
						dbus_error_free(&dbus_error);
					}
				}
				// Set up CTR-C signal handler
				typedef void(*SignalHandlerPointer)(int);
				SignalHandlerPointer previousHandlerSIGINT = std::signal(SIGINT, SignalHandlerSIGINT);	// Install CTR-C signal handler
				SignalHandlerPointer previousHandlerSIGHUP = std::signal(SIGHUP, SignalHandlerSIGHUP);	// Install Hangup signal handler

				// Main loop
				bRun = true;
				time_t TimeStart(0), TimeSVG(0), TimeAdvertisment(0);
				time(&TimeStart);
				while (bRun)
				{
					// Wait for access to the D-Bus
					if (!dbus_connection_read_write(dbus_conn, 1000)) // https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#ga371163b4955a6e0bf0f1f70f38390c14
					{
						if (ConsoleVerbosity > 0)
							std::cout << "[" << getTimeISO8601() << "] D-Bus connection was closed" << std::endl;
						else
							std::cerr << "D-Bus connection was closed" << std::endl;
						bRun = false;
					}
					else
					{
						// Pop first message on D-Bus connection
						DBusMessage* dbus_msg = dbus_connection_pop_message(dbus_conn); // https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#ga1e40d994ea162ce767e78de1c4988566

						// If there is nothing to receive we get a NULL
						if (dbus_msg != nullptr)
						{
							if (DBUS_MESSAGE_TYPE_SIGNAL == dbus_message_get_type(dbus_msg))
							{
								const std::string dbus_msg_Member(dbus_message_get_member(dbus_msg)); // https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#gaf5c6b705c53db07a5ae2c6b76f230cf9
								bdaddr_t localBTAddress({ 0 });
								if (!dbus_msg_Member.compare("InterfacesAdded"))
									bluez_dbus_msg_InterfacesAdded(dbus_msg, localBTAddress);
								else if (!dbus_msg_Member.compare("PropertiesChanged"))
									bluez_dbus_msg_PropertiesChanged(dbus_msg, localBTAddress);
							}
							dbus_message_unref(dbus_msg); // Free the message
						}
					}
					time_t TimeNow;
					time(&TimeNow);
					if ((!SVGDirectory.empty()) && (difftime(TimeNow, TimeSVG) > DAY_SAMPLE))
					{
						if (ConsoleVerbosity > 0)
							std::cout << "[" << getTimeISO8601() << "] " << std::dec << DAY_SAMPLE << " seconds or more have passed. Writing SVG Files" << std::endl;
						TimeSVG = (TimeNow / DAY_SAMPLE) * DAY_SAMPLE; // hack to try to line up TimeSVG to be on a five minute period
						WriteAllSVG();
					}
					const int LogFileTime(60);
					if (difftime(TimeNow, TimeStart) > LogFileTime)
					{
						if (ConsoleVerbosity > 0)
							std::cout << "[" << getTimeISO8601(true) << "] " << std::dec << LogFileTime << " seconds or more have passed. Writing LOG Files" << std::endl;
						TimeStart = TimeNow;
						GenerateLogFile(VictronVirtualLog);
						GenerateCacheFile(VictronSmartLithiumMRTGLogs); // flush FakeMRTG data to cache files
					}
				}
				bluez_discovery(dbus_conn, BlueZAdapter.c_str(), false);

				std::signal(SIGHUP, previousHandlerSIGHUP);	// Restore original Hangup signal handler
				std::signal(SIGINT, previousHandlerSIGINT);	// Restore original Ctrl-C signal handler

				GenerateLogFile(VictronVirtualLog);	// flush contents of accumulated map to logfiles
				//GenerateCacheFile(GoveeMRTGLogs); // flush FakeMRTG data to cache files
			}
			bluez_filter_le(dbus_conn, BlueZAdapter.c_str(), false, false); // remove discovery filter
		}

		// Close the connection. When using the System Bus, unreference the connection instead of closing it
		dbus_connection_unref(dbus_conn);
	}
	std::cerr << ProgramVersionString << " (exiting)" << std::endl;
	return(EXIT_SUCCESS);
}
