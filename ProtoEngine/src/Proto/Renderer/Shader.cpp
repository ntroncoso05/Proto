//#include "protopch.hpp"
//#include "Shader.hpp"
//
//#include "Renderer.hpp"
//#include "Platform/OpenGL/OpenGLShader.hpp"
//#include "Core/Log.hpp"
//
//namespace Proto {
//
//	Ref<Shader> Shader::Create(const std::string& filepath)
//	{
//		switch (Renderer::GetAPI())
//		{
//		case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
//		//case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(filepath);
//		}
//
//		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
//		return nullptr;
//	}
//
//	Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
//	{
//		switch (Renderer::GetAPI())
//		{
//		case RendererAPI::API::None:    PROTO_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
//		//case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(name, vertexSrc, fragmentSrc);
//		}
//
//		PROTO_CORE_ASSERT(false, "Unknown RendererAPI!");
//		return nullptr;
//	}
//
//	void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
//	{
//		PROTO_CORE_ASSERT(!Exists(name), "Shader already exists!");
//		m_Shaders[name] = shader;
//	}
//
//	void ShaderLibrary::Add(const Ref<Shader>& shader)
//	{
//		auto& name = shader->GetName();
//		Add(name, shader);
//	}
//
//	Proto::Ref<Proto::Shader> ShaderLibrary::Load(const std::string& filepath)
//	{
//		auto shader = Shader::Create(filepath);
//		Add(shader);
//		return shader;
//	}
//
//	Proto::Ref<Proto::Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
//	{
//		auto shader = Shader::Create(filepath);
//		Add(name, shader);
//		return shader;
//	}
//
//	Proto::Ref<Proto::Shader> ShaderLibrary::Get(const std::string& name)
//	{
//		PROTO_CORE_ASSERT(Exists(name), "Shader not found!");
//		return m_Shaders[name];
//	}
//
//	bool ShaderLibrary::Exists(const std::string& name) const
//	{
//		return m_Shaders.find(name) != m_Shaders.end();
//	}
//
//}