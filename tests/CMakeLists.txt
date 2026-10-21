add_executable(test_password_hasher test_password_hasher.cpp)
target_link_libraries(test_password_hasher PRIVATE bms_core)
target_include_directories(test_password_hasher PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME test_password_hasher COMMAND test_password_hasher)

add_executable(test_login_service test_login_service.cpp)
target_link_libraries(test_login_service PRIVATE bms_core)
target_include_directories(test_login_service PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME test_login_service COMMAND test_login_service)

add_executable(test_credential_store test_credential_store.cpp)
target_link_libraries(test_credential_store PRIVATE bms_core)
target_include_directories(test_credential_store PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME test_credential_store COMMAND test_credential_store)