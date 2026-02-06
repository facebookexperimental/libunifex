# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the license found in the
# LICENSE.txt file in the root directory of this source tree.

file(GLOB test-sources "*_test.cpp")
foreach(file-path ${test-sources})
    string( REPLACE ".cpp" "" file-path-without-ext ${file-path} )
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_executable( ${file-name} ${file-path})
    target_link_libraries(${file-name} PUBLIC unifex GTest::gtest_main)
    target_compile_definitions(${file-name} PUBLIC -DUNIFEX_UNIT_TEST=1)
    target_sources(${file-name} PRIVATE "./get_return_address_mock.cpp")
    add_test(NAME "test-${file-name}" COMMAND ${file-name})
endforeach()

target_link_libraries(any_sender_of_test PUBLIC GTest::gmock_main)
target_link_libraries(async_manual_reset_event_test PUBLIC GTest::gmock_main)
target_link_libraries(async_scope_test PUBLIC GTest::gmock_main)
target_link_libraries(async_scope_v0_test PUBLIC GTest::gmock_main)
