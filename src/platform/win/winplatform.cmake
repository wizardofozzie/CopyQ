# File icon on Windows
set(copyq_RC copyq.rc)

file(GLOB copyq_SOURCES ${copyq_SOURCES}
    platform/win/winplatform.cpp
    platform/win/winplatformclipboard.cpp
    platform/win/winplatformwindow.cpp
    platform/dummy/dummyclipboard.cpp
    platform/platformcommon.cpp
    ../qxt/qxtglobalshortcut_win.cpp
    )

set(USE_QXT TRUE)

