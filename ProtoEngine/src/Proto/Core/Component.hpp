#pragma once
#include "Events/Event.hpp"

namespace Proto
{
    // Forward declarations
    class Entity;

    // Component type ID system
    class ComponentTypeIDSystem {
    private:
        static size_t nextTypeID;

    public:
        template<typename T>
        static size_t GetTypeID() {
            static size_t typeID = nextTypeID++;
            return typeID;
        }
    };

    // Base component class
    class Component {
    public:
        enum class State {
            Uninitialized,
            Initializing,
            Active,
            Destroying,
            Destroyed
        };

    private:
        State state = State::Uninitialized;
        Entity* owner = nullptr;

    public:
        virtual ~Component();

        template<typename T>
        static size_t GetTypeID() {  return ComponentTypeIDSystem::GetTypeID<T>(); }

        void Initialize();

        void Destroy();

        bool IsActive() const { return state == State::Active; }

        void SetOwner(Entity* entity) { owner = entity; }
        Entity* GetOwner() const { return owner; }

        // --- NEW: Event Processing Entrypoint ---
        virtual void OnEvent(Event& event) {}

    protected:
        virtual void OnInitialize() {}
        virtual void OnDestroy() {}
        virtual void Update(float deltaTime) {}
        virtual void Render() {}

        friend class Entity; // Allow Entity to call protected methods
    };
    
}