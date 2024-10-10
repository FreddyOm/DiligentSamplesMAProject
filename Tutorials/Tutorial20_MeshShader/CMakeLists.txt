cmake_minimum_required (VERSION 3.6)

project(Tutorial20_MeshShader CXX)

set(SOURCE
    src/Tutorial20_MeshShader.cpp
    src/ufbx/ufbx.c
    src/octree/octree.cpp
    src/svo_builder/octree_build.cpp
    src/svo_builder/util.cpp
)

set(INCLUDE
    src/Tutorial20_MeshShader.hpp
    src/voxelizer.h
    src/ufbx/ufbx.h
    src/DrawTask.h
    src/octree/octree.h
    src/octree/aabb.h
    src/octree/octree_converter.h
    src/octree/loader/octree_io.h
    src/octree/loader/octree_io_node.h
    src/octree/loader/octree_io_voxel_data.h
    src/svo_builder/globals.h
    src/svo_builder/svo_builder_util.h
    src/svo_builder/timer.h
    src/svo_builder/octree_build.h
    src/svo_builder/Octree.h
    src/svo_builder/DataPoint.h
    src/svo_builder/util.h
    src/libmorton/include/morton.h
    src/libmorton/include/morton_AVX512BITALG.h
    src/libmorton/include/morton_BMI.h
    src/libmorton/include/morton_common.h
    src/libmorton/include/morton2D.h
    src/libmorton/include/morton2D_LUTs.h
    src/libmorton/include/morton3D.h
    src/libmorton/include/morton3D_LUTs.h
)

set(SHADERS
    assets/cube_ash.hlsl
    assets/cube_bestOc_ash.hlsl
    assets/cube_msh.hlsl
    assets/cube_psh.hlsl
    assets/structures.fxh
    assets/depth_reprojection_comp.hlsl
)

set(ASSETS
    assets/DGLogo.png
    assets/models/suzanne.fbx
    assets/models/sponza.fbx
)

add_sample_app("Tutorial20_MeshShader" "DiligentSamples/Tutorials" "${SOURCE}" "${INCLUDE}" "${SHADERS}" "${ASSETS}")
