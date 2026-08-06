add_executable(dag_tests test_dag.cpp)
target_link_libraries(dag_tests PRIVATE lark)

add_test(NAME dag_tests COMMAND dag_tests)

add_executable(test_column test_column.cpp)
target_link_libraries(test_column PRIVATE lark)

add_test(NAME test_column COMMAND test_column)

add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine PRIVATE lark)

add_test(NAME test_engine COMMAND test_engine)
