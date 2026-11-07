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

    // Specific event types
    //class CollisionEvent : public Event {
    //private:
    //    Entity* entity1;
    //    Entity* entity2;
    //
    //public:
    //    CollisionEvent(Entity* e1, Entity* e2) : entity1(e1), entity2(e2) {}
    //
    //    Entity* GetEntity1() const { return entity1; }
    //    Entity* GetEntity2() const { return entity2; }
    //};
    //
    //// Component that listens for events
    //// Handles physics-related behavior and responds to collision events through the event system
    //class PhysicsComponent : public Component, public EventListener {
    //public:
    //    void Initialize() override {
    //        // Register as event listener
    //        GetEventSystem().AddListener(this);
    //    }
    //
    //    ~PhysicsComponent() override {
    //        // Unregister as event listener
    //        GetEventSystem().RemoveListener(this);
    //    }
    //
    //    void OnEvent(const Event& event) override {
    //        if (auto collisionEvent = dynamic_cast<const CollisionEvent*>(&event)) {
    //            // Handle collision event
    //        }
    //    }
    //
    //private:
    //    EventSystem& GetEventSystem() {
    //        // Get event system from somewhere (e.g., service locator)
    //        static EventSystem eventSystem;
    //        return eventSystem;
    //    }
    //};
    
}