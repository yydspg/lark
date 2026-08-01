add_executable(dag_tests test_dag.cpp)
target_link_libraries(dag_tests PRIVATE lark)

add_test(NAME dag_tests COMMAND dag_tests)
