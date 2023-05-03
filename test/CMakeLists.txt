# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the license found in the
# LICENSE.txt file in the root directory of this source tree.

file(GLOB test-sources "*_test.cpp")
foreach(file-path ${test-sources})
    string( REPLACE ".cpp" "" file-path-without-ext ${file-path} )
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_executable( ${file-name} ${file-path})
    target_link_libraries(${file-name} PUBLIC unifex gtest_main)
    add_test(NAME "test-${file-name}" COMMAND ${file-name})
endforeach()

if(CXX_MEMORY_RESOURCE_HAVE_PMR)
  target_link_libraries(any_unique_test PUBLIC std::memory_resource)
  target_link_libraries(submit_allocator_customisation_test PUBLIC std::memory_resource)
endif()

target_link_libraries(any_sender_of_test PUBLIC gmock)
target_link_libraries(async_manual_reset_event_test PUBLIC gmock)
target_link_libraries(async_scope_test PUBLIC gmock)
target_link_libraries(async_scope_v0_test PUBLIC gmock)
