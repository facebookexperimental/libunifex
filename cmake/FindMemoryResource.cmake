# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the license found in the
# LICENSE.txt file in the root directory of this source tree.

#[=======================================================================[.rst:

FindMemoryResource
##############

This module supports the C++17 standard library's memory_resource utilities. Use
the :imp-target:`std::memory_resource` imported target to

Options
*******

The ``COMPONENTS`` argument to this module supports the following values:

.. find-component:: Experimental
    :name: pmr.Experimental

    Allows the module to find the "experimental" Library Fundamentals 2 TS
    version of the MemoryResouce library. This is the library that should be
    used with the ``std::experimental::pmr`` namespace.

.. find-component:: Final
    :name: pmr.Final

    Finds the final C++17 standard version of the MemoryResource library.

If no components are provided, behaves as if the
:find-component:`pmr.Final` component was specified.

If both :find-component:`pmr.Experimental` and :find-component:`pmr.Final` are
provided, first looks for ``Final``, and falls back to ``Experimental`` in case
of failure. If ``Final`` is found, :imp-target:`std::memory_resource` and all
:ref:`variables <pmr.variables>` will refer to the ``Final`` version.


Imported Targets
****************

.. imp-target:: std::memory_resource

    The ``std::memory_resource`` imported target is defined when any requested
    version of the C++ memory_resource library has been found, whether it is
    *Experimental* or *Final*.

    If no version of the memory_resource library is available, this target will not
    be defined.

    .. note::
        This target has ``cxx_std_17`` as an ``INTERFACE``
        :ref:`compile language standard feature <req-lang-standards>`. Linking
        to this target will automatically enable C++17 if no later standard
        version is already required on the linking target.


.. _pmr.variables:

Variables
*********

.. variable:: CXX_MEMORY_RESOURCE_HAVE_PMR

    Set to ``TRUE`` when a memory_resource header was found.

.. variable:: CXX_MEMORY_RESOURCE_HEADER

    Set to either ``memory_resource`` or ``experimental/memory_resource`` depending on
    whether :find-component:`pmr.Final` or :find-component:`pmr.Experimental` was
    found.

.. variable:: CXX_MEMORY_RESOURCE_NAMESPACE

    Set to either ``std::pmr`` or ``std::experimental::pmr``
    depending on whether :find-component:`pmr.Final` or
    :find-component:`pmr.Experimental` was found.


Examples
********

Using `find_package(MemoryResource)` with no component arguments:

.. code-block:: cmake

    find_package(MemoryResource REQUIRED)

    add_executable(my-program main.cpp)
    target_link_libraries(my-program PRIVATE std::memory_resource)


#]=======================================================================]


if(TARGET std::memory_resource)
    # This module has already been processed. Don't do it again.
    return()
endif()

include(CMakePushCheckState)
include(CheckIncludeFileCXX)
include(CheckCXXSourceCompiles)

cmake_push_check_state()

set(CMAKE_REQUIRED_QUIET ${MemoryResource_FIND_QUIETLY})

# All of our tests required C++17 or later
if("x${CMAKE_CXX_COMPILER_ID}" MATCHES "x.*Clang" AND "x${CMAKE_CXX_SIMULATE_ID}" STREQUAL "xMSVC")
  set(CMAKE_REQUIRED_FLAGS "/std:c++17")
elseif("x${CMAKE_CXX_COMPILER_ID}" STREQUAL "xMSVC")
  set(CMAKE_REQUIRED_FLAGS "/std:c++17")
else()
  set(CMAKE_REQUIRED_FLAGS "-std=c++17")
endif()

# Normalize and check the component list we were given
set(want_components ${MemoryResource_FIND_COMPONENTS})
if(MemoryResource_FIND_COMPONENTS STREQUAL "")
    set(want_components Final)
endif()

# Warn on any unrecognized components
set(extra_components ${want_components})
list(REMOVE_ITEM extra_components Final Experimental)
foreach(component IN LISTS extra_components)
    message(WARNING "Extraneous find_package component for MemoryResource: ${component}")
endforeach()

# Detect which of Experimental and Final we should look for
set(find_experimental TRUE)
set(find_final TRUE)
if(NOT "Final" IN_LIST want_components)
    set(find_final FALSE)
endif()
if(NOT "Experimental" IN_LIST want_components)
    set(find_experimental FALSE)
endif()

if(find_final)
    check_include_file_cxx("memory_resource" _CXX_MEMORY_RESOURCE_HAVE_HEADER)
    mark_as_advanced(_CXX_MEMORY_RESOURCE_HAVE_HEADER)
    if(_CXX_MEMORY_RESOURCE_HAVE_HEADER)
        # We found the non-experimental header. Don't bother looking for the
        # experimental one.
        set(find_experimental FALSE)
    endif()
