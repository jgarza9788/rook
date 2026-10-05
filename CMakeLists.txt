cmake_minimum_required(VERSION 3.28)
project(rook VERSION 0.1.23 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 DBus Svg)
# The quick view plays audio/video and pages through PDFs when these are
# installed (qt6-multimedia, qt6-webengine on Arch); without them those types
# fall back to an info card. Off by -DROOK_WITH_MEDIA/PDF=OFF.
option(ROOK_WITH_MEDIA "Audio/video in the quick view (Qt Multimedia)" ON)
option(ROOK_WITH_PDF "PDF pages in the quick view (Qt Pdf)" ON)
if(ROOK_WITH_MEDIA)
    find_package(Qt6 6.5 QUIET COMPONENTS Multimedia)
endif()
if(ROOK_WITH_PDF)
    find_package(Qt6 6.5 QUIET COMPONENTS Pdf PdfQuick)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
# Full-text search: SPARQL client for the localsearch index.
pkg_check_modules(TINYSPARQL REQUIRED IMPORTED_TARGET tinysparql-3.0)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

set(ROOK_SOURCES
    src/Application.cpp
    src/Application.h
    src/Applications.cpp
    src/Applications.h
    src/ArchiveEngine.cpp
    src/ArchiveEngine.h
    src/BatchRenamer.cpp
    src/Checksum.cpp
    src/Checksum.h
    src/BatchRenamer.h
    src/Clipboard.cpp
    src/Clipboard.h
    src/DBusAdaptors.cpp
    src/DBusAdaptors.h
    src/DragSource.cpp
    src/DragSource.h
    src/DirectoryModel.cpp
    src/DirectoryModel.h
    src/DirectoryTreeModel.cpp
    src/DirectoryTreeModel.h
    src/FileEntry.cpp
    src/FileEntry.h
    src/FileOperations.cpp
    src/FileOperations.h
    src/FileOperationTypes.h
    src/FileOperationWorker.cpp
    src/FileOperationWorker.h
    src/FileProperties.cpp
    src/FileProperties.h
    src/FileSortFilterModel.cpp
    src/FileSortFilterModel.h
    src/IconImageProvider.cpp
    src/IconImageProvider.h
    src/Location.cpp
    src/Location.h
    src/Mounter.cpp
    src/Mounter.h
    src/NavigationHistory.cpp
    src/NavigationHistory.h
    src/DefaultFileManager.cpp
    src/DefaultFileManager.h
    src/Platform.cpp
    src/Platform.h
    src/PlacesModel.cpp
    src/QuickViewInfo.cpp
    src/QuickViewInfo.h
    src/PlacesModel.h
    src/SearchModel.cpp
    src/StarredStore.cpp
    src/StarredStore.h
    src/StarredModel.cpp
    src/StarredModel.h
    src/ServerStore.cpp
    src/ServerStore.h
    src/NetworkModel.cpp
    src/NetworkModel.h
    src/Previewer.cpp
    src/Previewer.h
    src/SearchModel.h
    src/Settings.cpp
    src/Settings.h
    src/ThumbnailProvider.cpp
    src/ThumbnailProvider.h
    src/SystemTheme.cpp
    src/SystemTheme.h
    src/UserActions.cpp
    src/UserActions.h
)

