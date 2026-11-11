# Adds a Qt Test executable and registers it with CTest.
# Extra arguments are additional libraries to link.
function(launcher_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE launcher_core Qt6::Test ${ARGN})
    launcher_executable_defaults(${name})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# A program that stands in for a project when processes are started and watched.
# It has no window and needs no Qt; the tests copy it into a project's folder.
add_executable(SleepyApp WIN32 helpers/SleepyApp.cpp)
if(MINGW)
    target_link_options(SleepyApp PRIVATE -static)
endif()
target_link_libraries(SleepyApp PRIVATE shell32)
set_target_properties(SleepyApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

launcher_add_test(PortableTests)
launcher_add_test(SettingsTests)
launcher_add_test(LoggerTests)
launcher_add_test(RegistryTests)
launcher_add_test(GitHubClientTests)
launcher_add_test(RegistryManagerTests)
launcher_add_test(SemVerTests)
launcher_add_test(ReleaseTests)
launcher_add_test(DownloadTests)
# The test archives are built with the zlib inside Qt.
launcher_add_test(SecurityTests Qt6::ZlibPrivate)
launcher_add_test(InstallerTests Qt6::ZlibPrivate)
launcher_add_test(InstallServiceTests Qt6::ZlibPrivate)
launcher_add_test(StorageTests)
launcher_add_test(ProcessTests)
add_dependencies(ProcessTests SleepyApp)
launcher_add_test(UpdateTests Qt6::ZlibPrivate)
add_dependencies(UpdateTests SleepyApp)
launcher_add_test(UpdateTransactionTests)
launcher_add_test(LauncherUpdateTests Qt6::ZlibPrivate)
add_dependencies(LauncherUpdateTests launcher SleepyApp)
launcher_add_test(UpdaterTests)
add_dependencies(UpdaterTests launcher SleepyApp)

# The UI test runs without a visible window. Screenshots of every page are
# written to the build directory for visual review.
#
# The offscreen platform has no access to installed fonts and would draw every
# character as an empty box, so it is pointed at the Windows font folder.
launcher_add_test(UiSmokeTests launcher_ui)
add_dependencies(UiSmokeTests SleepyApp)
qt_import_plugins(UiSmokeTests INCLUDE Qt6::QOffscreenIntegrationPlugin)
file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" LAUNCHER_SYSTEM_FONT_DIR)
set_tests_properties(UiSmokeTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=${LAUNCHER_SYSTEM_FONT_DIR};LAUNCHER_TEST_SCREENSHOT_DIR=${CMAKE_BINARY_DIR}/screenshots")
