#include "protopch.hpp"
#include "VertexArray.hpp"

#include "Renderer.hpp"
#include "Platform/OpenGL/OpenGLVertexArray.hpp"
#include "Core/Core.hpp"

namespace Proto {

	Ref<VertexArray> VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
			//case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexArray>();
		}

		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}