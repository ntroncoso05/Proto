#pragma once
#include "Component.hpp"

namespace Proto
{
    // Entity class
    class Entity {
    private:
        std::string name;
        bool active = true;
        std::vector<std::unique_ptr<Component>> components;
        std::unordered_map<size_t, Component*> componentMap;

        // --- NEW: Hierarchical Relationships ---
        Entity* m_Parent = nullptr;
        std::vector<Entity*> m_Children;
        static std::vector<Entity*> s_SceneEntities;
    public:
        explicit Entity(const std::string& entityName);
        ~Entity();

        const std::string& GetName() const { return name; }
        bool IsActive() const { return active; }
        void SetActive(bool isActive) { active = isActive; }

        void Initialize();

        void Update(float deltaTime);

        void Render();

        // --- NEW: Event Routing Cascade ---
        void OnEvent(Event& event);

        void SetParent(Entity* parent);
        Entity* GetParent() const { return m_Parent; }
        const std::vector<Entity*>& GetChildren() const { return m_Children; }

        template<typename T, typename... Args>
        T* AddComponent(Args&&... args) {
            static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");

            size_t typeID = Component::GetTypeID<T>();

            // Check if component of this type already exists
            auto it = componentMap.find(typeID);
            if (it != componentMap.end()) {
                return static_cast<T*>(it->second);
            }

            // Create new component
            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T* componentPtr = component.get();

            // --- CRITICAL ARCHITECTURE LIFE CYCLE FIX ---
            // Safely assign this Entity instance context back to the component wrapper right away!
            componentPtr->SetOwner(this);

            componentMap[typeID] = componentPtr;
            components.push_back(std::move(component));
            return componentPtr;
        }

        template<typename T>
        T* GetComponent() {
            size_t typeID = Component::GetTypeID<T>();
            auto it = componentMap.find(typeID);
            if (it != componentMap.end()) {
                return static_cast<T*>(it->second);
            }
            return nullptr;
        }

        template<typename T>
        bool RemoveComponent() {
            size_t typeID = Component::GetTypeID<T>();
            auto it = componentMap.find(typeID);
            if (it != componentMap.end()) {
                Component* componentPtr = it->second;
                componentMap.erase(it);

                for (auto compIt = components.begin(); compIt != components.end(); ++compIt) {
                    if (compIt->get() == componentPtr) {
                        components.erase(compIt);
                        return true;
                    }
                }
            }
            return false;
        }

        // --- NEW: Global Scene Tracking ---
        static const std::vector<Entity*>& GetActiveSceneEntities() { return s_SceneEntities; }
    };
}