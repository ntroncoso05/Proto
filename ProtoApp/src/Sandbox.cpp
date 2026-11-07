#include <Proto.hpp>
#include <Core/EntryPoint.hpp>
#include <imgui.h>
//#include <Core/InputSystem.hpp>
//#include <Core/Component.hpp>
#include <Core/ComponentTypes.hpp>
#include <Core/Entity.hpp>
#include <Core/ServiceLocator.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>
#include "Renderer/RendererPipeline.hpp"

class ExampleLayer : public Proto::Layer
{
public:
	ExampleLayer()
		: Layer("Example")//, m_CameraController(1280.0f / 720.0f)
	{
		m_Camera = std::make_shared<Proto::Camera>(glm::vec3(0.0f, 0.0f, 5.0f));
	}

	void OnUpdate(Proto::Timestep ts) override
	{
		// 1. Check if ImGui is capturing input. If true, block keyboard locomotion controls completely.
		if (!ImGui::GetIO().WantCaptureKeyboard)
		{
			// 2. Fetch the native platform window directly from your running Application instance context
			auto* nativeWindow = static_cast<GLFWwindow*>(Proto::Application::Get().GetWindow().GetNativeWindow());
			auto* transform = m_RoomEntity->GetComponent<Proto::TransformComponent>();
			glm::vec3 movementDirection(0.0f);

			// 🟩 Update input for your shared camera pointer
			if (m_Camera) {
				if (glfwGetKey(nativeWindow, GLFW_KEY_W) == GLFW_PRESS) m_Camera->ProcessKeyboardMovement(Proto::CameraMovement::FORWARD, ts);
				if (glfwGetKey(nativeWindow, GLFW_KEY_S) == GLFW_PRESS) m_Camera->ProcessKeyboardMovement(Proto::CameraMovement::BACKWARD, ts);
				if (glfwGetKey(nativeWindow, GLFW_KEY_A) == GLFW_PRESS) m_Camera->ProcessKeyboardMovement(Proto::CameraMovement::LEFT, ts);
				if (glfwGetKey(nativeWindow, GLFW_KEY_D) == GLFW_PRESS) m_Camera->ProcessKeyboardMovement(Proto::CameraMovement::RIGHT, ts);
				if (glfwGetKey(nativeWindow, GLFW_KEY_Q) == GLFW_PRESS) m_Camera->ProcessKeyboardMovement(Proto::CameraMovement::UP, ts);
				if (glfwGetKey(nativeWindow, GLFW_KEY_E) == GLFW_PRESS) m_Camera->ProcessKeyboardMovement(Proto::CameraMovement::DOWN, ts);
				m_Camera->processInput(nativeWindow, m_Camera, ts);
			}

			// Find the camera entity in your scene and sync its component matrices
			for (auto* entity : Proto::Entity::GetActiveSceneEntities()) {
				if (entity->GetName() == "MainCamera") {
					auto* transform = entity->GetComponent<Proto::TransformComponent>();
					if (transform) {
						// Keep the component position synced with your interactive keyboard controls!
						transform->SetPosition(m_Camera->getPosition());
						//transform->SetRotation(glm::quat(glm::radians(m_Camera->GetPitchYawRoll()))); // Sync orientation angles
					}
					break;
				}
			}

			// 3. Process frame-continuous polling commands safely 
			if (glfwGetKey(nativeWindow, GLFW_KEY_W) == GLFW_PRESS)  movementDirection.z -= 1.0f;
			if (glfwGetKey(nativeWindow, GLFW_KEY_S) == GLFW_PRESS) movementDirection.z += 1.0f;
			if (glfwGetKey(nativeWindow, GLFW_KEY_A) == GLFW_PRESS) movementDirection.x -= 1.0f;
			if (glfwGetKey(nativeWindow, GLFW_KEY_D) == GLFW_PRESS) movementDirection.x += 1.0f;
			if (glfwGetKey(nativeWindow, GLFW_KEY_Q) == GLFW_PRESS) movementDirection.y += 1.0f;// Up navigation mapping
			if (glfwGetKey(nativeWindow, GLFW_KEY_E) == GLFW_PRESS) movementDirection.y -= 1.0f;// Down navigation mapping

			// CRITICAL FIX 1: Normalize vector to prevent rapid diagonal acceleration!
			if (glm::length(movementDirection) > 0.0f)
			{
				movementDirection = glm::normalize(movementDirection);

				// CRITICAL FIX 2: Only apply single delta calculation here if components do not update positions internally
				float movementSpeed = 5.0f;
				transform->SetPosition(transform->GetPosition() + (movementDirection * movementSpeed * (float)ts));
			}
		}

		if (m_Planet && m_Moon)
		{
			static float accumulatedTime = 0.0f;
			accumulatedTime += ts;

			// 1. Rotate the Parent Planet slowly over time
			auto* pTrans = m_Planet->GetComponent<Proto::TransformComponent>();
			glm::quat planetRotation = glm::angleAxis(accumulatedTime * glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			pTrans->SetRotation(planetRotation);

			// 2. Slowly move the entire system forward and backward along world Z axis
			float backAndForthOffset = sin(accumulatedTime) * 2.0f;
			pTrans->SetPosition(glm::vec3(0.0f, 0.0f, -10.0f + backAndForthOffset));

			// 3. Rotate the Moon locally on its own axis independently
			auto* cTrans = m_Moon->GetComponent<Proto::TransformComponent>();
			glm::quat moonLocalRotation = glm::angleAxis(accumulatedTime * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			cTrans->SetRotation(moonLocalRotation);

			// Execute standard Component updates down through the active entities
			m_Planet->Update(ts);
			m_Moon->Update(ts);
		}
		// Render world submission passes
		Proto::Renderer::BeginScene();
		Proto::Renderer::Submit(*m_Camera, ts);
		Proto::Renderer::EndScene();
	}

	virtual void OnImGuiRender() override
	{
		//ImGui::Begin("Settings");
		//ImGui::ColorEdit3("Square Color", glm::value_ptr(m_SquareColor));
		//ImGui::End();
	}

	void OnEvent(Proto::Event& e) override
	{
		if (m_Camera) m_Camera->OnEvent(e);
		if (m_RoomEntity) m_RoomEntity->OnEvent(e);
	}

	void OnAttach()
	{
		// FORCE-REGISTER THE ENGINE INSTANCE FIRST IF AN EARLY TICK OMISSION HID IT
		if (!Proto::ServiceLocator::Contains<Proto::VulkanRenderer>()) {
			auto& apiInstance = static_cast<Proto::VulkanRenderer&>(*Proto::RendererAPI::GetInstance());
			Proto::ServiceLocator::Register<Proto::VulkanRenderer>(&apiInstance);
		}

		// STEP A: TRIGGER PBR SHADER & PIPELINE COMPILATION ON LAYER ENTRY
		// At this point in the engine lifecycle, VulkanRenderer is guaranteed to be fully resident!
		auto& pbrRenderer = Proto::ServiceLocator::Get<Proto::SceneRenderer>();
		if (!pbrRenderer.CreatePBRPipeline()) {
			std::cerr << "[FATAL]: Failed to compile PBR pipelines during scene initialization!" << std::endl;
			return;
		}

		// STEP 1: FORCE THE GPU TO GO IDLE BEFORE LOADING ANY TEXTURES!
		// This clears the 'image has not been acquired' race conditions and lets EndSingleTimeCommands upload textures successfully!
		auto& lowLevelRenderer = Proto::VulkanRenderer::Get();
		lowLevelRenderer.vulkanContext->GetDevice().logicalDevice.waitIdle();

		auto& resourceManager = Proto::ServiceLocator::Get<Proto::ResourceManager>();

		std::cout << "[ENGINE]: Device synchronized safely. Launching asset texture loading stream..." << std::endl;

		// Load the full model container asset cleanly 
		auto modelHandle = resourceManager.Load<Proto::Model>("viking_room");

		if (!modelHandle.IsValid() || modelHandle->nodes.empty()) {
			std::cerr << "[ERROR]: Viking Room model container failed to load or has 0 meshes!" << std::endl;
			return;
		}

		// FIXED: Change from raw pointer leak to safe class unique_ptr allocation!
		m_CameraEntity = std::make_unique<Proto::Entity>("MainCamera");
		auto* camComp = m_CameraEntity->AddComponent<Proto::CameraComponent>();
		camComp->SetPerspective(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f);

		auto* camTransform = m_CameraEntity->AddComponent<Proto::TransformComponent>();
		camTransform->SetPosition(glm::vec3(0.0f, 3.0f, 10.0f));
		m_CameraEntity->Initialize();

		// STEP 1: CREATE A DEDICATED CAMERA ENTITY 
		// This allows the SceneRenderer loop to discover it automatically via Service Locator checks!
		// A. Configure Main Camera metrics
		m_RoomEntity = std::make_unique<Proto::Entity>("VikingRoom");
		auto* roomTransform = m_RoomEntity->AddComponent<Proto::TransformComponent>();
		roomTransform->SetScale(glm::vec3(1.0f, 1.0f, 1.0f));
		roomTransform->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));

		// FIXED: Inject ModelComponent instead of MeshComponent!
		m_RoomEntity->AddComponent<Proto::ModelComponent>(modelHandle);
		m_RoomEntity->Initialize();

		// C. Create the Planet Node
		m_Planet = std::make_unique<Proto::Entity>("Planet_Root");
		auto* pTrans = m_Planet->AddComponent<Proto::TransformComponent>();

		// FIXED: Inject ModelComponent instead of MeshComponent!
		m_Planet->AddComponent<Proto::ModelComponent>(modelHandle);
		pTrans->SetPosition(glm::vec3(-6.0f, 0.0f, -5.0f));
		pTrans->SetScale(glm::vec3(1.5f));
		m_Planet->Initialize();

		// D. Create the Child Moon Node
		m_Moon = std::make_unique<Proto::Entity>("Moon_Child");
		auto* cTrans = m_Moon->AddComponent<Proto::TransformComponent>();

		// FIXED: Inject ModelComponent instead of MeshComponent!
		m_Moon->AddComponent<Proto::ModelComponent>(modelHandle);
		cTrans->SetPosition(glm::vec3(3.5f, 0.0f, 0.0f));
		cTrans->SetScale(glm::vec3(0.5f));
		m_Moon->SetParent(m_Planet.get());
		m_Moon->Initialize();
	}
private:
	std::shared_ptr<Proto::Camera> m_Camera;
	std::unique_ptr<Proto::Entity> m_CameraEntity;
	std::unique_ptr<Proto::Entity> m_RoomEntity;
	std::unique_ptr<Proto::Entity> m_Planet;
	std::unique_ptr<Proto::Entity> m_Moon;
};

class Sandbox : public Proto::Application
{
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer());
	}

	~Sandbox()
	{}
};

Proto::Application* Proto::CreateApplication()
{
	return new Sandbox();
}