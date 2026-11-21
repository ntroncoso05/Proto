#include "protopch.hpp"
#include "PhysicsSystem.hpp"
#include "ComponentTypes.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

JPH_SUPPRESS_WARNINGS

namespace Proto {

    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    }

    namespace BroadPhaseLayers {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr uint32_t NUM_LAYERS = 2;
    }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            return (inLayer == Layers::NON_MOVING) ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            return (inLayer == BroadPhaseLayers::NON_MOVING) ? "NON_MOVING" : "MOVING";
        }
#endif
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
            if (inLayer1 == Layers::NON_MOVING) return inLayer2 == BroadPhaseLayers::MOVING;
            return true;
        }
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
            if (inObject1 == Layers::NON_MOVING) return inObject2 == Layers::MOVING;
            return true;
        }
    };

    struct PhysicsSystem::Impl {
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
        BPLayerInterfaceImpl bpLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl objVsBpFilter;
        ObjectLayerPairFilterImpl objPairFilter;
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
    };

    PhysicsSystem::PhysicsSystem() : m_Impl(std::make_unique<Impl>()) {}
    PhysicsSystem::~PhysicsSystem() { Shutdown(); }

    void PhysicsSystem::Init()
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        uint32_t workerThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        m_Impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreads);

        m_Impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        m_Impl->physicsSystem->Init(1024, 0, 1024, 1024,
            m_Impl->bpLayerInterface, m_Impl->objVsBpFilter, m_Impl->objPairFilter);
        m_Impl->physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    }

    void PhysicsSystem::Shutdown()
    {
        if (!m_Impl || !m_Impl->physicsSystem) return;
        m_Impl->physicsSystem.reset();
        m_Impl->jobSystem.reset();
        m_Impl->tempAllocator.reset();
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    void PhysicsSystem::OnSceneStart(Scene& scene)
    {
        if (!m_Impl->physicsSystem) return;
        auto& bodyInterface = m_Impl->physicsSystem->GetBodyInterface();

        for (const auto& entPtr : scene.GetEntities()) {
            Entity* entity = entPtr.get();
            if (!entity || !entity->IsActive()) continue;

            auto* rb = entity->GetComponent<RigidBodyComponent>();
            auto* box = entity->GetComponent<BoxColliderComponent>();
            auto* trans = entity->GetComponent<TransformComponent>();
            if (!rb || !box || !trans) continue;

            glm::vec3 scl = trans->GetScale();
            JPH::Vec3 halfExtents(
                std::max(0.01f, box->HalfExtents.x * scl.x),
                std::max(0.01f, box->HalfExtents.y * scl.y),
                std::max(0.01f, box->HalfExtents.z * scl.z)
            );
            JPH::BoxShapeSettings shapeSettings(halfExtents);
            JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
            if (shapeResult.HasError()) continue;

            glm::vec3 pos = trans->GetPosition() + (trans->GetRotation() * box->Offset);
            glm::quat rot = glm::normalize(trans->GetRotation());

            JPH::EMotionType motion = JPH::EMotionType::Dynamic;
            JPH::ObjectLayer layer = Layers::MOVING;
            if (rb->MotionType == BodyMotionType::Static) {
                motion = JPH::EMotionType::Static;
                layer = Layers::NON_MOVING;
            }
            else if (rb->MotionType == BodyMotionType::Kinematic) {
                motion = JPH::EMotionType::Kinematic;
            }

            JPH::BodyCreationSettings bodySettings(
                shapeResult.Get(),
                JPH::RVec3(pos.x, pos.y, pos.z),
                JPH::Quat(rot.x, rot.y, rot.z, rot.w),
                motion,
                layer
            );
            bodySettings.mFriction = rb->Friction;
            bodySettings.mRestitution = rb->Restitution;
            bodySettings.mLinearDamping = rb->LinearDamping;
            bodySettings.mAngularDamping = rb->AngularDamping;
            bodySettings.mGravityFactor = rb->UseGravity ? 1.0f : 0.0f;
            bodySettings.mIsSensor = box->IsTrigger;

            JPH::Body* body = bodyInterface.CreateBody(bodySettings);
            if (body) {
                bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
                rb->RuntimeBodyID = body->GetID().GetIndexAndSequenceNumber();
            }
        }

        m_Impl->physicsSystem->OptimizeBroadPhase();
        m_Accumulator = 0.0f;
        m_IsSimulating = true;
    }

    void PhysicsSystem::Step(Scene& scene, float deltaTime)
    {
        if (!m_IsSimulating || !m_Impl->physicsSystem) return;

        constexpr float fixedStep = 1.0f / 60.0f;
        m_Accumulator = std::min(m_Accumulator + deltaTime, 0.25f);

        while (m_Accumulator >= fixedStep) {
            m_Impl->physicsSystem->Update(fixedStep, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());
            m_Accumulator -= fixedStep;
        }

        // Sync dynamic Jolt transforms back to ECS TransformComponents
        auto& bodyInterface = m_Impl->physicsSystem->GetBodyInterface();
        for (const auto& entPtr : scene.GetEntities()) {
            Entity* entity = entPtr.get();
            if (!entity || !entity->IsActive()) continue;

            auto* rb = entity->GetComponent<RigidBodyComponent>();
            auto* box = entity->GetComponent<BoxColliderComponent>();
            auto* trans = entity->GetComponent<TransformComponent>();
            if (!rb || !trans || rb->RuntimeBodyID == 0xFFFFFFFF || rb->MotionType != BodyMotionType::Dynamic) continue;

            JPH::BodyID id(rb->RuntimeBodyID);
            if (!bodyInterface.IsActive(id)) continue;

            JPH::RVec3 p = bodyInterface.GetCenterOfMassPosition(id);
            JPH::Quat  q = bodyInterface.GetRotation(id);

            glm::quat glmRot(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
            glm::vec3 offsetWorld = box ? (glmRot * box->Offset) : glm::vec3(0.0f);
            trans->SetPosition(glm::vec3(p.GetX(), p.GetY(), p.GetZ()) - offsetWorld);
            trans->SetRotation(glmRot);
        }
    }

    void PhysicsSystem::OnSceneStop(Scene& scene)
    {
        if (!m_Impl->physicsSystem) return;
        auto& bodyInterface = m_Impl->physicsSystem->GetBodyInterface();

        for (const auto& entPtr : scene.GetEntities()) {
            if (!entPtr) continue;
            if (auto* rb = entPtr->GetComponent<RigidBodyComponent>()) {
                if (rb->RuntimeBodyID != 0xFFFFFFFF) {
                    JPH::BodyID id(rb->RuntimeBodyID);
                    bodyInterface.RemoveBody(id);
                    bodyInterface.DestroyBody(id);
                    rb->RuntimeBodyID = 0xFFFFFFFF;
                }
            }
        }
        m_IsSimulating = false;
    }
}