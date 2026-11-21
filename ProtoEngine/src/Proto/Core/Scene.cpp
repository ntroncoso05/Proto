#include "protopch.hpp"
#include "Scene.hpp"

namespace Proto {
    Entity* Scene::CreateEntity(const std::string& name, Entity* parent)
    {
        auto entity = std::make_unique<Entity>(name);
        Entity* ptr = entity.get();
        if (parent) {
            ptr->SetParent(parent);
        }
        m_Entities.push_back(std::move(entity));
        return ptr;
    }

    void Scene::DestroyEntity(Entity* entity)
    {
        if (!entity) return;
        if (std::find(m_PendingDeletions.begin(), m_PendingDeletions.end(), entity) == m_PendingDeletions.end()) {
            m_PendingDeletions.push_back(entity);
        }
    }

    void Scene::ProcessPendingDeletions()
    {
        if (m_PendingDeletions.empty()) return;

        // Collect target entities and all their recursive descendants
        std::vector<Entity*> toErase;
        std::function<void(Entity*)> collect = [&](Entity* e) {
            if (!e) return;
            for (Entity* child : e->GetChildren()) {
                collect(child);
            }
            if (std::find(toErase.begin(), toErase.end(), e) == toErase.end()) {
                toErase.push_back(e);
            }
            };

        for (Entity* e : m_PendingDeletions) {
            collect(e);
        }
        m_PendingDeletions.clear();

        // Detach from parents before erasing
        for (Entity* e : toErase) {
            e->SetParent(nullptr);
        }

        m_Entities.erase(
            std::remove_if(m_Entities.begin(), m_Entities.end(), [&](const std::unique_ptr<Entity>& ptr) {
                return std::find(toErase.begin(), toErase.end(), ptr.get()) != toErase.end();
                }),
            m_Entities.end()
        );
    }

    void Scene::Clear()
    {
        m_PendingDeletions.clear();
        // Detach hierarchy links first so destructors don't touch freed siblings
        for (auto& e : m_Entities) {
            if (e) e->SetParent(nullptr);
        }
        m_Entities.clear();
    }

    Entity* Scene::FindEntityByName(const std::string& name)
    {
        for (auto& e : m_Entities) {
            if (e && e->GetName() == name) return e.get();
        }
        return nullptr;
    }
}