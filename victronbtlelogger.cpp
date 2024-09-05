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

class VictronSmartLithium
{
public:
	time_t Time;
	std::string WriteTXT(const char seperator = '\t') const;
	std::string WriteCache(void) const;
	std::string WriteConsole(void) const;
	bool ReadCache(const std::string& data);
protected:
	unsigned int bms_flags;
	unsigned int smartlithium_error;
	unsigned int cell[8];
	unsigned int battery_voltage;
	unsigned int balancer_status;
	unsigned int battery_temperature;
};
std::string VictronSmartLithium::WriteTXT(const char seperator) const
{
	std::ostringstream ssValue;
	ssValue << timeToExcelDate(Time);
	ssValue << seperator << std::setfill('0') << std::hex << std::setw(8) << bms_flags;
	ssValue << seperator << std::setfill('0') << std::hex << std::setw(4) << smartlithium_error;
	for (auto & a : cell)
		ssValue << seperator << std::setfill('0') << std::hex << std::setw(2) << a;
	ssValue << seperator << std::setfill('0') << std::hex << std::setw(4) << battery_voltage;
	ssValue << seperator << std::setfill('0') << std::hex << std::setw(4) << balancer_status;
	ssValue << seperator << std::setfill('0') << std::hex << std::setw(4) << battery_temperature;
	return(ssValue.str());
}
std::string VictronSmartLithium::WriteCache(void) const
{
	std::ostringstream ssValue;
	ssValue << timeToExcelDate(Time);
	ssValue << "\t" << std::setfill('0') << std::hex << std::setw(8) << bms_flags;
	ssValue << "\t" << std::setfill('0') << std::hex << std::setw(4) << smartlithium_error;
	for (auto& a : cell)
		ssValue << "\t" << std::setfill('0') << std::hex << std::setw(2) << a;
	ssValue << "\t" << std::setfill('0') << std::hex << std::setw(4) << battery_voltage;
	ssValue << "\t" << std::setfill('0') << std::hex << std::setw(4) << balancer_status;
	ssValue << "\t" << std::setfill('0') << std::hex << std::setw(4) << battery_temperature;
	return(ssValue.str());
}
std::string VictronSmartLithium::WriteConsole(void) const
{
	std::ostringstream ssValue;
	for (auto& a : cell)
		if (a != 0x7f)
			ssValue << " Cell: " << float(a) * 0.01 + 2.60 << "V";
	ssValue << " Battery: " << float(battery_voltage) * 0.01 << "V";
	ssValue << " Temperature: " << battery_temperature - 40 << "\u00B0" << "C";
	return(ssValue.str());
}
bool VictronSmartLithium::ReadCache(const std::string& data)
{
	bool rval = false;
	std::istringstream ssValue(data);
	ssValue >> Time;
	ssValue >> bms_flags;
	ssValue >> smartlithium_error;
	for (auto& a : cell)
		ssValue >> a;
	ssValue >> battery_voltage;
	ssValue >> balancer_status;
	ssValue >> battery_temperature;
	return(rval);
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
					ssOutput << "[" << timeToISO8601(TimeNow, true) << "] [" << ba2string(dbusBTAddress) << "] " << Key << ": " << value.str << std::endl;
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
																if (ConsoleVerbosity > 0)
																{
																	VictronExtraData_t* ExtraDataPtr = (VictronExtraData_t*)(ManufacturerData.data()+8);
																	ssOutput << std::dec;
																	if (ManufacturerData[4] == 0x01) // Solar Charger
																	{
																		ssOutput << " (Solar)";
																		ssOutput << " battery_current:" << float(ExtraDataPtr->SolarCharger.battery_current) * 0.01 << "V";
																		ssOutput << " battery_voltage:" << float(ExtraDataPtr->SolarCharger.battery_voltage) * 0.01 << "V";
																		ssOutput << " load_current:" << float(ExtraDataPtr->SolarCharger.load_current) * 0.01 << "V";
																	}
																	if (ManufacturerData[4] == 0x04) // DC/DC converter
																	{
																		ssOutput << " (DC/DC)";
																		ssOutput << " input_voltage:" << float(ExtraDataPtr->DCDCConverter.input_voltage) * 0.01 + 2.60 << "V";
																		ssOutput << " output_voltage:" << float(ExtraDataPtr->DCDCConverter.output_voltage) * 0.01 + 2.60 << "V";
																	}
																	if (ManufacturerData[4] == 0x05) // SmartLithium
																	{
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
																	if (ManufacturerData[4] == 0x0f) // Orion XS DC/DC converter
																	{
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

	ReadVictronEncryptionKeys(VictronEncryptionKeyFilename);

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
					//if ((!SVGDirectory.empty()) && (difftime(TimeNow, TimeSVG) > DAY_SAMPLE))
					//{
					//	if (ConsoleVerbosity > 0)
					//		std::cout << "[" << getTimeISO8601() << "] " << std::dec << DAY_SAMPLE << " seconds or more have passed. Writing SVG Files" << std::endl;
					//	TimeSVG = (TimeNow / DAY_SAMPLE) * DAY_SAMPLE; // hack to try to line up TimeSVG to be on a five minute period
					//	WriteAllSVG();
					//}
					const int LogFileTime(60);
					if (difftime(TimeNow, TimeStart) > LogFileTime)
					{
						if (ConsoleVerbosity > 0)
							std::cout << "[" << getTimeISO8601(true) << "] " << std::dec << LogFileTime << " seconds or more have passed. Writing LOG Files" << std::endl;
						TimeStart = TimeNow;
						GenerateLogFile(VictronVirtualLog);
						//GenerateCacheFile(GoveeMRTGLogs); // flush FakeMRTG data to cache files
						//MonitorLoggedData();
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
	return 0;
}
