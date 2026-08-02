cmake_minimum_required(VERSION 3.20)
project(ask VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncursesw)
pkg_check_modules(LIBEDIT REQUIRED IMPORTED_TARGET libedit)

add_library(ask_core
  src/config.cpp
  src/http.cpp
  src/session.cpp
  src/conference.cpp
  src/conference_tui.cpp
  src/client.cpp
  src/tools.cpp
  src/prompt_template.cpp
  src/token_estimator.cpp
  src/tui.cpp
  src/repl.cpp
  src/cli.cpp
)
target_include_directories(ask_core PUBLIC include)
target_link_libraries(ask_core
  PUBLIC
    PkgConfig::CURL
    PkgConfig::JSONCPP
    PkgConfig::SQLITE
    PkgConfig::NCURSES
    PkgConfig::LIBEDIT
    Threads::Threads
)
target_compile_options(ask_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ask_core PUBLIC ASK_VERSION="${PROJECT_VERSION}")

add_executable(ask src/main.cpp)
target_link_libraries(ask PRIVATE ask_core)

install(TARGETS ask RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md CHANGES.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES docs/AI_CONFERENCE.md docs/AI_CONFERENCE_GUIDE.md docs/QUICKSTART.zh-CN.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)

include(CTest)
if(BUILD_TESTING)
  add_executable(ask_tests tests/tests.cpp)
  target_link_libraries(ask_tests PRIVATE ask_core)
  add_test(NAME ask_tests COMMAND ask_tests)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME ask_integration
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py $<TARGET_FILE:ask>)
    set_tests_properties(ask_integration PROPERTIES TIMEOUT 30)
  endif()
endif()