else()
    set(_CXX_MEMORY_RESOURCE_HAVE_HEADER FALSE)
endif()

if(find_experimental)
    check_include_file_cxx("experimental/memory_resource" _CXX_MEMORY_RESOURCE_HAVE_EXPERIMENTAL_HEADER)
    mark_as_advanced(_CXX_MEMORY_RESOURCE_HAVE_EXPERIMENTAL_HEADER)
else()
    set(_CXX_MEMORY_RESOURCE_HAVE_EXPERIMENTAL_HEADER FALSE)
endif()

if(_CXX_MEMORY_RESOURCE_HAVE_HEADER)
    set(_have_pmr TRUE)
    set(_pmr_header memory_resource)
    set(_pmr_namespace std::pmr)
elseif(_CXX_MEMORY_RESOURCE_HAVE_EXPERIMENTAL_HEADER)
    set(_have_pmr TRUE)
    set(_pmr_header experimental/memory_resource)
    set(_pmr_namespace std::experimental::pmr)
else()
    set(_have_pmr FALSE)
endif()

set(CXX_MEMORY_RESOURCE_HAVE_PMR ${_have_pmr} CACHE BOOL "TRUE if we have the C++ memory_resource headers")
set(CXX_MEMORY_RESOURCE_HEADER ${_pmr_header} CACHE STRING "The header that should be included to obtain the memory_resource APIs")
set(CXX_MEMORY_RESOURCE_NAMESPACE ${_pmr_namespace} CACHE STRING "The C++ namespace that contains the memory_resource APIs")

set(_found FALSE)

if(CXX_MEMORY_RESOURCE_HAVE_PMR)
    # We have some memory_resource library available. Do link checks
    string(CONFIGURE [[
        #include <@CXX_MEMORY_RESOURCE_HEADER@>

        int main() {
            @CXX_MEMORY_RESOURCE_NAMESPACE@::polymorphic_allocator<char> alloc{
                @CXX_MEMORY_RESOURCE_NAMESPACE@::new_delete_resource()};
            (void) alloc;
        }
    ]] code @ONLY)

    # Try to compile a simple memory_resource program without any linker flags
    check_cxx_source_compiles("${code}" CXX_MEMORY_RESOURCE_NO_LINK_NEEDED)

    set(can_link ${CXX_MEMORY_RESOURCE_NO_LINK_NEEDED})

    if(NOT CXX_MEMORY_RESOURCE_NO_LINK_NEEDED)
        set(prev_libraries ${CMAKE_REQUIRED_LIBRARIES})
        # Add the libstdc++experimental flag
        set(CMAKE_REQUIRED_LIBRARIES ${prev_libraries} -lstdc++experimental)
        check_cxx_source_compiles("${code}" CXX_MEMORY_RESOURCE_STDCPPEXPERIMENTAL_NEEDED)
        set(can_link ${CXX_MEMORY_RESOURCE_STDCPPEXPERIMENTAL_NEEDED})
        if(NOT CXX_MEMORY_RESOURCE_STDCPPEXPERIMENTAL_NEEDED)
            # Try the libc++experimental flag
            set(CMAKE_REQUIRED_LIBRARIES ${prev_libraries} -lc++experimental)
            check_cxx_source_compiles("${code}" CXX_MEMORY_RESOURCE_CPPEXPERIMENTAL_NEEDED)
            set(can_link ${CXX_MEMORY_RESOURCE_CPPEXPERIMENTAL_NEEDED})
        endif()
    endif()

    if(can_link)
        add_library(std::memory_resource INTERFACE IMPORTED)
        target_compile_features(std::memory_resource INTERFACE cxx_std_17)
        set(_found TRUE)

        if(CXX_MEMORY_RESOURCE_NO_LINK_NEEDED)
            # Nothing to add...
        elseif(CXX_MEMORY_RESOURCE_STDCPPEXPERIMENTAL_NEEDED)
            target_link_libraries(std::memory_resource INTERFACE -lstdc++experimental)
        elseif(CXX_MEMORY_RESOURCE_CPPEXPERIMENTAL_NEEDED)
            target_link_libraries(std::memory_resource INTERFACE -lc++experimental)
        endif()
    else()
        set(CXX_MEMORY_RESOURCE_HAVE_PMR FALSE)
    endif()
endif()

cmake_pop_check_state()

set(MemoryResource_FOUND ${_found} CACHE BOOL "TRUE if we can compile and link a program using std::memory_resource" FORCE)

if(MemoryResource_FIND_REQUIRED AND NOT MemoryResource_FOUND)
    message(FATAL_ERROR "Cannot compile simple program using std::memory_resource")
endif()
