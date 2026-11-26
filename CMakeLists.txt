# Weapon Roulette: a native mod for OpenRAC's port of Ratchet & Clank: Going Commando, laid out as
# the SDK's template (sdk/template): one mod at the top of its repository.
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/weapon_roulette -o dist
#
# Push a tag v<version> and the workflow (.github/workflows/build.yml) publishes the release the
# launcher installs from.
cmake_minimum_required(VERSION 3.20)
project(weapon_roulette LANGUAGES C CXX)

# The SDK: the release sdk.lock pins (tools/fetch_sdk.py puts it in .sdk/); OPENRAC_SDK=<dir> (an
# unpacked SDK, or sdk/ in a checkout of OpenRAC) or OpenRACMod_DIR=<dir>/cmake for another.
if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.21 REQUIRED)

# The rotation's rules, apart from the game: the mod links them, and so do the tests.
add_library(roulette_logic STATIC
  src/roulette/weapons.cpp
  src/roulette/rotation.cpp
  src/roulette/kills.cpp
  src/roulette/feedback.cpp)
target_include_directories(roulette_logic PUBLIC src)
target_compile_features(roulette_logic PUBLIC cxx_std_17)
set_target_properties(roulette_logic PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

openrac_add_mod(weapon_roulette MANIFEST openrac-mod.json SOURCES src/mod.cpp)
target_link_libraries(weapon_roulette PRIVATE roulette_logic)
target_include_directories(weapon_roulette PRIVATE src)

if(MSVC)
  target_compile_options(roulette_logic PRIVATE /W4)
  target_compile_options(weapon_roulette PRIVATE /W4)
else()
  target_compile_options(roulette_logic PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(weapon_roulette PRIVATE -Wall -Wextra)
endif()

# The tests: the rotation's rules, and the manifest against them. Not for a cross build (Android),
# whose programs cannot run here.
include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  foreach(test weapons_test rotation_test kills_test feedback_test)
    add_executable(${test} tests/${test}.cpp)
    target_link_libraries(${test} PRIVATE roulette_logic)
    add_test(NAME ${test} COMMAND ${test})
  endforeach()

  # The built library, loaded against a port table the test makes (no game runs).
  add_executable(load_test tests/load_test.cpp)
  target_link_libraries(load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  target_compile_features(load_test PRIVATE cxx_std_17)
  add_dependencies(load_test weapon_roulette)
  add_test(NAME load_test COMMAND load_test $<TARGET_FILE:weapon_roulette>)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME manifest_test
      COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/manifest_test.py"
              "${CMAKE_CURRENT_SOURCE_DIR}/openrac-mod.json"
              "${CMAKE_CURRENT_SOURCE_DIR}/src/roulette/rotation.hpp"
              "${OPENRAC_MOD_SDK_DIR}")
  endif()
endif()
