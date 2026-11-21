#pragma once
#include "Entity.hpp"
#include <memory>
#include <vector>
#include <string>

namespace Proto {
    class Scene {
    public:
        Scene() = default;
        ~Scene() { Clear(); }

        Entity* CreateEntity(const std::string& name, Entity* parent = nullptr);
        void DestroyEntity(Entity* entity);
        void ProcessPendingDeletions();
        void Clear();

        Entity* FindEntityByName(const std::string& name);
        const std::vector<std::unique_ptr<Entity>>& GetEntities() const { return m_Entities; }

    private:
        std::vector<std::unique_ptr<Entity>> m_Entities;
        std::vector<Entity*> m_PendingDeletions;
    };
}