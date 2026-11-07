//#include "protopch.hpp"
//#include "Texture.hpp"
//
//#include "Renderer.hpp"
//#include "Platform/OpenGL/OpenGLTexture.hpp"
//#include "Core/Log.hpp"
//
//namespace Proto {
//
//	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
//	{
//		switch (Renderer::GetAPI())
//		{
//		case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
//		//case RendererAPI::API::OpenGL:  return CreateRef<OpenGLTexture2D>(width, height);
//		}
//
//		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
//		return nullptr;
//	}
//
//	Ref<Texture2D> Texture2D::Create(const std::string& path)
//	{
//		switch (Renderer::GetAPI())
//		{
//		case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
//		//case RendererAPI::API::OpenGL:  return CreateRef<OpenGLTexture2D>(path);
//		}
//
//		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
//		return nullptr;
//	}
//
//}