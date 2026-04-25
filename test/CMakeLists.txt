cmake_minimum_required(VERSION 3.15)

find_package(GTest REQUIRED)

# T1: IPC 可靠性测试
add_executable(test_ipc test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE robot_common GTest::gtest_main)
add_test(NAME test_ipc COMMAND test_ipc)

# T3: 句子分割测试（最重要！）
add_executable(test_sentence_segmenter test_sentence_segmenter.cpp
    ../src/brain_proc/sentence_segmenter.cpp)
target_include_directories(test_sentence_segmenter PRIVATE ../src)
target_link_libraries(test_sentence_segmenter PRIVATE robot_common GTest::gtest_main)
add_test(NAME test_sentence_segmenter COMMAND test_sentence_segmenter)

# T4: 状态机完整性测试
add_executable(test_state_machine test_state_machine.cpp
    ../src/brain_proc/state_machine.cpp)
target_include_directories(test_state_machine PRIVATE ../src)
target_link_libraries(test_state_machine PRIVATE robot_common GTest::gtest_main)
add_test(NAME test_state_machine COMMAND test_state_machine)

# T9: 触发引擎测试
add_executable(test_trigger_engine test_trigger_engine.cpp
    ../src/brain_proc/trigger_engine.cpp)
target_include_directories(test_trigger_engine PRIVATE ../src)
target_link_libraries(test_trigger_engine PRIVATE robot_common GTest::gtest_main)
add_test(NAME test_trigger_engine COMMAND test_trigger_engine)
