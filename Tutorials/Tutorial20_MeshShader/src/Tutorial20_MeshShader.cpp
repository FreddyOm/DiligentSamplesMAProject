/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include <array>
#include "Tutorial20_MeshShader.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "imgui.h"
#include "ImGuiUtils.hpp"
#include "FastRand.hpp"
#include <set>
#include <unordered_set>
#include <d3d12.h>
#include "../../../../DiligentCore/Graphics/GraphicsEngineD3D12/include/d3dx12_win.h"

#define VOXELIZER_IMPLEMENTATION
#include "voxelizer.h"  // Voxelizer

#include "ufbx/ufbx.h"  // FBX importer

using namespace VCore;

namespace Diligent
{
    namespace
    {
        #include "../assets/structures.fxh"
        
        struct DrawStatistics
        {
            Uint32 visibleCubes;
            Uint32 visibleOctreeNodes;
        };
        
        static_assert(sizeof(OctreeLeafNode) % 16 == 0, "Structure must be 16-byte aligned");
    
    } // namespace

    SampleBase* CreateSample()
    {
        return new Tutorial20_MeshShader();
    }
    
    void Tutorial20_MeshShader::CreateDrawTasks()
    {
        // In this tutorial draw tasks contain:
        //  * cube position in the grid
        //  * cube scale factor
        //  * time that is used for animation and will be updated in the shader.
        // Additionally you can store model transformation matrix, mesh and material IDs, etc.
    
        const int2          GridDim{256, 256};
        FastRandReal<float> Rnd{0, 0.f, 1.f};
    
        std::vector<OctreeLeafNode> DrawTasks;
        DrawTasks.resize(static_cast<size_t>(GridDim.x) * static_cast<size_t>(GridDim.y));
    
        for (int y = 0; y < GridDim.y; ++y)
        {
            for (int x = 0; x < GridDim.x; ++x)
            {
                int   idx = x + y * GridDim.x;
                auto& dst = DrawTasks[idx];
    
                dst.BasePosAndScale.x  = static_cast<float>((x - GridDim.x / 2) * 2);
                dst.BasePosAndScale.y  = static_cast<float>((y - GridDim.y / 2) * 2);
                dst.BasePosAndScale.w  = 1.f; // 0.5 .. 1
                dst.RandomValue        = {Rnd(), 0, 0, 0};
            }
        }
    
        BufferDesc BuffDesc;
        BuffDesc.Name              = "Draw tasks buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(DrawTasks[0]);
        BuffDesc.Size              = sizeof(DrawTasks[0]) * static_cast<Uint32>(DrawTasks.size());
    
        BufferData BufData;
        BufData.pData    = DrawTasks.data();
        BufData.DataSize = BuffDesc.Size;
    
        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pVoxelPosBuffer);
        VERIFY_EXPR(m_pVoxelPosBuffer != nullptr);
    
