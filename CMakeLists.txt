cmake_minimum_required(VERSION 3.16)

set(ENV{IDF_COMPONENT_MANAGER} "1")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(goouuu_usb_camera)

# usb_device_uvc 1.3.1 exposes usb_descriptors.c as a PUBLIC TinyUSB source.
# That makes PlatformIO/SCons compile the same object with different component
# flags. Keep it in TinyUSB, without propagating it to dependent libraries.
# Apply this at project level so downloaded components remain unmodified.
idf_component_get_property(tinyusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(uvc_dir espressif__usb_device_uvc COMPONENT_DIR)
get_target_property(tinyusb_public_sources ${tinyusb_lib} INTERFACE_SOURCES)
if(tinyusb_public_sources)
    list(REMOVE_ITEM tinyusb_public_sources "${uvc_dir}/tusb/usb_descriptors.c")
    set_property(TARGET ${tinyusb_lib} PROPERTY INTERFACE_SOURCES "${tinyusb_public_sources}")
endif()
