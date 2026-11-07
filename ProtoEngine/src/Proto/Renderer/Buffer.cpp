#include "protopch.hpp"
#include "Buffer.hpp"

#include "Renderer.hpp"

#include "Platform/OpenGL/OpenGLBuffer.hpp"
#include "Platform/Vulkan/VK_Buffer.hpp"

namespace Proto {

	VertexBuffer* VertexBuffer::Create(float* vertices, uint32_t size)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		//case RendererAPI::API::OpenGL:  return new OpenGLVertexBuffer(vertices, size);
		//case RendererAPI::API::Vulkan:  return new Vertex(vertices, size);
		}

		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t size)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		//case RendererAPI::API::OpenGL:  return new OpenGLIndexBuffer(indices, size);
		}

		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}