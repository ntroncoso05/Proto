#pragma once
#include <chrono>
#include <string>

// Forward declarations
class Entity;

// Base component class
class Component {
protected:
    Entity* owner = nullptr;

public:
    virtual ~Component() = default;

    virtual void Initialize() {}
    virtual void Update(float deltaTime) {}
    virtual void Render() {}

    void SetOwner(Entity* entity) { owner = entity; }
    Entity* GetOwner() const { return owner; }
};