        m_DrawTaskCount = static_cast<Uint32>(DrawTasks.size());
    }
    
    void Tutorial20_MeshShader::CreateStatisticsBuffer()
    {
        // This buffer is used as an atomic counter in the amplification shader to show
        // how many cubes are rendered with and without frustum culling.
    
        BufferDesc BuffDesc;
        BuffDesc.Name      = "Statistics buffer";
        BuffDesc.Usage     = USAGE_DEFAULT;
        BuffDesc.BindFlags = BIND_UNORDERED_ACCESS;
        BuffDesc.Mode      = BUFFER_MODE_RAW;
        BuffDesc.Size      = sizeof(DrawStatistics);
    
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_pStatisticsBuffer);
        VERIFY_EXPR(m_pStatisticsBuffer != nullptr);
    
        // Staging buffer is needed to read the data from statistics buffer.
    
        BuffDesc.Name           = "Statistics staging buffer";
        BuffDesc.Usage          = USAGE_STAGING;
        BuffDesc.BindFlags      = BIND_NONE;
        BuffDesc.Mode           = BUFFER_MODE_UNDEFINED;
        BuffDesc.CPUAccessFlags = CPU_ACCESS_READ;
        BuffDesc.Size           = sizeof(DrawStatistics) * m_StatisticsHistorySize;
    
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_pStatisticsStaging);
        VERIFY_EXPR(m_pStatisticsStaging != nullptr);
    
        FenceDesc FDesc;
        FDesc.Name = "Statistics available";
        m_pDevice->CreateFence(FDesc, &m_pStatisticsAvailable);
    }
    
    void Tutorial20_MeshShader::CreateConstantsBuffer()
    {
        BufferDesc BuffDesc;
        BuffDesc.Name           = "Constant buffer";
        BuffDesc.Usage          = USAGE_DYNAMIC;
        BuffDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        BuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        BuffDesc.Size           = sizeof(Constants);
    
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_pConstants);
        VERIFY_EXPR(m_pConstants != nullptr);
    }
    
    void Tutorial20_MeshShader::LoadTexture()
    {
        TextureLoadInfo loadInfo;
        loadInfo.IsSRGB = true;
        RefCntAutoPtr<ITexture> pTex;
        CreateTextureFromFile("DGLogo.png", loadInfo, m_pDevice, &pTex);
        VERIFY_EXPR(pTex != nullptr);
    
        m_CubeTextureSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        VERIFY_EXPR(m_CubeTextureSRV != nullptr);
    }
    
     vx_point_cloud_t* p_voxelMesh = nullptr;
     float      voxelSize   = 2.f;
    
    Tutorial20_MeshShader::~Tutorial20_MeshShader()
    {
        delete p_occlusionOctreeRoot;
        // Delete voxelized meshes here!
        vx_point_cloud_free(p_voxelMesh);
    }
    
    void Tutorial20_MeshShader::GetPointCloudFromMesh(std::string meshPath)
    {
        vx_mesh_t* p_triangleMesh = nullptr;
    
        //Load model from file
        ufbx_load_opts opts = {0}; // Optional, pass NULL for defaults
        ufbx_scene*    scene = ufbx_load_file(meshPath.c_str(), &opts, NULL);
        
        assert(scene);

        ufbx_node* node = scene->nodes.data[0];
    
        int nVertices = static_cast<int>(node->children[0]->mesh->num_vertices);
        int nIndices  = static_cast<int>(node->children[0]->mesh->num_indices);
    
        p_triangleMesh = vx_mesh_alloc(nVertices, nIndices);
    
        // Copy value by value (try memcpy later).
        for (size_t i = 0; i < nVertices; ++i)
        {
            p_triangleMesh->vertices[i].x = (float)node->children[0]->mesh->vertices[i].x;
            p_triangleMesh->vertices[i].y = (float)node->children[0]->mesh->vertices[i].y;
            p_triangleMesh->vertices[i].z = (float)node->children[0]->mesh->vertices[i].z;
        }
    
        for (size_t i = 0; i < nIndices; ++i)
        {
            p_triangleMesh->indices[i] = node->children[0]->mesh->vertex_indices[i];
        }
    
        // Run voxelization
        p_voxelMesh = vx_voxelize_pc(p_triangleMesh, voxelSize, voxelSize, voxelSize, 0.1f);
    
        // Free the triangle mesh and the scene
        vx_mesh_free(p_triangleMesh);
        ufbx_free_scene(scene);
        // Do not vx_mesh_free(pVoxelMesh) yet. Do this in the deinitialization routine instead!
    }

    VCore::VectoriMap<VCore::Voxel> Tutorial20_MeshShader::LoadVoxMesh(std::string meshPath)
    {
        VoxelFormat format = IVoxelFormat::CreateAndLoad(meshPath);
        std::vector<VoxelModel> models = format->GetModels();

        return models[0]->QueryVisible(true);
    }
    
    void Tutorial20_MeshShader::CreateDrawTasksFromLoadedMesh()
    {    
        // Draw Tasks
        std::vector<Vec4> unorderedPositionBuffer;
       
        float minMeshDimension = 10000.f;
        float maxMeshDimension = -10000.f;

        PopulateUnorderedVoxelPosBufAndCalcBounds(unorderedPositionBuffer, minMeshDimension, maxMeshDimension);

        VERIFY_EXPR(unorderedPositionBuffer.size() > 0);
        VERIFY_EXPR(minMeshDimension < maxMeshDimension);
        
        /*      -> Random value and more for alignment
            dst.RandomValue.x       = Rnd();
            dst.RandomValue.y       = static_cast<float>(alignedDrawTaskSize);
            dst.RandomValue.z       = static_cast<float>(alignedDrawTaskSize - p_voxelMesh->nvertices);
            dst.RandomValue.w       = 0;
        */

        //Octree
        AABB world = 
        {
            {minMeshDimension, minMeshDimension, minMeshDimension}, 
            {maxMeshDimension, maxMeshDimension, maxMeshDimension}
        };
        const DirectX::XMVECTOR voxelSizeOffset = {voxelSize, voxelSize, voxelSize};

        p_occlusionOctreeRoot = new OctreeNode<VoxelOC::OctreeLeafNode>(world, ASGroupSize);
    
        {
            // Copy draw tasks to global object buffer for AABB calculations
            for (int i = 0; i < p_voxelMesh->nvertices; ++i)
            {
                VoxelOC::OctreeLeafNode task{};

                task.BasePosAndScale.x = unorderedPositionBuffer[i].x;
                task.BasePosAndScale.y = unorderedPositionBuffer[i].y;
                task.BasePosAndScale.z = unorderedPositionBuffer[i].z;
                task.BasePosAndScale.w = unorderedPositionBuffer[i].w;

                ObjectBuffer.push_back(task);
            }

            // Calculate bounds and add them to octree
            for (int i = 0; i < p_voxelMesh->nvertices; ++i)
            {
                // Calculate bounds
                DirectX::XMVECTOR minBoundVec = DirectX::XMVectorSubtract(
                    {unorderedPositionBuffer[i].x,
                     unorderedPositionBuffer[i].y,
                     unorderedPositionBuffer[i].z},
                    voxelSizeOffset);
                DirectX::XMVECTOR maxBoundVec = DirectX::XMVectorAdd(
                    {unorderedPositionBuffer[i].x,
                     unorderedPositionBuffer[i].y,
                     unorderedPositionBuffer[i].z},
                    voxelSizeOffset);

                DirectX::XMFLOAT3 minBound;
                DirectX::XMFLOAT3 maxBound;

                DirectX::XMStoreFloat3(&minBound, minBoundVec);
                DirectX::XMStoreFloat3(&maxBound, maxBoundVec);

                p_occlusionOctreeRoot->InsertObject(i, {minBound, maxBound});
            }
        }        
        
        // Buffer where objects in one node are stored contigously (start index + length)
        std::vector<VoxelOC::VoxelBufData>  orderedVoxelDataBuffer{};
        orderedVoxelDataBuffer.reserve(unorderedPositionBuffer.size());
        
        // Buffer for all octree nodes which include at least one voxel
        std::vector<VoxelOC::OctreeLeafNode> OTLeafNodes{};
        OTLeafNodes.reserve(static_cast<int>(unorderedPositionBuffer.size() / 2.0f));

        // Buffer for full octree nodes which represent best occluders
        std::vector<VoxelOC::DepthPrepassDrawTask> depthPrepassOTNodes;
        depthPrepassOTNodes.reserve(OTLeafNodes.size());
        
        {
            // Buffer to avoid duplicate entries of voxels into the ordered voxel data buffer
            std::vector<char> duplicateBuffer(unorderedPositionBuffer.size(), 0);   // Can be discarded after QueryAllNodes

            // Visist all nodes and fill the given buffers with data
            p_occlusionOctreeRoot->QueryAllNodes(orderedVoxelDataBuffer, duplicateBuffer, OTLeafNodes);
            VERIFY_EXPR(orderedVoxelDataBuffer.size() > 0 && OTLeafNodes.size() > 0);

            // Visit all nodes and search for "full" nodes
            p_occlusionOctreeRoot->QueryBestOccluders(depthPrepassOTNodes);
            VERIFY_EXPR(depthPrepassOTNodes.size() > 0);        // Might be valid to be 0, though
        }
        
        // Temporary buffer for octree insertion - can now be cleared (but was formerly used in QueryAllNodes()!)
        ObjectBuffer.clear();
        unorderedPositionBuffer.clear(); // Discard unordered position buffer here, so we don't accidentially use it!
        
        // Assign some more (debug) data to the draw tasks (= octree leaf nodes)
        FastRandReal<float> Rnd{0, 0.f, 1.f};

        for (auto& task : OTLeafNodes)
        {
            task.RandomValue.x = Rnd();
            task.RandomValue.y = 0;
            task.RandomValue.z = 0;
        }

        for (auto& task : depthPrepassOTNodes)
        {
            task.BestOccluderCount = static_cast<int>(depthPrepassOTNodes.size());
        }

        // Bind buffer resources to GPU
        BindSortedIndexBuffer(orderedVoxelDataBuffer);
        BindOctreeNodeBuffer(OTLeafNodes);
        BindBestOccluderBuffer(depthPrepassOTNodes);
        
        // Set draw task count
        m_DrawTaskCount = static_cast<Uint32>(OTLeafNodes.size());
        VERIFY_EXPR(m_DrawTaskCount % ASGroupSize == 0);

        m_DepthPassDrawTaskCount = static_cast<Uint32>(depthPrepassOTNodes.size());
        VERIFY_EXPR(m_DepthPassDrawTaskCount % ASGroupSize == 0);
    }

    void Tutorial20_MeshShader::PopulateUnorderedVoxelPosBufAndCalcBounds(std::vector<Vec4>& UnsortedPositionBuffer, float& minMeshDimension, float& maxMeshDimension)
    {
        // Add some spatial padding to explicitly include every voxel!
        minMeshDimension -= voxelSize * 2.f;
        maxMeshDimension += voxelSize * 2.f; 

        UnsortedPositionBuffer.resize(p_voxelMesh->nvertices);

        // Populate unordered voxel position buffer
        for (int i = 0; i < p_voxelMesh->nvertices; ++i)
        {
            Vec4& dst = UnsortedPositionBuffer[i];

            dst.x = p_voxelMesh->vertices[i].x;
            dst.y = p_voxelMesh->vertices[i].y;
            dst.z = p_voxelMesh->vertices[i].z;
            dst.w = voxelSize / 2.f; // 0.5 .. 1 -> divide by 2 for size from middle point

            minMeshDimension = (std::min)(dst.x, minMeshDimension);
            minMeshDimension = (std::min)(dst.y, minMeshDimension);
            minMeshDimension = (std::min)(dst.z, minMeshDimension);

            maxMeshDimension = (std::max)(dst.x, maxMeshDimension);
            maxMeshDimension = (std::max)(dst.y, maxMeshDimension);
            maxMeshDimension = (std::max)(dst.z, maxMeshDimension);
        }
    }

    void Tutorial20_MeshShader::BindSortedIndexBuffer(std::vector<VoxelOC::VoxelBufData>& orderedVoxelDataBuffer)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name              = "Ordered voxel data buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(orderedVoxelDataBuffer[0]);
        BuffDesc.Size              = sizeof(orderedVoxelDataBuffer[0]) * static_cast<Uint32>(orderedVoxelDataBuffer.size());

        BufferData BufData;
        BufData.pData    = orderedVoxelDataBuffer.data();
        BufData.DataSize = BuffDesc.Size;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pVoxelPosBuffer);
        VERIFY_EXPR(m_pVoxelPosBuffer != nullptr);
    }

    void Tutorial20_MeshShader::BindOctreeNodeBuffer(std::vector<VoxelOC::OctreeLeafNode>& octreeNodeBuffer)
    {
        // Realign octree node buffer
        octreeNodeBuffer.resize(octreeNodeBuffer.size() + ASGroupSize - (octreeNodeBuffer.size() % ASGroupSize));
        VERIFY_EXPR(octreeNodeBuffer.size() % ASGroupSize == 0);

        BufferDesc BuffDesc;
        BuffDesc.Name              = "Octree node buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(octreeNodeBuffer[0]);
        BuffDesc.Size              = sizeof(octreeNodeBuffer[0]) * static_cast<Uint32>(octreeNodeBuffer.size());

        BufferData BufData;
        BufData.pData    = octreeNodeBuffer.data();
        BufData.DataSize = BuffDesc.Size;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pOctreeNodeBuffer);
        VERIFY_EXPR(m_pOctreeNodeBuffer != nullptr);
    }
    
    void Tutorial20_MeshShader::BindBestOccluderBuffer(std::vector<VoxelOC::DepthPrepassDrawTask>& depthPrepassOTNodes)
    {
        if (depthPrepassOTNodes.size() == 0) return;

        // Realign deoth prepass octree node buffer
        depthPrepassOTNodes.resize(depthPrepassOTNodes.size() + ASGroupSize - (depthPrepassOTNodes.size() % ASGroupSize));
        VERIFY_EXPR(depthPrepassOTNodes.size() % ASGroupSize == 0);

        BufferDesc BuffDesc;
        BuffDesc.Name              = "Best occluder nodes buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(depthPrepassOTNodes[0]);
        BuffDesc.Size              = sizeof(depthPrepassOTNodes[0]) * static_cast<Uint32>(depthPrepassOTNodes.size());

        BufferData BufData;
        BufData.pData    = depthPrepassOTNodes.data();
        BufData.DataSize = BuffDesc.Size;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pBestOccluderBuffer);
        VERIFY_EXPR(m_pBestOccluderBuffer != nullptr);
    }

    void Tutorial20_MeshShader::CreatePipelineState()
    {
        // Pipeline state object encompasses configuration of all GPU stages
    
        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc = PSOCreateInfo.PSODesc;
    
        PSODesc.Name = "Mesh shader";
    
        PSODesc.PipelineType                                                = PIPELINE_TYPE_MESH;
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets                     = 1;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                        = m_pSwapChain->GetDesc().ColorBufferFormat;
        PSOCreateInfo.GraphicsPipeline.DSVFormat                            = m_pSwapChain->GetDesc().DepthBufferFormat;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FillMode              = FILL_MODE_SOLID; 
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = False;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.DepthBias             = 0;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.DepthBiasClamp        = 0.0f;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias  = 0.0f;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable         = True;
    
        // Topology is defined in the mesh shader, this value is not used.
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_UNDEFINED;
    
        // Define variable type that will be used by default.
        PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    
        // For Direct3D12 we must use the new DXIL compiler that supports mesh shaders.
        ShaderCI.ShaderCompiler = SHADER_COMPILER_DXC;
    
        ShaderCI.Desc.UseCombinedTextureSamplers = true;
    
        // Create a shader source stream factory to load shaders from files.
        RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
        m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
        ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
    
        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("GROUP_SIZE", ASGroupSize);
    
        ShaderCI.Macros = Macros;
    
        RefCntAutoPtr<IShader> pAS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_AMPLIFICATION;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader - AS";
            ShaderCI.FilePath        = "cube_ash.hlsl";
    
            m_pDevice->CreateShader(ShaderCI, &pAS);
            VERIFY_EXPR(pAS != nullptr);
        }

        RefCntAutoPtr<IShader> pASBestOccluders;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_AMPLIFICATION;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader best occluders - AS";
            ShaderCI.FilePath        = "cube_bestOC_ash.hlsl";

            m_pDevice->CreateShader(ShaderCI, &pASBestOccluders);
            VERIFY_EXPR(pASBestOccluders != nullptr);
        }
    
        RefCntAutoPtr<IShader> pMS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_MESH;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader - MS";
            ShaderCI.FilePath        = "cube_msh.hlsl";
    
            m_pDevice->CreateShader(ShaderCI, &pMS);
            VERIFY_EXPR(pMS != nullptr);
        }
    
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader - PS";
            ShaderCI.FilePath        = "cube_psh.hlsl";
    
            m_pDevice->CreateShader(ShaderCI, &pPS);
            VERIFY_EXPR(pPS != nullptr);
        }
    
        // clang-format off
        // Define immutable sampler for g_Texture. Immutable samplers should be used whenever possible
        SamplerDesc SamLinearClampDesc
        {
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, 
            TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
        };
        ImmutableSamplerDesc ImtblSamplers[] = 
        {
            {SHADER_TYPE_PIXEL, "g_Texture", SamLinearClampDesc}
        };
        // clang-format on
        PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;
        PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

        PSOCreateInfo.pAS = pAS;
        PSOCreateInfo.pMS = pMS;
        PSOCreateInfo.pPS = pPS;
    
        m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);
        VERIFY_EXPR(m_pPSO != nullptr);
    
        m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
        VERIFY_EXPR(m_pSRB != nullptr);
    
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "Statistics"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "Statistics")->Set(m_pStatisticsBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VoxelPositionBuffer")) 
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VoxelPositionBuffer")->Set(m_pVoxelPosBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "OctreeNodes"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "OctreeNodes")->Set(m_pOctreeNodeBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants")->Set(m_pConstants);
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants"))
            m_pSRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants")->Set(m_pConstants);
    
        if (m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture"))
            m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_CubeTextureSRV);

        {
            // Create depth pass pipeline state
            GraphicsPipelineStateCreateInfo PSOCreateDepthOnlyPLInfo;
            PipelineStateDesc&              PSODepthOnlyPLDesc = PSOCreateDepthOnlyPLInfo.PSODesc;

            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable      = true;
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc        = COMPARISON_FUNC_LESS;

            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.DepthBias              = 0;
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.DepthBiasClamp         = 0.0f;
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias   = 0.0f;

            // Disable color output
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.NumRenderTargets = 0;
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RTVFormats[0]    = TEX_FORMAT_UNKNOWN;
            PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DSVFormat        = m_pSwapChain->GetDesc().DepthBufferFormat;

            // Mesh shading pipeline setup
            PSODepthOnlyPLDesc.Name         = "Depth only pipeline";
            PSODepthOnlyPLDesc.PipelineType = PIPELINE_TYPE_MESH;
            PSODepthOnlyPLDesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

            // No pixel shader needed for basic depth-only pass
            PSOCreateDepthOnlyPLInfo.pAS = pASBestOccluders;
            PSOCreateDepthOnlyPLInfo.pMS = pMS;
            PSOCreateDepthOnlyPLInfo.pPS = nullptr;

            // Create the pipeline state
            m_pDevice->CreateGraphicsPipelineState(PSOCreateDepthOnlyPLInfo, &m_pDepthOnlyPSO);
            VERIFY_EXPR(m_pDepthOnlyPSO != nullptr);

            // Create and populate the SRB
            m_pDepthOnlyPSO->CreateShaderResourceBinding(&m_pDepthOnlySRB, true);
            VERIFY_EXPR(m_pDepthOnlySRB != nullptr);

            // Bind resources

            if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "BestOccluders"))
                m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "BestOccluders")->Set(m_pBestOccluderBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

            if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants"))
                m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants")->Set(m_pConstants);

            if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants"))
                m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants")->Set(m_pConstants);

            // Set State transitions
            m_TransitionBarrier[0]                = {};
            m_TransitionBarrier[0].pResource      = m_pSwapChain->GetDepthBufferDSV()->GetTexture();
            m_TransitionBarrier[0].OldState       = RESOURCE_STATE_DEPTH_WRITE;
            m_TransitionBarrier[0].NewState       = RESOURCE_STATE_COPY_SOURCE;
            m_TransitionBarrier[0].TransitionType = STATE_TRANSITION_TYPE_IMMEDIATE;
            m_TransitionBarrier[0].Flags          = STATE_TRANSITION_FLAG_UPDATE_STATE;

            m_TransitionBarrier[1]                = {};
            m_TransitionBarrier[1].pResource      = m_pDepthBufferCpy;
            m_TransitionBarrier[1].OldState       = RESOURCE_STATE_UNKNOWN;
            m_TransitionBarrier[1].NewState       = RESOURCE_STATE_COPY_DEST;
            m_TransitionBarrier[1].TransitionType = STATE_TRANSITION_TYPE_IMMEDIATE;
            m_TransitionBarrier[1].Flags          = STATE_TRANSITION_FLAG_UPDATE_STATE;


            m_ResetTransitionBarrier[0]                = {};
            m_ResetTransitionBarrier[0].pResource      = m_pSwapChain->GetDepthBufferDSV()->GetTexture();
            m_ResetTransitionBarrier[0].OldState       = RESOURCE_STATE_UNKNOWN;
            m_ResetTransitionBarrier[0].NewState       = RESOURCE_STATE_DEPTH_WRITE;
            m_ResetTransitionBarrier[0].TransitionType = STATE_TRANSITION_TYPE_IMMEDIATE;
            m_ResetTransitionBarrier[0].Flags          = STATE_TRANSITION_FLAG_UPDATE_STATE;

            m_ResetTransitionBarrier[1]                = {};
            m_ResetTransitionBarrier[1].pResource      = m_pDepthBufferCpy;
            m_ResetTransitionBarrier[1].OldState       = RESOURCE_STATE_UNKNOWN;
            m_ResetTransitionBarrier[1].NewState       = RESOURCE_STATE_COPY_SOURCE;
            m_ResetTransitionBarrier[1].TransitionType = STATE_TRANSITION_TYPE_IMMEDIATE;
            m_ResetTransitionBarrier[1].Flags          = STATE_TRANSITION_FLAG_UPDATE_STATE;
        }
    }

    void Tutorial20_MeshShader::CreateDepthBuffers()
    {
        /*
            |-------------------------|
            |   Main Depth Buffer     |-------------> First Prepass: Render best occluders into main depth buffer
            |-------------------------|

            |-------------------------|
            |  Main Depth Buffer Cpy  |-------------> First Prepass: Copy main depth buffer and create HiZ
            |-------------------------|
            
            |-------------------------|
            | Prev Frame Depth Buffer |-------------> EOF: Copy last frames depth buffer! 
            |-------------------------|

        */

        if (m_pDepthBufferCpy.RawPtr() != nullptr)
            m_pDepthBufferCpy.Release();
        
        if (m_pDepthBufferCpySRV.RawPtr() != nullptr)
            m_pDepthBufferCpySRV.Release();
        
        if (m_pDepthBufferCpyUAV.RawPtr() != nullptr)
            m_pDepthBufferCpyUAV.Release();

        ITextureView* pDepthBufferDSV = m_pSwapChain->GetDepthBufferDSV();
        ITexture*     pDepthTexture   = pDepthBufferDSV->GetTexture();
        const auto&   DepthTexDesc    = pDepthTexture->GetDesc();

        // Create a separate texture for occlusion culling computations
        TextureDesc OcclusionTexDesc = DepthTexDesc; // Copy the description of the depth texture
        OcclusionTexDesc.Name        = "Occlusion Computation Texture";
        OcclusionTexDesc.Usage       = USAGE_DEFAULT;
        OcclusionTexDesc.BindFlags   = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        
        // If the original depth buffer is using a depth-specific format, we need to use a compatible color format
        if (OcclusionTexDesc.Format == TEX_FORMAT_D32_FLOAT)
            OcclusionTexDesc.Format = TEX_FORMAT_R32_FLOAT;
        else if (OcclusionTexDesc.Format == TEX_FORMAT_D24_UNORM_S8_UINT)
            OcclusionTexDesc.Format = TEX_FORMAT_R24_UNORM_X8_TYPELESS;

        m_pDevice->CreateTexture(OcclusionTexDesc, nullptr, &m_pDepthBufferCpy);
        VERIFY_EXPR(m_pDepthBufferCpy != nullptr);

        // Create SRV for the occlusion texture
        TextureViewDesc             OcclusionSRVDesc;
        OcclusionSRVDesc.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
        m_pDepthBufferCpy->CreateView(OcclusionSRVDesc, &m_pDepthBufferCpySRV);
        VERIFY_EXPR(m_pDepthBufferCpySRV != nullptr);

        // Create UAV for the occlusion texture
        TextureViewDesc             OcclusionUAVDesc;
        OcclusionUAVDesc.ViewType = TEXTURE_VIEW_UNORDERED_ACCESS;
        m_pDepthBufferCpy->CreateView(OcclusionUAVDesc, &m_pDepthBufferCpyUAV);
        VERIFY_EXPR(m_pDepthBufferCpyUAV != nullptr);
    }

    void Tutorial20_MeshShader::DepthPrepass() const
    {
        m_pImmediateContext->SetPipelineState(m_pDepthOnlyPSO);
        m_pImmediateContext->CommitShaderResources(m_pDepthOnlySRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Set depth-stencil view
        auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
        m_pImmediateContext->SetRenderTargets(0, nullptr, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Draw best occluders. Task count doesn't change, since the buffers are all the same, we just discard 
        // more invocations.
        VERIFY_EXPR(m_DepthPassDrawTaskCount % ASGroupSize == 0);

        DrawMeshAttribs drawAttrs{m_DepthPassDrawTaskCount, DRAW_FLAG_VERIFY_ALL};
        m_pImmediateContext->DrawMesh(drawAttrs);
        
        // Copy and store best occluder depth buffer 
        CopyTextureAttribs storeDepthBufAttribs{};
        storeDepthBufAttribs.pSrcTexture              = m_pSwapChain->GetDepthBufferDSV()->GetTexture();
        storeDepthBufAttribs.pDstTexture              = m_pDepthBufferCpy;
        storeDepthBufAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        storeDepthBufAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_pImmediateContext->CopyTexture(storeDepthBufAttribs);
    }
    
    void Tutorial20_MeshShader::UpdateUI()
    {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Checkbox("Frustum culling", &m_FrustumCulling);
            ImGui::Checkbox("Occlusion culling", &m_OcclusionCulling);
            ImGui::Checkbox("MS Debug Visualization", &m_MSDebugViz);
            ImGui::Checkbox("Octree Debug Visualization", &m_OTDebugViz);
            ImGui::Checkbox("Syncronize Camera Position", &m_SyncCamPosition);

            if (ImGui::Button("Reset Camera"))
            {
                fpc.SetPos({0, 5, 0});
            }
            ImGui::Text("Visible cubes: %d", m_VisibleCubes);
            ImGui::Text("Visible octree nodes: %d", m_VisibleOTNodes);

        }
        ImGui::End();
    }

    void Tutorial20_MeshShader::WindowResize(Uint32 Width, Uint32 Height)
    {
        CreateDepthBuffers();
    }
    
    void Tutorial20_MeshShader::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
    {
        SampleBase::ModifyEngineInitInfo(Attribs);
    
        Attribs.EngineCI.Features.MeshShaders = DEVICE_FEATURE_STATE_ENABLED;
    }
    
    void Tutorial20_MeshShader::Initialize(const SampleInitInfo& InitInfo)
    {
        SampleBase::Initialize(InitInfo);
    
        fpc.SetMoveSpeed(25.f);
    
        LoadTexture();
        GetPointCloudFromMesh("models/suzanne.fbx");
        //CreateDrawTasks();
        CreateDrawTasksFromLoadedMesh();
        CreateStatisticsBuffer();
        CreateConstantsBuffer();
        CreateDepthBuffers();
        CreatePipelineState();
    }
    
    // Render a frame
    void Tutorial20_MeshShader::Render()
    {
        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
        // Clear the back buffer and depth buffer
        const float ClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
        
        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Reset statistics
        DrawStatistics stats;
        std::memset(&stats, 0, sizeof(stats));
        m_pImmediateContext->UpdateBuffer(m_pStatisticsBuffer, 0, sizeof(stats), &stats, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    
        {
            // Map the buffer and write current view, view-projection matrix and other constants.
            MapHelper<Constants> CBConstants(m_pImmediateContext, m_pConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            CBConstants->ViewMat                = m_ViewMatrix.Transpose();
            CBConstants->ViewProjMat            = m_ViewProjMatrix.Transpose();
            CBConstants->CoTanHalfFov           = m_LodScale * m_CoTanHalfFov;
            CBConstants->FrustumCulling         = m_FrustumCulling ? 1 : 0;
            CBConstants->OcclusionCulling       = m_OcclusionCulling ? 1 : 0;
            CBConstants->MSDebugViz             = m_MSDebugViz ? 1.0f : 0.0f;
            CBConstants->OctreeDebugViz         = m_OTDebugViz ? 1.0f : 0.0f;
    
            // Calculate frustum planes from view-projection matrix.
            if (m_SyncCamPosition)
                ExtractViewFrustumPlanesFromMatrix(m_ViewProjMatrix, Frustum, false);
    
            // Each frustum plane must be normalized.
            for (uint i = 0; i < _countof(CBConstants->Frustum); ++i)
            {
                Plane3D plane  = Frustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(i));
                float   invlen = 1.0f / length(plane.Normal);
                plane.Normal *= invlen;
                plane.Distance *= invlen;
    
                CBConstants->Frustum[i] = plane;
            }

            // Draw best occluders to depth buffer
            DepthPrepass();

            // Clear Depth Stencil to avoid flickering (Remove later, should be able to just )
            m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }        

        // Reset pipeline state to normally draw to back buffer
        m_pImmediateContext->SetPipelineState(m_pPSO);
        m_pImmediateContext->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Amplification shader executes 32 threads per group and the task count must be aligned to 32
        // to prevent loss of tasks or access outside of the data array.
        VERIFY_EXPR(m_DrawTaskCount % ASGroupSize == 0);
    
        DrawMeshAttribs drawAttrs{m_DrawTaskCount, DRAW_FLAG_VERIFY_ALL};
        m_pImmediateContext->DrawMesh(drawAttrs);
    
        // Copy statistics to staging buffer
        {
            m_VisibleCubes = 0;
    
            m_pImmediateContext->CopyBuffer(m_pStatisticsBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                            m_pStatisticsStaging, static_cast<Uint32>(m_FrameId % m_StatisticsHistorySize) * sizeof(DrawStatistics), sizeof(DrawStatistics),
                                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    
            // We should use synchronizations to safely access the mapped memory.
            m_pImmediateContext->EnqueueSignal(m_pStatisticsAvailable, m_FrameId);
    
            // Read statistics from previous frame.
            Uint64 AvailableFrameId = m_pStatisticsAvailable->GetCompletedValue();
    
            // Synchronize
            if (m_FrameId - AvailableFrameId > m_StatisticsHistorySize)
            {
                // In theory we should never get here as we wait for more than enough
                // frames.
                AvailableFrameId = m_FrameId - m_StatisticsHistorySize;
                m_pStatisticsAvailable->Wait(AvailableFrameId);
            }
    
            // Read the staging data
            if (AvailableFrameId > 0)
            {
                MapHelper<DrawStatistics> StagingData(m_pImmediateContext, m_pStatisticsStaging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
                if (StagingData)
                {
                    m_VisibleCubes = StagingData[AvailableFrameId % m_StatisticsHistorySize].visibleCubes;
                    m_VisibleOTNodes = StagingData[AvailableFrameId % m_StatisticsHistorySize].visibleOctreeNodes;
                }
            }
    
            m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            m_pImmediateContext->Flush();

            ++m_FrameId;
        }
    }
    
    void Tutorial20_MeshShader::Update(double CurrTime, double ElapsedTime)
    {
        SampleBase::Update(CurrTime, ElapsedTime);
        UpdateUI();
    
        fpc.Update(GetInputController(), (float)ElapsedTime);
    
        // Set camera position
        float4x4 View = fpc.GetViewMatrix();
    
        // Get pretransform matrix that rotates the scene according the surface orientation
        auto SrfPreTransform = GetSurfacePretransformMatrix(float3{0, 0, 1});
    
        // Get projection matrix adjusted to the current screen orientation
        auto Proj = GetAdjustedProjectionMatrix(m_FOV, 0.01f, 1000.f);
    
        // Compute view and view-projection matrices
        m_ViewMatrix = View * SrfPreTransform;
        m_ViewProjMatrix = m_ViewMatrix * Proj;
    }

} // namespace Diligent
