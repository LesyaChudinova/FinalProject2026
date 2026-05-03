cmake_minimum_required(VERSION 3.15)
project(AcademicYearProject2026 VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PG_PATH "C:/Program Files/PostgreSQL/18")
set(PostgreSQL_INCLUDE_DIRS "${PG_PATH}/include")
set(PostgreSQL_LIBRARIES "${PG_PATH}/lib/libpq.lib")

add_executable(AcademicYearProject2026 src/main.cpp "src/utils/crypto.h")

target_include_directories(AcademicYearProject2026 PRIVATE
    ${CMAKE_SOURCE_DIR}/src 
    ${CMAKE_SOURCE_DIR}/src/database
    ${CMAKE_SOURCE_DIR}/src/models
    ${CMAKE_SOURCE_DIR}/src/repositories
    ${CMAKE_SOURCE_DIR}/src/scheduler
    ${CMAKE_SOURCE_DIR}/src/utils
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(AcademicYearProject2026 PRIVATE
    ${PostgreSQL_LIBRARIES} 
    ws2_32
    "${PG_PATH}/lib/libssl.lib"
    "${PG_PATH}/lib/libcrypto.lib")
    

add_custom_command(TARGET AcademicYearProject2026 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PG_PATH}/bin/libpq.dll" "$<TARGET_FILE_DIR:AcademicYearProject2026>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PG_PATH}/bin/libcrypto-3-x64.dll" "$<TARGET_FILE_DIR:AcademicYearProject2026>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PG_PATH}/bin/libssl-3-x64.dll" "$<TARGET_FILE_DIR:AcademicYearProject2026>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PG_PATH}/bin/libintl-9.dll" "$<TARGET_FILE_DIR:AcademicYearProject2026>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_SOURCE_DIR}/front" "$<TARGET_FILE_DIR:AcademicYearProject2026>/front")

if(WIN32)
    target_compile_definitions(AcademicYearProject2026 PRIVATE 
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
)
endif()