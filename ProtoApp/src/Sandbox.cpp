#include <Proto.hpp>
#include <Core/EntryPoint.hpp>
#include <Core/ComponentTypes.hpp>
#include <Core/Entity.hpp>
#include <Core/ServiceLocator.hpp>
#include <Core/ImGuiLayer.hpp>
#include <Core/Scene.hpp>
#include <Renderer/RendererPipeline.hpp>
#include <Renderer/EditorCameraController.hpp>

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class ExampleLayer : public Proto::Layer
{
public:
	ExampleLayer() : Layer("Example"){	}

	// Helper in Sandbox.cpp: Compute true local-space AABB of a Model (including glTF node matrices)
	static Proto::BoundingBox GetModelLocalAABB(Proto::Model* model)
	{
		Proto::BoundingBox box;
		if (!model || model->nodes.empty()) return box;

		glm::vec3 minB(std::numeric_limits<float>::max());
		glm::vec3 maxB(std::numeric_limits<float>::lowest());
		bool hasVertices = false;

		std::function<void(Proto::Node*, const glm::mat4&)> traverse =
			[&](Proto::Node* node, const glm::mat4& parentMat) {
			if (!node) return;
			glm::mat4 worldNodeMat = parentMat * node->getLocalMatrix();

			if (node->mesh) {
				for (const auto& v : node->mesh->GetRawVertices()) {
					glm::vec3 p = glm::vec3(worldNodeMat * glm::vec4(v.pos, 1.0f));
					minB = glm::min(minB, p);
					maxB = glm::max(maxB, p);
					hasVertices = true;
				}
			}
			for (auto* child : node->children) {
				traverse(child, worldNodeMat);
			}
			};

		for (auto* root : model->nodes) {
			traverse(root, glm::mat4(1.0f));
		}

		if (hasVertices) {
			box.minBound = minB;
			box.maxBound = maxB;
		}
		return box;
	}

	static bool IntersectRayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
		const glm::vec3& boxMin, const glm::vec3& boxMax, float& tOut)
	{
		float tmin = 0.0f;
		float tmax = std::numeric_limits<float>::max();

		for (int i = 0; i < 3; ++i) {
			if (std::abs(rayDir[i]) < 1e-6f) {
				if (rayOrigin[i] < boxMin[i] || rayOrigin[i] > boxMax[i]) return false;
			}
			else {
				float invD = 1.0f / rayDir[i];
				float t1 = (boxMin[i] - rayOrigin[i]) * invD;
				float t2 = (boxMax[i] - rayOrigin[i]) * invD;
				if (t1 > t2) std::swap(t1, t2);
				tmin = std::max(tmin, t1);
				tmax = std::min(tmax, t2);
				if (tmin > tmax) return false;
			}
		}
		tOut = tmin;
		return true;
	}

	void OnUpdate(Proto::Timestep ts) override
	{
		if (!m_ActiveScene) return;

		// 1. Find whichever entity currently holds the active CameraComponent
		Proto::Entity* cameraEntity = nullptr;
		for (const auto& entPtr : m_ActiveScene->GetEntities()) {
			if (entPtr && entPtr->IsActive() && entPtr->GetComponent<Proto::CameraComponent>()) {
				cameraEntity = entPtr.get();
				break;
			}
		}

		m_CameraController.OnUpdate(ts, *m_ActiveScene);

		Proto::Entity* planet = m_ActiveScene->FindEntityByName("Planet_Root");
		Proto::Entity* moon = m_ActiveScene->FindEntityByName("Moon_Child");
		if (planet && moon) {
			static float accumulatedTime = 0.0f;
			accumulatedTime += ts;

			if (auto* pTrans = planet->GetComponent<Proto::TransformComponent>()) {
				pTrans->SetRotation(glm::angleAxis(accumulatedTime * glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
				pTrans->SetPosition(glm::vec3(0.0f, 0.0f, -10.0f + sin(accumulatedTime) * 2.0f));
			}
			if (auto* cTrans = moon->GetComponent<Proto::TransformComponent>()) {
				cTrans->SetRotation(glm::angleAxis(accumulatedTime * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
			}
		}

		for (const auto& entPtr : m_ActiveScene->GetEntities()) {
			Proto::Entity* entity = entPtr.get();
			if (!entity || !entity->IsActive()) continue;
			auto* modelComp = entity->GetComponent<Proto::ModelComponent>();
			auto* animComp = entity->GetComponent<Proto::AnimationComponent>();
			if (!modelComp || !animComp || !animComp->IsPlaying) continue;

			if (modelComp->GetModelHandle().IsValid()) {
				Proto::Model* rawModel = modelComp->GetModelHandle().Get();
				if (rawModel && rawModel->IsLoaded() && !rawModel->animations.empty()) {
					uint32_t clip = std::min(animComp->ActiveClipIndex, static_cast<uint32_t>(rawModel->animations.size() - 1));
					rawModel->updateAnimation(clip, ts * animComp->PlaybackSpeed, animComp->Loop);
					animComp->CurrentTime = rawModel->animations[clip].currentTime;
				}
			}
		}

		// Render world submission passes
		Proto::Renderer::BeginScene();
		Proto::Renderer::Submit(ts);
		Proto::Renderer::EndScene();

		m_PhysicsSystem->Step(*m_ActiveScene, static_cast<float>(ts));

		m_ActiveScene->ProcessPendingDeletions();
	}

	virtual void OnImGuiRender() override
	{
		//ImGui::Begin("Settings");
		//ImGui::ColorEdit3("Square Color", glm::value_ptr(m_SquareColor));
		//ImGui::End();
	}

	void OnEvent(Proto::Event& e) override
	{
		if (!m_ActiveScene) return;
		m_CameraController.OnEvent(e, *m_ActiveScene);
		Proto::Entity* cameraEntity = nullptr;
		for (const auto& entPtr : m_ActiveScene->GetEntities()) {
			if (entPtr && entPtr->IsActive() && entPtr->GetComponent<Proto::CameraComponent>()) {
				cameraEntity = entPtr.get();
				break;
			}
		}
		if (!cameraEntity) return;
		auto* camComp = cameraEntity->GetComponent<Proto::CameraComponent>();

		Proto::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Proto::MouseButtonPressedEvent>([this, cameraEntity, camComp](const Proto::MouseButtonPressedEvent& ev) {
			if (ev.GetMouseButton() != Proto::Mouse::ButtonLeft) return false;
			bool altDown = Proto::Input::IsKeyPressed(Proto::Key::LeftAlt) || Proto::Input::IsKeyPressed(Proto::Key::RightAlt);
			auto imGuiUtil = Proto::VulkanRenderer::Get().imGui;
			if (altDown || !imGuiUtil || !imGuiUtil->IsViewportHovered() || ImGuizmo::IsOver()) return false;

			// ... existing ray-AABB entity picking using Proto::Input::GetMousePosition() ...
			glm::vec2 mousePos = Proto::Input::GetMousePosition();

			glm::vec2 vpMin = imGuiUtil->GetViewportMin();
			glm::vec2 vpSize = imGuiUtil->GetViewportSize();

			float ndcX = ((static_cast<float>(mousePos.x) - vpMin.x) / vpSize.x) * 2.0f - 1.0f;
			float ndcY = ((static_cast<float>(mousePos.y) - vpMin.y) / vpSize.y) * 2.0f - 1.0f;

			if (camComp && vpSize.x > 0.0f && vpSize.y > 0.0f) {
				glm::mat4 proj = camComp->GetProjectionMatrix();
				glm::mat4 invVP = glm::inverse(proj * camComp->GetViewMatrix());

				glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
				glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
				nearPoint /= nearPoint.w;
				farPoint /= farPoint.w;

				glm::vec3 rayOrigin = glm::vec3(nearPoint);
				glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

				Proto::Entity* closestEntity = nullptr;
				float closestDist = std::numeric_limits<float>::max();

				for (auto* entity : Proto::Entity::GetActiveSceneEntities()) {
					if (entity == cameraEntity) continue;

					auto* transform = entity->GetComponent<Proto::TransformComponent>();
					auto* modelComp = entity->GetComponent<Proto::ModelComponent>();
					if (!transform || !modelComp || !modelComp->GetModelHandle().IsValid()) continue;

					Proto::BoundingBox worldBox = GetModelLocalAABB(modelComp->GetModelHandle().Get());
					worldBox.Transform(transform->GetWorldTransformMatrix());

					float hitDist = 0.0f;
					if (IntersectRayAABB(rayOrigin, rayDir, worldBox.minBound, worldBox.maxBound, hitDist)) {
						if (closestDist > hitDist) {
							closestDist = hitDist;
							closestEntity = entity;
						}
					}
				}

				if (closestEntity != nullptr) {
					imGuiUtil->SetSelectedEntity(closestEntity);
				}

				return true;
			}
			return false;
		});
	}

	void OnAttach()
	{
		// FORCE-REGISTER THE ENGINE INSTANCE FIRST IF AN EARLY TICK OMISSION HID IT
		if (!Proto::ServiceLocator::Contains<Proto::VulkanRenderer>()) {
			auto& apiInstance = static_cast<Proto::VulkanRenderer&>(*Proto::RendererAPI::GetInstance());
			Proto::ServiceLocator::Register<Proto::VulkanRenderer>(&apiInstance);
		}
		m_ActiveScene = std::make_unique<Proto::Scene>();
		Proto::ServiceLocator::Register<Proto::Scene>(m_ActiveScene.get());

		m_PhysicsSystem = std::make_unique<Proto::PhysicsSystem>();
		m_PhysicsSystem->Init();
		Proto::ServiceLocator::Register<Proto::PhysicsSystem>(m_PhysicsSystem.get());

		// FORCE THE GPU TO GO IDLE BEFORE LOADING ANY TEXTURES!. This clears the 'image has not been acquired' race conditions and lets EndSingleTimeCommands upload textures successfully!
		auto& lowLevelRenderer = Proto::VulkanRenderer::Get();
		lowLevelRenderer.vulkanContext->GetDevice().logicalDevice.waitIdle();

		auto& resourceManager = Proto::ServiceLocator::Get<Proto::ResourceManager>();
		std::cout << "[ENGINE]: Device synchronized safely. Launching asset texture loading stream..." << std::endl;

		if (auto groundModel = Proto::Model::CreateGroundPlane("procedural_ground_plane", 25.0f, 10.0f)) {
			resourceManager.RegisterExternal<Proto::Model>("procedural_ground_plane", groundModel);
		}

		auto groundHandle = resourceManager.Load<Proto::Model>("procedural_ground_plane");
		if (groundHandle.IsValid()) {
			auto* groundEntity = m_ActiveScene->CreateEntity("GroundPlane");
			auto* groundTrans = groundEntity->AddComponent<Proto::TransformComponent>();
			groundTrans->SetPosition(glm::vec3(0.0f, -1.0f, -2.0f)); // Directly beneath the Car's tires!
			groundEntity->AddComponent<Proto::ModelComponent>(groundHandle);
			
			auto* groundRb = groundEntity->AddComponent<Proto::RigidBodyComponent>();
			groundRb->MotionType = Proto::BodyMotionType::Static;
			groundRb->Friction = 0.7f;

			auto* groundBox = groundEntity->AddComponent<Proto::BoxColliderComponent>();
			groundBox->HalfExtents = glm::vec3(25.0f, 0.1f, 25.0f);
			groundBox->Offset = glm::vec3(0.0f, -0.1f, 0.0f);
			groundEntity->Initialize();
		}

		// Load the full model container asset cleanly 
		auto modelHandle = resourceManager.Load<Proto::Model>("viking_room");

		if (!modelHandle.IsValid() || modelHandle->nodes.empty()) {
			std::cerr << "[ERROR]: Viking Room model container failed to load or has 0 meshes!" << std::endl;
			return;
		}

		// Change from raw pointer leak to safe class unique_ptr allocation!
		auto* entityC = m_ActiveScene->CreateEntity("MainCamera");
		auto* camComp = entityC->AddComponent<Proto::CameraComponent>();
		camComp->SetPerspective(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f);

		auto* camTransform = entityC->AddComponent<Proto::TransformComponent>();
		camTransform->SetPosition(glm::vec3(0.0f, 3.0f, 10.0f));
		entityC->Initialize();

		auto* entityR = m_ActiveScene->CreateEntity("VikingRoom");
		auto* roomTransform = entityR->AddComponent<Proto::TransformComponent>();
		roomTransform->SetScale(glm::vec3(1.0f, 1.0f, 1.0f));
		roomTransform->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
		// Inject ModelComponent instead of MeshComponent!
		entityR->AddComponent<Proto::ModelComponent>(modelHandle);
		entityR->Initialize();

		// Create the Planet Node
		auto* entityP = m_ActiveScene->CreateEntity("Planet_Root");
		auto* pTrans = entityP->AddComponent<Proto::TransformComponent>();
		entityP->AddComponent<Proto::ModelComponent>(modelHandle);
		pTrans->SetPosition(glm::vec3(-6.0f, 0.0f, -5.0f));
		pTrans->SetScale(glm::vec3(1.5f));
		entityP->Initialize();

		// Create the Child Moon Node
		auto* entityM = m_ActiveScene->CreateEntity("Moon_Child");
		auto* cTrans = entityM->AddComponent<Proto::TransformComponent>();		
		entityM->AddComponent<Proto::ModelComponent>(modelHandle);
		cTrans->SetPosition(glm::vec3(3.5f, 0.0f, 0.0f));
		cTrans->SetScale(glm::vec3(0.5f));
		entityM->SetParent(entityP);
		entityM->Initialize();

		auto* entityL = m_ActiveScene->CreateEntity("MainLight");
		auto* lightTransform = entityL->AddComponent<Proto::TransformComponent>();
		lightTransform->SetPosition(glm::vec3(3.0f, 5.0f, 4.0f));
		lightTransform->SetRotation(glm::quat(glm::radians(glm::vec3(-45.0f, 30.0f, 0.0f))));
		auto* lightComp = entityL->AddComponent<Proto::LightComponent>();
		lightComp->Type = Proto::LightType::Directional;
		lightComp->Color = glm::vec3(1.0f, 0.95f, 0.85f);
		lightComp->Intensity = 3.0f;
		lightComp->Radius = 5.5f;
		lightComp->CastShadows = true;
		entityL->Initialize();

		auto carHandle = resourceManager.Load<Proto::Model>("CarConcept");
		if (carHandle.IsValid()) {
			auto* entity = m_ActiveScene->CreateEntity("Car");
			auto* carTrans = entity->AddComponent<Proto::TransformComponent>();
			carTrans->SetPosition(glm::vec3(-3.0f, 4.0f, -2.0f));
			carTrans->SetScale(glm::vec3(1.0f, 1.0f, 1.0f));
			entity->AddComponent<Proto::ModelComponent>(carHandle);
			
			auto* carRb = entity->AddComponent<Proto::RigidBodyComponent>();
			carRb->MotionType = Proto::BodyMotionType::Dynamic;
			carRb->Restitution = 0.3f;

			auto* carBox = entity->AddComponent<Proto::BoxColliderComponent>();
			carBox->HalfExtents = glm::vec3(1.1f, 0.7f, 2.2f);
			carBox->Offset = glm::vec3(0.0f, 0.7f, 0.0f);

			auto* carPE = entity->AddComponent<Proto::ParticleEmitterComponent>();
			entity->Initialize();

			if (!carHandle->animations.empty()) {
				entity->AddComponent<Proto::AnimationComponent>();
			}
		}

		auto watchHandle = resourceManager.Load<Proto::Model>("ChronographWatch");
		if (watchHandle.IsValid()) {
			auto* entity = m_ActiveScene->CreateEntity("Watch");
			auto* groundTrans = entity->AddComponent<Proto::TransformComponent>();
			groundTrans->SetPosition(glm::vec3(3.0f, 5.0f, -2.0f));
			groundTrans->SetScale(glm::vec3(0.5f, 0.5f, 0.5f));
			entity->AddComponent<Proto::ModelComponent>(watchHandle);

			auto* watchRb = entity->AddComponent<Proto::RigidBodyComponent>();
			watchRb->MotionType = Proto::BodyMotionType::Dynamic;
			watchRb->Restitution = 0.3f;

			auto* watchBox = entity->AddComponent<Proto::BoxColliderComponent>();
			watchBox->HalfExtents = glm::vec3(1.1f, 0.7f, 2.2f);
			watchBox->Offset = glm::vec3(0.0f, 0.7f, 0.0f);
			entity->Initialize();

			if (!watchHandle->animations.empty()) {
				entity->AddComponent<Proto::AnimationComponent>();
			}
		}

		auto sheriffHandle = resourceManager.Load<Proto::Model>("sheriff-office");
		if (sheriffHandle.IsValid()) {
			auto* entity = m_ActiveScene->CreateEntity("sheriff");
			auto* groundTrans = entity->AddComponent<Proto::TransformComponent>();
			groundTrans->SetPosition(glm::vec3(-3.0f, 7.0f, -2.0f));
			groundTrans->SetScale(glm::vec3(0.5f, 0.5f, 0.5f));
			entity->AddComponent<Proto::ModelComponent>(sheriffHandle);
			entity->Initialize();
		}

		auto mushroomHandle = resourceManager.Load<Proto::Model>("mushroom-house");
		if (mushroomHandle.IsValid()) {
			auto* entity = m_ActiveScene->CreateEntity("MushroomHouse");
			auto* groundTrans = entity->AddComponent<Proto::TransformComponent>();
			groundTrans->SetPosition(glm::vec3(0.0f, 7.0f, -2.0f));
			groundTrans->SetScale(glm::vec3(0.5f, 0.5f, 0.5f));
			entity->AddComponent<Proto::ModelComponent>(mushroomHandle);
			entity->Initialize();
		}
	}

	void OnDetach()
	{
		m_PhysicsSystem->OnSceneStop(*m_ActiveScene);
		m_PhysicsSystem->Shutdown();
		m_ActiveScene->Clear();
	}
private:
	Proto::EditorCameraController m_CameraController;
	std::unique_ptr<Proto::Scene> m_ActiveScene;
	std::unique_ptr<Proto::PhysicsSystem> m_PhysicsSystem;
};

class Sandbox : public Proto::Application
{
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer());
	}

	~Sandbox() {}
};

Proto::Application* Proto::CreateApplication()
{
	return new Sandbox();
}