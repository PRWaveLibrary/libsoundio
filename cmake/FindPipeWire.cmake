# PipeWire 的头文件不在标准 include 目录下，需要把 pipewire-0.3 和 spa-0.2 都加到搜索路径里，
# 否则 <pipewire/pipewire.h> 内部对 <spa/...>、<pipewire/...> 的引用会找不到。

find_path(PIPEWIRE_INCLUDE_DIR NAMES pipewire-0.3/pipewire/pipewire.h)
find_path(SPA_INCLUDE_DIR NAMES spa-0.2/spa/param/audio/raw.h)

find_library(PIPEWIRE_LIBRARY NAMES pipewire-0.3)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PipeWire DEFAULT_MSG PIPEWIRE_LIBRARY PIPEWIRE_INCLUDE_DIR SPA_INCLUDE_DIR)

if (PIPEWIRE_FOUND)
    set(PIPEWIRE_INCLUDE_DIRS ${PIPEWIRE_INCLUDE_DIR}/pipewire-0.3 ${SPA_INCLUDE_DIR}/spa-0.2)
    set(PIPEWIRE_LIBRARIES ${PIPEWIRE_LIBRARY})
endif ()

mark_as_advanced(PIPEWIRE_INCLUDE_DIR SPA_INCLUDE_DIR PIPEWIRE_LIBRARY)
