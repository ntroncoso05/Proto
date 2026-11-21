#pragma once
#include "Scene.hpp"
#include <memory>

namespace Proto {
    class PhysicsSystem {
    public:
        PhysicsSystem();
        ~PhysicsSystem();

        void Init();
        void Shutdown();

        void OnSceneStart(Scene& scene);
        void Step(Scene& scene, float deltaTime);
        void OnSceneStop(Scene& scene);

        bool IsSimulating() const { return m_IsSimulating; }

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        float m_Accumulator = 0.0f;
        bool m_IsSimulating = false;
    };
}