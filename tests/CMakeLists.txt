add_executable(dag_tests test_dag.cpp)
target_link_libraries(dag_tests PRIVATE lark)

add_test(NAME dag_tests COMMAND dag_tests)

add_executable(test_column test_column.cpp)
target_link_libraries(test_column PRIVATE lark)

add_test(NAME test_column COMMAND test_column)
