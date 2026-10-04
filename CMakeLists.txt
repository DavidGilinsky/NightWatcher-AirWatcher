# ---------------------------------------------------------------------------
# Author:        David Gilinsky
# File:          CMakeLists.txt
# Purpose:       Top-level CMake build for AirWatcher: the airwatcherd daemon
#                (copier + own web UI + NightWatcher extension registration).
# Created:       2026-07-22
# Last Modified: 2026-10-04
# Version:       0.1.1
# License:       GPL-3.0-or-later
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

project(AirWatcher
    VERSION 0.1.1
    DESCRIPTION "Pull FITS frames off ZWO ASIAir devices into the NightWatcher ingest tree"
    HOMEPAGE_URL "https://github.com/DavidGilinsky/NightWatcher-AirWatcher"
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Self-contained install bundle under one prefix (matches NightWatcher2).
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr/local/airwatcher" CACHE PATH
        "Install prefix (self-contained bundle)" FORCE)
endif()

option(AW_BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MARIADB REQUIRED libmariadb)
find_package(OpenSSL REQUIRED)

# libsmbclient (Samba) has no pkg-config file on Debian; the header lives in a
# versioned include dir and the lib links as -lsmbclient.
find_path(SMBCLIENT_INCLUDE_DIR libsmbclient.h PATH_SUFFIXES samba-4.0)
find_library(SMBCLIENT_LIB NAMES smbclient)
if(NOT SMBCLIENT_INCLUDE_DIR OR NOT SMBCLIENT_LIB)
    message(FATAL_ERROR "libsmbclient not found -- install libsmbclient-dev")
endif()

# ---- Shared warning flags ----
add_library(aw_warnings INTERFACE)
target_compile_options(aw_warnings INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic>)

# ---- Generated version header ----
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/airwatcher/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/airwatcher/version.hpp
    @ONLY)

# ---- Core (config + logging), shared by the daemon and tests ----
add_library(aw_core STATIC
    src/daemon/config.cpp
    src/daemon/logging.cpp)
