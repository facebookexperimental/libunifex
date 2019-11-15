# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the Apache License found in the
# LICENSE.txt file in the root directory of this source tree.

file(GLOB test-sources "*_test.cpp")
foreach(file-path ${test-sources})
    string( REPLACE ".cpp" "" file-path-without-ext ${file-path} )
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_executable( ${file-name} ${file-path})
    target_link_libraries( ${file-name} PUBLIC unifex)
    add_test(NAME "test-${file-name}" COMMAND ${file-name})
endforeach()

if (NOT "x${UNIFEX_PMR_LIBRARY}" STREQUAL "x")
  target_link_libraries(any_unique_test PUBLIC ${UNIFEX_PMR_LIBRARY})
  target_link_libraries(submit_allocator_customisation_test PUBLIC ${UNIFEX_PMR_LIBRARY})
endif()
if (NOT "x${UNIFEX_PMR_LINK_PATH}" STREQUAL "x")
  target_link_options(any_unique_test PUBLIC "${UNIFEX_PMR_LINK_PATH}")
  target_link_options(submit_allocator_customisation_test PUBLIC "${UNIFEX_PMR_LINK_PATH}")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  file(GLOB linux-test-sources "linux/*_test.cpp")
  foreach(file-path ${linux-test-sources})
    string( REPLACE ".cpp" "" file-path-without-ext ${file-path} )
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_executable( ${file-name} ${file-path})
    target_link_libraries( ${file-name} PUBLIC unifex)
    add_test(NAME "test-${file-name}" COMMAND ${file-name})
  endforeach()
endif()
