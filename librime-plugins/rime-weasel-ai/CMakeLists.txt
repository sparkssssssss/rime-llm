project(rime-weasel-ai)
cmake_minimum_required(VERSION 3.10)

aux_source_directory(src ai_correction_src)

add_library(rime-weasel-ai-objs OBJECT ${ai_correction_src})
if(BUILD_SHARED_LIBS)
  set_target_properties(rime-weasel-ai-objs
    PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

set(plugin_name rime-weasel-ai PARENT_SCOPE)
set(plugin_objs $<TARGET_OBJECTS:rime-weasel-ai-objs> PARENT_SCOPE)
set(plugin_deps ${rime_library} PARENT_SCOPE)
set(plugin_modules "weasel_ai" PARENT_SCOPE)
