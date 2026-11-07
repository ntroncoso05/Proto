#include "protopch.hpp"
#include "Entity.hpp"

namespace Proto
{
    std::vector<Entity*> Entity::s_SceneEntities;

	Entity::Entity(const std::string& entityName) : name(entityName) {
        // Track every spawned entity in our active scene registry automatically
        s_SceneEntities.push_back(this);
    }

    Entity::~Entity()
    {
        // Safe clean unregistration when entities are destroyed
        s_SceneEntities.erase(std::remove(s_SceneEntities.begin(), s_SceneEntities.end(), this), s_SceneEntities.end());
    }

    void Entity::SetParent(Entity* parent)
    {
        // Remove ourselves from old parent if changing parents
        if (m_Parent)
        {
            auto& children = m_Parent->m_Children;
            children.erase(std::remove(children.begin(), children.end(), this), children.end());
        }

        m_Parent = parent;

        // Add ourselves to new parent's child list
        if (m_Parent)
        {
            m_Parent->m_Children.push_back(this);
        }
    }

	void Entity::Initialize()
    {
        for (auto& component : components) {
            component->Initialize();
        }
    }

    void Entity::Update(float deltaTime)
    {
        if (!active) return;

        for (auto& component : components) {
            component->Update(deltaTime);
        }
    }

    void Entity::Render()
    {
        if (!active) return;

        for (auto& component : components) {
            component->Render();
        }
    }

    void Entity::OnEvent(Event& event)
    {
        if (!active) return;

        for (auto& component : components)
        {
            // Propagate events to every component
            component->OnEvent(event);

            // If a specific component marks the event handled, stop processing early
            if (event.Handled)
                break;
        }
    }

}