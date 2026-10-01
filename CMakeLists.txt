cmake_minimum_required(VERSION 3.25)
project(dune_daq_phlex VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Suppress spurious GCC 14/15 header warnings (else fatal under -Werror).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "14.1")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15")
      add_compile_options(-Wno-stringop-overflow)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16")
      add_compile_options(-Wno-array-bounds)
    endif()
  endif()
endif()

add_compile_options(-Wall -Wpedantic -Werror)

# Dependencies.  Phlex >= 0.4.1 finds its own dependencies and installs
# phlex/driver.hpp.  The WireCell-using Configs need FindWireCell.cmake on the
# module path -- so find wire_cell_arrow (whose Config adds it) before the
# remaining sibling Configs.
find_package(Arrow CONFIG REQUIRED)
find_package(HDF5 REQUIRED COMPONENTS C)
find_package(spdlog REQUIRED)
find_package(phlex 0.4.1 REQUIRED)
include(GNUInstallDirs)
find_package(wire_cell_arrow CONFIG REQUIRED)
find_package(phlex_arrow_common CONFIG REQUIRED)
find_package(dune_daq_hdf CONFIG REQUIRED)
find_package(dune_daq_codec CONFIG REQUIRED)

# Link the Spack view's gcc15 libstdc++ explicitly (GLIBCXX/CXXABI resolution).
# Resolve THIS compiler's libstdc++ directly, independent of CMAKE_PREFIX_PATH
# order (a Spack build can place an older gcc-runtime earlier on the path).
execute_process(
  COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.so
  OUTPUT_VARIABLE _stdc_real OUTPUT_STRIP_TRAILING_WHITESPACE)
file(REAL_PATH "${_stdc_real}" _stdc_real)
get_filename_component(_stdc_dir "${_stdc_real}" DIRECTORY)
set(DUNE_DAQ_PHLEX_RUNTIME_LIBS "${_stdc_real}")
link_directories(BEFORE "${_stdc_dir}")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,-rpath,${_stdc_dir}")

# Implementation library: the DAQ-HDF5 -> decode -> WCT IFrame -> Arrow
# "wc.frame" TableGroup read logic and the phlex::source that wraps it.
add_library(dune_daq_phlex SHARED
  dune_daq_phlex/DaqFrameSource.cpp
)
target_include_directories(dune_daq_phlex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(dune_daq_phlex
  PUBLIC
    phlex_arrow_common::phlex_arrow_common
    phlex::model
    phlex::core          # DaqFrameSource: phlex::source, product_selector::match
    dune_daq_hdf::dune_daq_hdf
    dune_daq_codec::dune_daq_codec
    wire_cell_arrow::wire_cell_arrow
    WireCell::Aux WireCell::Iface WireCell::Util
    Arrow::arrow_shared
    spdlog::spdlog
    ${DUNE_DAQ_PHLEX_RUNTIME_LIBS}
)

# The Phlex read source plugin (dlopen MODULE): loading it registers the DUNE
# DAQ HDF5 read source (DaqFrameSource, a phlex::source).  PHLEX prepends "lib"
# and appends ".so" to the workflow JSON `cpp` field, so target name
# dune_daq_phlex_source -> libdune_daq_phlex_source.so matches cpp:
# 'dune_daq_phlex_source'.
add_library(dune_daq_phlex_source MODULE
  dune_daq_phlex/SourceModule.cpp
)
target_link_libraries(dune_daq_phlex_source PRIVATE
  dune_daq_phlex
  phlex::module
  ${DUNE_DAQ_PHLEX_RUNTIME_LIBS}
)

# The Phlex read driver plugin (dlopen MODULE): loading it registers the
# file-driven driver that drives the cells a DaqFrameSource discovered.
add_library(dune_daq_phlex_driver MODULE
  dune_daq_phlex/DriverModule.cpp
)
target_link_libraries(dune_daq_phlex_driver PRIVATE
  dune_daq_phlex
  phlex::module
  ${DUNE_DAQ_PHLEX_RUNTIME_LIBS}
)

# dune-daq-phlex is a PURE PLUGIN package: it produces the Phlex source + driver
# plugins (dlopen MODULEs) plus the implementation library they link.  Its
# headers are PRIVATE and are deliberately NOT installed and NO CMake Config is
# exported -- nothing should build against this "final" package; loading the
# plugins is its only interface.  The implementation .so is installed because
# the plugins link it at runtime.
install(TARGETS dune_daq_phlex
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(TARGETS dune_daq_phlex_source
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(TARGETS dune_daq_phlex_driver
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
