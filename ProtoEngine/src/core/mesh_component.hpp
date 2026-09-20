#pragma once
/*#include "component.hpp"

// Mesh component
// Manages the visual representation of an entity by handling its 3D mesh and material
class MeshComponent : public Component {
private:
    Mesh* mesh = nullptr;
    Material* material = nullptr;

public:
    MeshComponent(Mesh* m, Material* mat) : mesh(m), material(mat) {}

    void SetMesh(Mesh* m) { mesh = m; }
    void SetMaterial(Material* mat) { material = mat; }

    Mesh* GetMesh() const { return mesh; }
    Material* GetMaterial() const { return material; }

    void Render() override {
        if (!mesh || !material) return;

        // Get transform component
        auto transform = GetOwner()->GetComponent<TransformComponent>();
        if (!transform) return;

        // Render mesh with material and transform
        material->Bind();
        material->SetUniform("modelMatrix", transform->GetTransformMatrix());
        mesh->Render();
    }
};*/