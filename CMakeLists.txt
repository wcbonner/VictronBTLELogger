# CMakeList.txt : CMake project for VictronBTLELogger, include source and define
# project specific logic here.
#

# This set of commands should build the package
# git clone https://github.com/wcbonner/VictronBTLELogger.git
# cmake -S VictronBTLELogger -B VictronBTLELogger/build
# cmake --build VictronBTLELogger/build
# pushd VictronBTLELogger/build && cpack . && popd

cmake_minimum_required(VERSION 3.12.0)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

if (POLICY CMP0115)
  cmake_policy(SET CMP0115 NEW)
endif()

project (VictronBTLELogger
    VERSION 3.20240911.0
    DESCRIPTION "Log Victron Direct Bluetooth LE messages and graph battery states"
    HOMEPAGE_URL https://github.com/wcbonner/VictronBTLELogger
)

configure_file(
    victronbtlelogger-version.h.in
    victronbtlelogger-version.h)

include(FindPkgConfig)
if(NOT PKG_CONFIG_FOUND)
  message(FATAL_ERROR "pkg-config not found!" )
endif()
# find_package(FindPkgConfig REQUIRED)

pkg_check_modules(DBUS dbus-1)
if(NOT DBUS_FOUND)
  message(FATAL_ERROR "dbus-1 not found! sudo apt install libdbus-1-dev" )
endif()

pkg_check_modules(CRYPTO libcrypto)
if(NOT CRYPTO_FOUND)
  message(FATAL_ERROR "crypto not found! sudo apt install libssl-dev" )
endif()

# Add source to this project's executable.
add_executable (victronbtlelogger
    victronbtlelogger.cpp
    victronbtlelogger-version.h
    wimiso8601.cpp
    wimiso8601.h
    )

target_link_libraries(victronbtlelogger
    -lstdc++fs
    ${DBUS_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    )

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET victronbtlelogger PROPERTY CXX_STANDARD 17)
endif()

target_include_directories(victronbtlelogger PUBLIC
    "${PROJECT_BINARY_DIR}"
    ${EXTRA_INCLUDES}
    ${DBUS_INCLUDE_DIRS}
    ${CRYPTO_INCLUDE_DIRS}
    )

target_compile_options(victronbtlelogger PUBLIC 
    ${DBUS_CFLAGS_OTHER}
    ${CRYPTO_CFLAGS_OTHER}
    )

# TODO: Add tests and install targets if needed.
include(CTest)
add_test(NAME victronbtlelogger COMMAND victronbtlelogger --help)

install(TARGETS victronbtlelogger
    DESTINATION bin
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(FILES victronbtlelogger.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
    COMPONENT "victronbtlelogger"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_CONTACT "wcbonner@users.noreply.github.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "William C Bonner <${CPACK_PACKAGE_CONTACT}>")
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION custom)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/postinst" "${CMAKE_CURRENT_SOURCE_DIR}/prerm" "${CMAKE_CURRENT_SOURCE_DIR}/postrm")
set(CPACK_STRIP_FILES YES)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS YES)
message(STATUS "Components to pack: ${CPACK_COMPONENTS_ALL}")
include(CPack)
