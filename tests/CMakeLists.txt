add_executable(dag_tests test_dag.cpp)
target_link_libraries(dag_tests PRIVATE lark)

add_test(NAME dag_tests COMMAND dag_tests)

add_executable(test_column test_column.cpp)
target_link_libraries(test_column PRIVATE lark)

add_test(NAME test_column COMMAND test_column)

add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine PRIVATE lark)

add_test(NAME test_engine COMMAND test_engine)

add_executable(test_rpc test_rpc.cpp)
target_link_libraries(test_rpc PRIVATE lark)

add_test(NAME test_rpc COMMAND test_rpc)

add_executable(test_cache test_cache.cpp)
target_link_libraries(test_cache PRIVATE lark)

add_test(NAME test_cache COMMAND test_cache)

add_executable(test_toolkit test_toolkit.cpp)
target_link_libraries(test_toolkit PRIVATE lark)

add_test(NAME test_toolkit COMMAND test_toolkit)

add_executable(test_metric test_metric.cpp)
target_link_libraries(test_metric PRIVATE lark)

add_test(NAME test_metric COMMAND test_metric)

add_executable(test_future test_future.cpp)
target_link_libraries(test_future PRIVATE lark)

add_test(NAME test_future COMMAND test_future)

add_executable(test_pools test_pools.cpp)
target_link_libraries(test_pools PRIVATE lark)

add_test(NAME test_pools COMMAND test_pools)

add_executable(test_op test_op.cpp)
target_link_libraries(test_op PRIVATE lark)

add_test(NAME test_op COMMAND test_op)