set_source_files_properties(qml/Colors.qml qml/DragState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(ROOK_QML_FILES
    qml/Main.qml
    qml/Tab.qml
    qml/TabPanes.qml
    qml/OpsPie.qml
    qml/PathBar.qml
    qml/FilterBar.qml
    qml/QuickView.qml
    qml/PreviewPane.qml
    qml/ColumnsView.qml
    qml/CommandPalette.qml
    qml/Commands.qml
    qml/GalleryView.qml
    qml/InfoPanel.qml
    qml/ChecksumBox.qml
    qml/Keymap.js
    qml/Sidebar.qml
    qml/FileListView.qml
    qml/FileIconView.qml
    qml/FileDrag.qml
    qml/ToolbarButton.qml
    qml/OmMenu.qml
    qml/OmMenuItem.qml
    qml/OmMenuSeparator.qml
    qml/OmCard.qml
    qml/OmPopup.qml
    qml/OmDialog.qml
    qml/OmButton.qml
    qml/OmButtonBox.qml
    qml/OmComboBox.qml
    qml/OmTabButton.qml
    qml/DialogCloseButton.qml
    qml/PromptDialog.qml
    qml/PreferencesDialog.qml
    qml/VisibleColumnsDialog.qml
    qml/ShortcutsDialog.qml
    qml/AboutDialog.qml
    qml/BatchRenameDialog.qml
    qml/CompressDialog.qml
    qml/FolderPickerDialog.qml
    qml/ConfirmDialog.qml
    qml/ConflictDialog.qml
    qml/CredentialDialog.qml
    qml/PropertiesDialog.qml
    qml/Colors.qml
    qml/DragState.qml
    qml/FileDropArea.qml
)

set(ROOK_OPTIONAL_DEFINITIONS)
if(Qt6Multimedia_FOUND)
    list(APPEND ROOK_QML_FILES qml/QuickViewMedia.qml)
    list(APPEND ROOK_OPTIONAL_DEFINITIONS ROOK_HAVE_MULTIMEDIA)
endif()
if(Qt6PdfQuick_FOUND)
    list(APPEND ROOK_QML_FILES qml/QuickViewPdf.qml)
    list(APPEND ROOK_OPTIONAL_DEFINITIONS ROOK_HAVE_PDF)
endif()
message(STATUS "rook quick view: media=${Qt6Multimedia_FOUND} pdf=${Qt6PdfQuick_FOUND}")

qt_add_executable(rook src/main.cpp ${ROOK_SOURCES})
qt_add_qml_module(rook URI Rook VERSION 1.0 QML_FILES ${ROOK_QML_FILES}
    RESOURCES packaging/rook.svg)

# GIO's gdbusintrospection.h declares a struct member named `signals`, which
# Qt's unprefixed keyword macros turn into a syntax error. Any Qt+GIO project
# must drop the unprefixed keywords and use Q_SIGNALS/Q_SLOTS/Q_EMIT.
target_compile_definitions(rook PRIVATE QT_NO_KEYWORDS ${ROOK_OPTIONAL_DEFINITIONS})

# Qt generates a type-registration file that includes each QML_ELEMENT header
# by bare name (<DirectoryModel.h>), so src/ has to be on the include path or
# the registrations silently compile to nothing.
target_include_directories(rook PRIVATE src)

set(ROOK_LIBRARIES
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Svg
    PkgConfig::GIO
    PkgConfig::LIBARCHIVE
    PkgConfig::TINYSPARQL
)
if(Qt6Multimedia_FOUND)
    list(APPEND ROOK_LIBRARIES Qt6::Multimedia)
endif()
if(Qt6PdfQuick_FOUND)
    list(APPEND ROOK_LIBRARIES Qt6::Pdf Qt6::PdfQuick)
endif()

target_link_libraries(rook PRIVATE ${ROOK_LIBRARIES})

option(ROOK_BUILD_TESTS "Build the model test suite" ON)
if(ROOK_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
    # Exercise the real QML and generated type registrations in a separate
    # executable, using the same source lists as the application.
    qt_add_executable(tst_qmlviews tests/tst_qmlviews.cpp ${ROOK_SOURCES})
    qt_add_qml_module(tst_qmlviews URI Rook VERSION 1.0
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/qml/Rook
        QML_FILES ${ROOK_QML_FILES}
        RESOURCES packaging/rook.svg)
    target_compile_definitions(tst_qmlviews PRIVATE QT_NO_KEYWORDS ${ROOK_OPTIONAL_DEFINITIONS})
    target_include_directories(tst_qmlviews PRIVATE src)
    target_link_libraries(tst_qmlviews PRIVATE ${ROOK_LIBRARIES})
    add_subdirectory(tests)
endif()

install(TARGETS rook RUNTIME DESTINATION bin)
install(FILES LICENSE DESTINATION share/licenses/rook)
install(FILES packaging/rook.desktop DESTINATION share/applications)
install(FILES packaging/rook.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/rook-symbolic.svg DESTINATION share/icons/hicolor/symbolic/apps)
install(DIRECTORY packaging/actions/ DESTINATION share/rook/actions FILES_MATCHING PATTERN "*.toml")
install(PROGRAMS packaging/bin/rook-dropbox-link DESTINATION bin)
install(PROGRAMS packaging/bin/rook-switch packaging/bin/rook-launch
        packaging/bin/rook-launch-cwd DESTINATION bin)
