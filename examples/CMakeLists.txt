add_executable(simple_pipeline simple_pipeline.cpp)
target_link_libraries(simple_pipeline PRIVATE lark)

add_executable(column_example column_example.cpp)
target_link_libraries(column_example PRIVATE lark)

add_executable(engine_example engine_example.cpp)
target_link_libraries(engine_example PRIVATE lark)
