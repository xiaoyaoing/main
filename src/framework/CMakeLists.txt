
file(GLOB_RECURSE PROJECT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*")
file(GLOB_RECURSE HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(target_name framework)

add_library(${target_name} STATIC ${PROJECT_FILES})

# link_libraries(${Vulkan_LIBRARY}/vulkan-1.lib)
find_package(Vulkan REQUIRED)
target_link_libraries(${target_name} PUBLIC
        spdlog
        vma
        glfw
        stb_image
        tinyexr
       

        glm
        imgui
        ktx
        astc
        volk
        ctpl
        nlohmann_json
        tiny_obj_loader

        glslang
        glslang-default-resource-limits
        SPIRV
        spirv-cross-core
        spirv-cross-cpp
        spirv-cross-glsl
        spirv-cross-reflect
        dxc

        Vulkan::Vulkan)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shaders")


# 设置头文件搜索路径
target_include_directories(${target_name} PUBLIC . ${SHADER_DIR}  $<BUILD_INTERFACE:${Y_VK_THIRD_PARTY_DIR}/DirectXShaderCompiler/include>
)

set(FRAMEWORK_INCLUDE_DIR "." CACHE PATH "Path to the framework include directory")

# 获取 framework 文件夹下的所有源文件
file(GLOB_RECURSE FRAMEWORK_FILES *.h)

file(GLOB_RECURSE ALL_SOURCES *.cpp)

# 按照层次结构组织源文件
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${FRAMEWORK_FILES} ${ALL_SOURCES})

# 设置源文件
target_sources(${target_name} PRIVATE ${FRAMEWORK_FILES})