target_include_directories(aw_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/daemon
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(aw_core PUBLIC aw_warnings)

# ---- Database layer (libmariadb) ----
add_library(aw_db STATIC src/db/database.cpp)
target_include_directories(aw_db PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db
    ${MARIADB_INCLUDE_DIRS})
target_link_directories(aw_db PUBLIC ${MARIADB_LIBRARY_DIRS})
target_link_libraries(aw_db PUBLIC aw_warnings ${MARIADB_LIBRARIES})

# ---- SMB client (libsmbclient / Samba) ----
add_library(aw_smb STATIC src/smb/smb_client.cpp)
target_include_directories(aw_smb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/smb
    ${SMBCLIENT_INCLUDE_DIR})
target_link_libraries(aw_smb PUBLIC aw_warnings ${SMBCLIENT_LIB})

# ---- ASIAir subnet discovery ----
add_library(aw_discovery STATIC src/discovery/discovery.cpp)
target_include_directories(aw_discovery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/discovery)
target_link_libraries(aw_discovery PUBLIC aw_warnings Threads::Threads)

# ---- Copy engine + policy scheduler ----
add_library(aw_copier STATIC src/copier/copier.cpp src/copier/fits_check.cpp)
target_include_directories(aw_copier PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/copier)
target_link_libraries(aw_copier PUBLIC aw_core aw_db aw_smb Threads::Threads)

# ---- Vendored cpp-httplib with OpenSSL client/server support ----
add_library(aw_httplib INTERFACE)
target_include_directories(aw_httplib SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib)
target_compile_definitions(aw_httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(aw_httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# ---- Self-signed TLS helper (vendored from NightWatcher2) ----
add_library(aw_tls STATIC src/api/tls_cert.cpp)
target_include_directories(aw_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/api)
target_link_libraries(aw_tls PUBLIC aw_warnings OpenSSL::SSL OpenSSL::Crypto)

# ---- Embedded web server + JSON API ----
add_library(aw_api STATIC src/api/http_server.cpp)
target_include_directories(aw_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/api)
target_link_libraries(aw_api PUBLIC aw_core aw_db aw_smb aw_discovery aw_tls aw_httplib)

# ---- The daemon ----
add_executable(airwatcherd src/daemon/main.cpp)
target_link_libraries(airwatcherd PRIVATE aw_core aw_db aw_smb aw_discovery aw_copier aw_api Threads::Threads)

# ---- Tests ----
if(AW_BUILD_TESTS)
    enable_testing()
    # Live DB round-trip; needs a MariaDB (env NWDB_PASSWORD etc.). Gated in CI.
    add_executable(db_smoke tests/db_smoke.cpp)
    target_link_libraries(db_smoke PRIVATE aw_db)
    add_test(NAME db_smoke COMMAND db_smoke)

    # Manual SMB probe (against a mock share or real hardware); not a ctest.
    add_executable(smb_probe tests/smb_probe.cpp)
    target_link_libraries(smb_probe PRIVATE aw_smb)

    # Discovery: cidr_hosts unit checks + a live scan probe.
    add_executable(discovery_probe tests/discovery_probe.cpp)
    target_link_libraries(discovery_probe PRIVATE aw_discovery)
    add_test(NAME discovery_probe COMMAND discovery_probe)

    # End-to-end copier against a mock ASIAir share; needs nwdb + a mock share.
    add_executable(copier_probe tests/copier_probe.cpp)
    target_link_libraries(copier_probe PRIVATE aw_copier)

    # Pure unit checks: the whole-FITS-file verifier and the settle gate.
    add_executable(fits_check_test tests/fits_check_test.cpp)
    target_link_libraries(fits_check_test PRIVATE aw_copier)
    add_test(NAME fits_check_test COMMAND fits_check_test)
    add_executable(settle_test tests/settle_test.cpp)
    target_link_libraries(settle_test PRIVATE aw_copier)
    add_test(NAME settle_test COMMAND settle_test)
endif()

# ---- Install (self-contained bundle under CMAKE_INSTALL_PREFIX) ----
configure_file(config/airwatcher.conf.example.in
               ${CMAKE_CURRENT_BINARY_DIR}/airwatcher.conf.example @ONLY)
configure_file(systemd/airwatcherd.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/airwatcherd.service @ONLY)

install(TARGETS airwatcherd RUNTIME DESTINATION bin)
install(DIRECTORY web/ DESTINATION web)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/airwatcher.conf.example DESTINATION share)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/airwatcherd.service DESTINATION lib/systemd/system)

# ---- .deb via CPack (debconf-driven install; postinst does /etc + systemd + user) ----
set(CPACK_PACKAGE_NAME "airwatcher")
set(CPACK_PACKAGE_VENDOR "SAZ Dark Sky Association")
set(CPACK_PACKAGE_CONTACT "David Gilinsky <gilinsky@gilinskyresearch.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)             # auto Depends from the ELF binary
set(CPACK_DEBIAN_PACKAGE_DEPENDS "adduser, debconf (>= 0.5) | debconf-2.0")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "mariadb-server | mysql-server")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)  # 0755 maintainer scripts
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")          # airwatcher_<ver>_<arch>.deb

configure_file(debian/postinst.in ${CMAKE_CURRENT_BINARY_DIR}/debian/postinst @ONLY)
configure_file(debian/prerm.in    ${CMAKE_CURRENT_BINARY_DIR}/debian/prerm    @ONLY)
configure_file(debian/postrm.in   ${CMAKE_CURRENT_BINARY_DIR}/debian/postrm   @ONLY)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_BINARY_DIR}/debian/postinst"
    "${CMAKE_CURRENT_BINARY_DIR}/debian/prerm"
    "${CMAKE_CURRENT_BINARY_DIR}/debian/postrm"
    "${CMAKE_CURRENT_SOURCE_DIR}/debian/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/debian/templates")

include(CPack)
