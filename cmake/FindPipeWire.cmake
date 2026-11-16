find_path(PIPEWIRE_INCLUDE_DIR NAMES pipewire-0.3/pipewire/pipewire.h)

find_library(PIPEWIRE_LIBRARY NAMES pipewire-0.3)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PipeWire DEFAULT_MSG PIPEWIRE_LIBRARY PIPEWIRE_INCLUDE_DIR)

mark_as_advanced(PIPEWIRE_INCLUDE_DIR PIPEWIRE_LIBRARY)