cmake_minimum_required(VERSION 3.30)
project(mdcomp_installed_smoke LANGUAGES CXX)
find_package(mdcomp 0.2 CONFIG REQUIRED PATHS "${PACKAGE_CONFIG_DIR}" NO_DEFAULT_PATH)
add_executable(installed-codecs main.cc)
target_link_libraries(installed-codecs PRIVATE
    mdcomp::comperStatic mdcomp::comperxStatic mdcomp::enigmaStatic
    mdcomp::kosinskiStatic mdcomp::kosplusStatic mdcomp::lzkn1Static
    mdcomp::nemesisStatic mdcomp::rocketStatic mdcomp::saxmanStatic mdcomp::snkrleStatic)
enable_testing()
add_test(NAME installed-codecs COMMAND installed-codecs)
