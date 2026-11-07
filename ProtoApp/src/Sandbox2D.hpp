//#pragma once
//
//#include "Proto.hpp"
//
//class Sandbox2D : public Proto::Layer
//{
//public:
//	Sandbox2D();
//	virtual ~Sandbox2D() = default;
//
//	virtual void OnAttach() override;
//	virtual void OnDetach() override;
//
//	void OnUpdate(Proto::Timestep ts) override;
//	virtual void OnImGuiRender() override;
//	void OnEvent(Proto::Event& e) override;
//private:
//	//Proto::OrthographicCameraController m_CameraController;
//
//	// Temp
//	Proto::Ref<Proto::VertexArray> m_SquareVA;
//	Proto::Ref<Proto::Shader> m_FlatColorShader;
//
//	Proto::Ref<Proto::Texture2D> m_CheckerboardTexture;
//
//	glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
//};