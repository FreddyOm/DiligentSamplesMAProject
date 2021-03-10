/*
 *  Copyright 2019-2021 Diligent Graphics LLC
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

#include <memory>
#include <iomanip>
#include <iostream>

#ifndef ENGINE_DLL
#    define ENGINE_DLL 1
#endif

#ifndef D3D11_SUPPORTED
#    define D3D11_SUPPORTED 0
#endif

#ifndef D3D12_SUPPORTED
#    define D3D12_SUPPORTED 0
#endif

#ifndef GL_SUPPORTED
#    define GL_SUPPORTED 0
#endif

#ifndef VULKAN_SUPPORTED
#    define VULKAN_SUPPORTED 0
#endif

#if PLATFORM_WIN32
#    define GLFW_EXPOSE_NATIVE_WIN32 1
#endif
#if PLATFORM_LINUX
#    define GLFW_EXPOSE_NATIVE_X11 1
#endif
#if PLATFORM_MACOS
#    define GLFW_EXPOSE_NATIVE_COCOA 1
#endif
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"

#include "Common/interface/RefCntAutoPtr.hpp"

#include "Graphics/GraphicsEngineD3D11/interface/EngineFactoryD3D11.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "Graphics/GraphicsEngineOpenGL/interface/EngineFactoryOpenGL.h"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"

#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

using namespace Diligent;

// For this tutorial, we will use simple vertex shader
// that creates a procedural triangle

// Diligent Engine can use HLSL source on all supported platforms.
// It will convert HLSL to GLSL in OpenGL mode, while Vulkan backend will compile it directly to SPIRV.

static const char* VSSource = R"(
struct PSInput 
{ 
    float4 Pos   : SV_POSITION; 
    float3 Color : COLOR; 
};

void main(in  uint    VertId : SV_VertexID,
          out PSInput PSIn) 
{
    float4 Pos[3];
    Pos[0] = float4(-0.5, -0.5, 0.0, 1.0);
    Pos[1] = float4( 0.0, +0.5, 0.0, 1.0);
    Pos[2] = float4(+0.5, -0.5, 0.0, 1.0);

    float3 Col[3];
    Col[0] = float3(1.0, 0.0, 0.0); // red
    Col[1] = float3(0.0, 1.0, 0.0); // green
    Col[2] = float3(0.0, 0.0, 1.0); // blue

    PSIn.Pos   = Pos[VertId];
    PSIn.Color = Col[VertId];
}
)";

// Pixel shader simply outputs interpolated vertex color
static const char* PSSource = R"(
struct PSInput 
{ 
    float4 Pos   : SV_POSITION; 
    float3 Color : COLOR; 
};

struct PSOutput
{ 
    float4 Color : SV_TARGET; 
};

void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    PSOut.Color = float4(PSIn.Color.rgb, 1.0);
}
)";


class Tutorial00App
{
public:
    Tutorial00App()
    {
    }

    ~Tutorial00App()
    {
        m_pImmediateContext->Flush();
        m_pSwapChain        = nullptr;
        m_pImmediateContext = nullptr;
        m_pDevice           = nullptr;

        if (m_Window)
            glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    bool InitializeGLFW(const char* Title, int Width, int Height)
    {
        if (glfwInit() != GLFW_TRUE)
            return false;

        // Use GLFW only to create window, skip additional work for graphics API initialization.
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_Window = glfwCreateWindow(Width, Height, Title, nullptr, nullptr);
        if (m_Window == nullptr)
            return false;

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, &GLFW_ResizeCallback);
        glfwSetKeyCallback(m_Window, &GLFW_KeyCallback);
        glfwSetMouseButtonCallback(m_Window, &GLFW_MouseButtonCallback);
        glfwSetCursorPosCallback(m_Window, &GLFW_CursorPosCallback);
        glfwSetScrollCallback(m_Window, &GLFW_MouseWheelCallback);

        return true;
    }

    bool InitializeDiligentEngine()
    {
#if PLATFORM_WIN32
        Win32NativeWindow Window{glfwGetWin32Window(m_Window)};
#endif
#if PLATFORM_LINUX
        LinuxNativeWindow Window{glfwGetX11Window(m_Window), glfwGetX11Display()};
#endif
#if PLATFORM_MACOS
        MacOSNativeWindow Window{glfwGetCocoaWindow(m_Window)};
#endif

        SwapChainDesc SCDesc;
        switch (m_DeviceType)
        {
#if D3D11_SUPPORTED
            case RENDER_DEVICE_TYPE_D3D11:
            {
                EngineD3D11CreateInfo EngineCI;
#    ifdef DILIGENT_DEBUG
                EngineCI.DebugFlags |=
                    D3D11_DEBUG_FLAG_CREATE_DEBUG_DEVICE |
                    D3D11_DEBUG_FLAG_VERIFY_COMMITTED_SHADER_RESOURCES;
#    endif
#    if ENGINE_DLL
                // Load the dll and import GetEngineFactoryD3D11() function
                auto GetEngineFactoryD3D11 = LoadGraphicsEngineD3D11();
#    endif
                auto* pFactoryD3D11 = GetEngineFactoryD3D11();
                pFactoryD3D11->CreateDeviceAndContextsD3D11(EngineCI, &m_pDevice, &m_pImmediateContext);
                pFactoryD3D11->CreateSwapChainD3D11(m_pDevice, m_pImmediateContext, SCDesc, FullScreenModeDesc{}, Window, &m_pSwapChain);
            }
            break;
#endif // D3D11_SUPPORTED


#if D3D12_SUPPORTED
            case RENDER_DEVICE_TYPE_D3D12:
            {
#    if ENGINE_DLL
                // Load the dll and import GetEngineFactoryD3D12() function
                auto GetEngineFactoryD3D12 = LoadGraphicsEngineD3D12();
#    endif
                EngineD3D12CreateInfo EngineCI;
#    ifdef DILIGENT_DEBUG
                // There is a bug in D3D12 debug layer that causes memory leaks in this tutorial
                //EngineCI.EnableDebugLayer = true;
#    endif
                auto* pFactoryD3D12 = GetEngineFactoryD3D12();
                pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_pDevice, &m_pImmediateContext);
                pFactoryD3D12->CreateSwapChainD3D12(m_pDevice, m_pImmediateContext, SCDesc, FullScreenModeDesc{}, Window, &m_pSwapChain);
            }
            break;
#endif // D3D12_SUPPORTED


#if GL_SUPPORTED
            case RENDER_DEVICE_TYPE_GL:
            {
#    if EXPLICITLY_LOAD_ENGINE_GL_DLL
                // Load the dll and import GetEngineFactoryOpenGL() function
                auto GetEngineFactoryOpenGL = LoadGraphicsEngineOpenGL();
#    endif
                auto* pFactoryOpenGL = GetEngineFactoryOpenGL();

                EngineGLCreateInfo EngineCI;
                EngineCI.Window = Window;
#    ifdef DILIGENT_DEBUG
                EngineCI.CreateDebugContext = true;
#    endif
                pFactoryOpenGL->CreateDeviceAndSwapChainGL(EngineCI, &m_pDevice, &m_pImmediateContext, SCDesc, &m_pSwapChain);
            }
            break;
#endif // GL_SUPPORTED


#if VULKAN_SUPPORTED
            case RENDER_DEVICE_TYPE_VULKAN:
            {
#    if EXPLICITLY_LOAD_ENGINE_VK_DLL
                // Load the dll and import GetEngineFactoryVk() function
                auto GetEngineFactoryVk = LoadGraphicsEngineVk();
#    endif
                EngineVkCreateInfo EngineCI;
#    ifdef DILIGENT_DEBUG
                EngineCI.EnableValidation = true;
#    endif
                auto* pFactoryVk = GetEngineFactoryVk();
                pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_pDevice, &m_pImmediateContext);
                pFactoryVk->CreateSwapChainVk(m_pDevice, m_pImmediateContext, SCDesc, Window, &m_pSwapChain);
            }
            break;
#endif // VULKAN_SUPPORTED


            default:
                std::cerr << "Unknown/unsupported device type";
                return false;
                break;
        }

        if (m_pDevice == nullptr || m_pImmediateContext == nullptr || m_pSwapChain == nullptr)
            return false;

        return true;
    }

    bool ProcessCommandLine(const char* CmdLine)
    {
        const auto* Key = "-mode ";
        const auto* pos = strstr(CmdLine, Key);
        if (pos != nullptr)
        {
            pos += strlen(Key);
            if (_stricmp(pos, "D3D11") == 0)
            {
#if D3D11_SUPPORTED
                m_DeviceType = RENDER_DEVICE_TYPE_D3D11;
#else
                std::cerr << "Direct3D11 is not supported. Please select another device type";
                return false;
#endif
            }
            else if (_stricmp(pos, "D3D12") == 0)
            {
#if D3D12_SUPPORTED
                m_DeviceType = RENDER_DEVICE_TYPE_D3D12;
#else
                std::cerr << "Direct3D12 is not supported. Please select another device type";
                return false;
#endif
            }
            else if (_stricmp(pos, "GL") == 0)
            {
#if GL_SUPPORTED
                m_DeviceType = RENDER_DEVICE_TYPE_GL;
#else
                std::cerr << "OpenGL is not supported. Please select another device type";
                return false;
#endif
            }
            else if (_stricmp(pos, "VK") == 0)
            {
#if VULKAN_SUPPORTED
                m_DeviceType = RENDER_DEVICE_TYPE_VULKAN;
#else
                std::cerr << "Vulkan is not supported. Please select another device type";
                return false;
#endif
            }
            else
            {
                std::cerr << "Unknown device type. Only the following types are supported: D3D11, D3D12, GL, VK";
                return false;
            }
        }
        else
        {
#if D3D12_SUPPORTED
            m_DeviceType = RENDER_DEVICE_TYPE_D3D12;
#elif VULKAN_SUPPORTED
            m_DeviceType = RENDER_DEVICE_TYPE_VULKAN;
#elif D3D11_SUPPORTED
            m_DeviceType = RENDER_DEVICE_TYPE_D3D11;
#elif GL_SUPPORTED
            m_DeviceType = RENDER_DEVICE_TYPE_GL;
#endif
        }
        return true;
    }


    void CreateResources()
    {
        // Pipeline state object encompasses configuration of all GPU stages

        GraphicsPipelineStateCreateInfo PSOCreateInfo;

        // Pipeline state name is used by the engine to report issues.
        // It is always a good idea to give objects descriptive names.
        PSOCreateInfo.PSODesc.Name = "Simple triangle PSO";

        // This is a graphics pipeline
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        // clang-format off
        // This tutorial will render to a single render target
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
        // Set render target format which is the format of the swap chain's color buffer
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
        // Use the depth buffer format from the swap chain
        PSOCreateInfo.GraphicsPipeline.DSVFormat                    = m_pSwapChain->GetDesc().DepthBufferFormat;
        // Primitive topology defines what kind of primitives will be rendered by this pipeline state
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        // No back face culling for this tutorial
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        // Disable depth testing
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
        // clang-format on

        ShaderCreateInfo ShaderCI;
        // Tell the system that the shader source code is in HLSL.
        // For OpenGL, the engine will convert this into GLSL under the hood
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        // OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
        ShaderCI.UseCombinedTextureSamplers = true;
        // Create a vertex shader
        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Triangle vertex shader";
            ShaderCI.Source          = VSSource;
            m_pDevice->CreateShader(ShaderCI, &pVS);
        }

        // Create a pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Triangle pixel shader";
            ShaderCI.Source          = PSSource;
            m_pDevice->CreateShader(ShaderCI, &pPS);
        }

        // Finally, create the pipeline state
        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;
        m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);
    }

    void Render()
    {
        // Set render targets before issuing any draw command.
        // Note that Present() unbinds the back buffer if it is set as render target.
        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Clear the back buffer
        const float ClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
        // Let the engine perform required state transitions
        m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Set the pipeline state in the immediate context
        m_pImmediateContext->SetPipelineState(m_pPSO);

        // Typically we should now call CommitShaderResources(), however shaders in this example don't
        // use any resources.

        DrawAttribs drawAttrs;
        drawAttrs.NumVertices = 3; // Render 3 vertices
        m_pImmediateContext->Draw(drawAttrs);
    }

    bool Update()
    {
        if (glfwWindowShouldClose(m_Window))
            return false;

        glfwPollEvents();
        return true;
    }

    void Present()
    {
        m_pSwapChain->Present();
    }

    RENDER_DEVICE_TYPE GetDeviceType() const { return m_DeviceType; }

private:
    static void GLFW_ResizeCallback(GLFWwindow* wnd, int w, int h)
    {
        auto* Self = static_cast<Tutorial00App*>(glfwGetWindowUserPointer(wnd));
        if (Self->m_pSwapChain != nullptr)
            Self->m_pSwapChain->Resize(static_cast<Uint32>(w), static_cast<Uint32>(h));
    }

    static void GLFW_KeyCallback(GLFWwindow* wnd, int key, int, int action, int)
    {
        auto* Self = static_cast<Tutorial00App*>(glfwGetWindowUserPointer(wnd));
        if (Self->m_Window && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(Self->m_Window, GLFW_TRUE);
        }
    }

    static void GLFW_MouseButtonCallback(GLFWwindow* wnd, int button, int action, int)
    {
    }

    static void GLFW_CursorPosCallback(GLFWwindow* wnd, double xpos, double ypos)
    {
    }

    static void GLFW_MouseWheelCallback(GLFWwindow* wnd, double dx, double dy)
    {
    }

private:
    RefCntAutoPtr<IRenderDevice>  m_pDevice;
    RefCntAutoPtr<IDeviceContext> m_pImmediateContext;
    RefCntAutoPtr<ISwapChain>     m_pSwapChain;
    RefCntAutoPtr<IPipelineState> m_pPSO;
    RENDER_DEVICE_TYPE            m_DeviceType = RENDER_DEVICE_TYPE_D3D11;
    GLFWwindow*                   m_Window     = nullptr;
};

std::unique_ptr<Tutorial00App> g_pTheApp;

int main(int argc, const char** argv)
{
    const char* cmdLine = argc >= 2 ? argv[1] : "";

    g_pTheApp.reset(new Tutorial00App);

    if (!g_pTheApp->ProcessCommandLine(cmdLine))
        return -1;

    String Title("Tutorial00: Hello GLFW");
    switch (g_pTheApp->GetDeviceType())
    {
        case RENDER_DEVICE_TYPE_D3D11: Title.append(" (D3D11)"); break;
        case RENDER_DEVICE_TYPE_D3D12: Title.append(" (D3D12)"); break;
        case RENDER_DEVICE_TYPE_GL: Title.append(" (GL)"); break;
        case RENDER_DEVICE_TYPE_VULKAN: Title.append(" (VK)"); break;
    }

    if (!g_pTheApp->InitializeGLFW(Title.c_str(), 1024, 768))
        return -1;

    if (!g_pTheApp->InitializeDiligentEngine())
        return -1;

    g_pTheApp->CreateResources();

    for (;;)
    {
        if (!g_pTheApp->Update())
            break;

        g_pTheApp->Render();
        g_pTheApp->Present();
    }

    g_pTheApp.reset();
    return 0;
}