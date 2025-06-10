# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/tiago/esp/v5.4.1/esp-idf/components/bootloader/subproject"
  "/home/tiago/Modelos/lora_received/build/bootloader"
  "/home/tiago/Modelos/lora_received/build/bootloader-prefix"
  "/home/tiago/Modelos/lora_received/build/bootloader-prefix/tmp"
  "/home/tiago/Modelos/lora_received/build/bootloader-prefix/src/bootloader-stamp"
  "/home/tiago/Modelos/lora_received/build/bootloader-prefix/src"
  "/home/tiago/Modelos/lora_received/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/tiago/Modelos/lora_received/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/tiago/Modelos/lora_received/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
