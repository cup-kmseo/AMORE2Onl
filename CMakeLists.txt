# ------------------------------------------------------------
# AMORE2Onl top-level CMakeLists.txt (Linux Optimized & Relocatable)
# ------------------------------------------------------------
cmake_minimum_required ( VERSION 3.16 )
project ( AMORE2Onl VERSION 1.0.0 LANGUAGES CXX )

# ---- C++ Standard ----
set ( CMAKE_CXX_STANDARD 17 )
set ( CMAKE_CXX_STANDARD_REQUIRED ON )
set ( CMAKE_CXX_EXTENSIONS OFF )

# Use standard GNU installation directories
include ( GNUInstallDirs )

# ---- Relocatable RPATH Settings (Linux) ----
set ( CMAKE_SKIP_BUILD_RPATH FALSE )
set ( CMAKE_BUILD_WITH_INSTALL_RPATH FALSE )
set ( CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE )
set ( CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}" )

# ---- ROOT ----
find_package ( ROOT REQUIRED COMPONENTS Core RIO )
include ( ${ROOT_USE_FILE} )

# Prevent absolute paths from being hardcoded in ROOT dictionaries
set ( CMAKE_ROOT_DICT_ARGS --noinclude )
set ( ROOT_GENERATE_DICTIONARY_RELATIVE ON )

# ---- libusb (via pkg-config) ----
find_package ( PkgConfig REQUIRED )
pkg_check_modules ( LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0 )

# ---- yaml-cpp for configuration ----
find_package ( yaml-cpp REQUIRED )

# ---- zeromq (cppzmq) for message server ----
find_package ( cppzmq REQUIRED )

# ---- HDF5 (Required) ----
# Find HDF5 and halt the build process with a fatal error if not found.
find_package ( HDF5 REQUIRED COMPONENTS C CXX HL )
message ( STATUS "HDF5 found (${HDF5_VERSION}): enabling HDF5-based utilities" )
add_compile_definitions ( ENABLE_HDF5 )

# ---- CUPDAQ & CUPDataModel (external shared packages) ----
set ( CUPDAQ_ROOT "/home/cupsoft/Products/CUPDAQ/V4.3"
      CACHE PATH "Path to installed CUPDAQ" )
set ( CUPDATAMODEL_ROOT "/home/cupsoft/Products/CUPDATAMODEL/V1.1"
      CACHE PATH "Path to installed CUPDataModel" )
list ( APPEND CMAKE_PREFIX_PATH "${CUPDAQ_ROOT}" "${CUPDATAMODEL_ROOT}" )

find_package ( CUPDataModel REQUIRED )
find_package ( CUPDAQ REQUIRED )

set ( CUPDAQ_INCLUDE_DIR       "${CUPDAQ_ROOT}/include" )
set ( CUPDATAMODEL_INCLUDE_DIR "${CUPDATAMODEL_ROOT}/include" )
set ( CUPDAQ_ENABLE_HDF5 ON )

message ( STATUS "CUPDAQ found: ${CUPDAQ_INCLUDE_DIR}" )
message ( STATUS "CUPDataModel found: ${CUPDATAMODEL_INCLUDE_DIR}" )

# ---- Global Compiler Settings ----
add_compile_options ( -O2 )

# ---- AMORE2Onl Specific Subdirectories ----
# Add your specific modules here when ready
add_subdirectory ( AMORE )
add_subdirectory ( AMOREAlgs )
add_subdirectory ( AMORESystem )
add_subdirectory ( AMOREHDF5 )
add_subdirectory ( AMOREDAQ )

# ---- Environment Setup Script Generation (setup_amore2onl.sh) ----
# Create a unified setup script for both AMORE2Onl and CUPDAQ
file ( WRITE ${CMAKE_BINARY_DIR}/setup_amore2onl.sh
  "#!/bin/bash
# AMORE2Onl & CUPDAQ Unified Environment Setup Script
# Generated automatically by CMake.

export AMORE2ONL_DIR=\"\$(builtin cd \"\$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"

# Update system PATH
export PATH=\"\$AMORE2ONL_DIR/bin:\$PATH\"

# Update library search path
export LD_LIBRARY_PATH=\"\$AMORE2ONL_DIR/${CMAKE_INSTALL_LIBDIR}:\$LD_LIBRARY_PATH\"

# Update ROOT include path
export ROOT_INCLUDE_PATH=\"\$AMORE2ONL_DIR/include:\$ROOT_INCLUDE_PATH\"

echo \"[AMORE2Onl] Environment loaded from: \$AMORE2ONL_DIR\"
" )

install ( FILES ${CMAKE_BINARY_DIR}/setup_amore2onl.sh DESTINATION . )
