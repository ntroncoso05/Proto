#include "protopch.hpp"
#include "SceneSerializer.hpp"

#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Core/ServiceLocator.hpp"
#include "Core/Scene.hpp"
#include "Renderer/RendererPipeline.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace Proto
{
    bool SceneSerializer::Serialize(const std::string& filepath)
    {
        json root;
        root["Scene"] = "ProtoScene";
        root["Entities"] = json::array();

        for (Entity* entity : Entity::GetActiveSceneEntities())
        {
            if (!entity) continue;

            json entityJson;
            entityJson["Name"] = entity->GetName();
            entityJson["Parent"] = entity->GetParent() ? entity->GetParent()->GetName() : "";

            if (auto* transform = entity->GetComponent<TransformComponent>())
            {
                const glm::vec3& pos = transform->GetPosition();
                const glm::quat& rot = transform->GetRotation();
                const glm::vec3& scl = transform->GetScale();

                entityJson["TransformComponent"] = {
                    { "Position", { pos.x, pos.y, pos.z } },
                    { "Rotation", { rot.w, rot.x, rot.y, rot.z } },
                    { "Scale",    { scl.x, scl.y, scl.z } }
                };
            }

            if (auto* cam = entity->GetComponent<CameraComponent>())
            {
                entityJson["CameraComponent"] = {
                    { "FOV",              cam->GetFOV()},
                    { "MovementSpeed",    cam->MovementSpeed },
                    { "MouseSensitivity", cam->MouseSensitivity }
                };
            }

            if (auto* model = entity->GetComponent<ModelComponent>())
            {
                if (model->GetModelHandle().IsValid())
                {
                    entityJson["ModelComponent"] = {
                        { "AssetId", model->GetModelHandle().GetId() }
                    };
                }
            }

            if (auto* light = entity->GetComponent<LightComponent>()) {
                entityJson["LightComponent"] = {
                    { "Type",           static_cast<int>(light->Type) },
                    { "Color",{ light->Color.r, light->Color.g, light->Color.b } },
                    { "Intensity",      light->Intensity },
                    { "Radius",         light->Radius },
                    { "InnerConeAngle", light->InnerConeAngle },
                    { "OuterConeAngle", light->OuterConeAngle },
                    { "CastShadows",    light->CastShadows }
                };
            }

            if (auto* anim = entity->GetComponent<AnimationComponent>()) {
                entityJson["AnimationComponent"] = {
                    { "ActiveClipIndex", anim->ActiveClipIndex },
                    { "CurrentTime",     anim->CurrentTime },
                    { "PlaybackSpeed",   anim->PlaybackSpeed },
                    { "IsPlaying",       anim->IsPlaying },
                    { "Loop",            anim->Loop }
                };
            }

            if (auto* rb = entity->GetComponent<RigidBodyComponent>()) {
                entityJson["RigidBodyComponent"] = {
                    { "MotionType",     static_cast<int>(rb->MotionType) },
                    { "Mass",           rb->Mass },
                    { "Friction",       rb->Friction },
                    { "Restitution",    rb->Restitution },
                    { "LinearDamping",  rb->LinearDamping },
                    { "AngularDamping", rb->AngularDamping },
                    { "UseGravity",     rb->UseGravity }
                };
            }
            if (auto* box = entity->GetComponent<BoxColliderComponent>()) {
                entityJson["BoxColliderComponent"] = {
                    { "HalfExtents",{ box->HalfExtents.x, box->HalfExtents.y, box->HalfExtents.z } },
                    { "Offset",{ box->Offset.x, box->Offset.y, box->Offset.z } },
                    { "IsTrigger",   box->IsTrigger }
                };
            }
            
            if (auto* pe = entity->GetComponent<ParticleEmitterComponent>()) {
                entityJson["ParticleEmitterComponent"] = {
                    { "PeOffset",     { pe->Offset.x, pe->Offset.y, pe->Offset.z } },
                    { "Direction",  { pe->Direction.x, pe->Direction.y, pe->Direction.z } },
                    { "Color",      { pe->Color.r, pe->Color.g, pe->Color.b } },
                    { "HDRIntensity",    pe->HDRIntensity },
                    { "ParticleSize",  pe->ParticleSize },
                    { "InitialSpeed", pe->InitialSpeed },
                    { "Spread",     pe->Spread },
                    { "Gravity", pe->Gravity },
                    { "MinLife", pe->MinLife },
                    { "MaxLife", pe->MaxLife },
                    { "ActiveParticles", pe->ActiveParticles },
                    { "Enabled", pe->Enabled },
                };
            }

            if (auto* ovComp = entity->GetComponent<MaterialOverrideComponent>(); ovComp && !ovComp->Overrides.empty()) {
                nlohmann::json ovArray = nlohmann::json::array();
                for (const auto& [matIdx, ov] : ovComp->Overrides) {
                    if (!ov.Enabled) continue;
                    ovArray.push_back({
                        { "MaterialIndex",      matIdx },
                        { "Enabled", ov.Enabled },
                        { "BaseColorFactor", { ov.BaseColorFactor.r, ov.BaseColorFactor.g, ov.BaseColorFactor.b, ov.BaseColorFactor.a } },
                        { "MetallicFactor",     ov.MetallicFactor },
                        { "RoughnessFactor",    ov.RoughnessFactor },
                        { "ClearcoatFactor",    ov.ClearcoatFactor },
                        { "ClearcoatRoughness", ov.ClearcoatRoughness },
                        { "IridescenceFactor",  ov.IridescenceFactor },
                        { "EmissiveFactor",{ ov.EmissiveFactor.r, ov.EmissiveFactor.g, ov.EmissiveFactor.b } }
                        });
                }
                if (!ovArray.empty()) {
                    entityJson["MaterialOverrideComponent"] = ovArray;
                }
            }

            root["Entities"].push_back(entityJson);
        }

        if (ServiceLocator::Contains<SceneRenderer>()) {
            auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
            root["Environment"] = {
                { "HDRIPath",           pbrRenderer.m_ActiveHDRIPath },
                { "IBLIntensity",       pbrRenderer.m_IBLIntensity },
                { "EmissiveStrength",   pbrRenderer.m_EmissiveStrength },
                { "BloomIntensity",     pbrRenderer.m_BloomIntensity },
                { "SSRIntensity",       pbrRenderer.m_SSRIntensity },
                { "SSRMaxDistance",     pbrRenderer.m_SSRMaxDistance },
                { "SSRThickness",       pbrRenderer.m_SSRThickness },
                { "FogColor",           { pbrRenderer.m_FogColor.r, pbrRenderer.m_FogColor.g, pbrRenderer.m_FogColor.b } },
                { "FogDensity",         pbrRenderer.m_FogDensity },
                { "FogSunScatter",      pbrRenderer.m_FogSunScatter },
                { "FogHeightFalloff",   pbrRenderer.m_FogHeightFalloff },
                { "FogBaseHeight",      pbrRenderer.m_FogBaseHeight },
                { "FogMaxDistance",     pbrRenderer.m_FogMaxDistance },
            };
        }

        if (filepath.find("_temp_play_state") == std::string::npos) {
            s_ActiveScenePath = filepath;
        }

        std::ofstream outFile(filepath);
        if (!outFile.is_open()) return false;

        outFile << root.dump(4);
        return true;
    }

    bool SceneSerializer::Deserialize(const std::string& filepath)
    {
        std::ifstream inFile(filepath);
        if (!inFile.is_open()) return false;
        json root;
        inFile >> root;

        if (filepath.find("_temp_play_state") == std::string::npos) {
            std::unordered_set<std::string> incomingNames;
            for (const auto& ej : root["Entities"]) {
                incomingNames.insert(ej.value("Name", ""));
            }

            auto& scene = ServiceLocator::Get<Scene>();
            for (const auto& ent : scene.GetEntities()) {
                if (ent && !ent->GetComponent<CameraComponent>() && !incomingNames.contains(ent->GetName())) {
                    scene.DestroyEntity(ent.get());
                }
            }
            s_ActiveScenePath = filepath;
        }

        if (!root.contains("Entities")) return false;

        auto& resourceManager = ServiceLocator::Get<ResourceManager>();
        std::unordered_map<std::string, Entity*> entityMap;
        std::vector<std::pair<Entity*, std::string>> pendingParents;

        // Update existing entities by name (or create new ones) and restore components
        for (const auto& entityJson : root["Entities"])
        {
            std::string name = entityJson.value("Name", "Entity");
            Entity* targetEntity = nullptr;

            for (Entity* existing : Entity::GetActiveSceneEntities())
            {
                if (existing && existing->GetName() == name)
                {
                    targetEntity = existing;
                    break;
                }
            }

            if (!targetEntity)
            {
                targetEntity = ServiceLocator::Get<Scene>().CreateEntity(name);
                targetEntity->Initialize();
            }

            entityMap[name] = targetEntity;

            std::string parentName = entityJson.value("Parent", "");
            if (!parentName.empty())
            {
                pendingParents.emplace_back(targetEntity, parentName);
            }

            if (entityJson.contains("TransformComponent"))
            {
                const auto& t = entityJson["TransformComponent"];
                auto* transform = targetEntity->GetComponent<TransformComponent>();
                if (!transform) transform = targetEntity->AddComponent<TransformComponent>();

                transform->SetPosition({ t["Position"][0], t["Position"][1], t["Position"][2] });
                transform->SetRotation(glm::quat(t["Rotation"][0], t["Rotation"][1], t["Rotation"][2], t["Rotation"][3]));
                transform->SetScale({ t["Scale"][0], t["Scale"][1], t["Scale"][2] });
            }

            if (entityJson.contains("CameraComponent"))
            {
                const auto& c = entityJson["CameraComponent"];
                auto* cam = targetEntity->GetComponent<CameraComponent>();
                if (!cam) cam = targetEntity->AddComponent<CameraComponent>();

                cam->SetFOV(c.value("FOV", 45.0f));
                cam->MovementSpeed = c.value("MovementSpeed", 5.0f);
                cam->MouseSensitivity = c.value("MouseSensitivity", 0.1f);
            }

            if (entityJson.contains("ModelComponent"))
            {
                std::string assetId = entityJson["ModelComponent"].value("AssetId", "");
                if (!assetId.empty() && !targetEntity->GetComponent<ModelComponent>())
                {
                    if (assetId == "procedural_ground_plane" && !resourceManager.GetResource<Model>(assetId)) {
                        if (auto gm = Model::CreateGroundPlane(assetId, 25.0f, 10.0f)) {
                            resourceManager.RegisterExternal<Model>(assetId, gm);
                        }
                    }

                    auto handle = resourceManager.Load<Model>(assetId);
                    if (handle.IsValid()) targetEntity->AddComponent<ModelComponent>(handle);
                }
            }

            if (entityJson.contains("LightComponent")) {
                const auto& lj = entityJson["LightComponent"];
                auto* light = targetEntity->GetComponent<LightComponent>();
                if (!light) light = targetEntity->AddComponent<LightComponent>();

                light->Type = static_cast<LightType>(lj.value("Type", 0));
                light->Intensity = lj.value("Intensity", 3.0f);
                light->Radius = lj.value("Radius", 15.0f);
                light->InnerConeAngle = lj.value("InnerConeAngle", 15.0f);
                light->OuterConeAngle = lj.value("OuterConeAngle", 30.0f);
                light->CastShadows = lj.value("CastShadows", true);
                if (lj.contains("Color") && lj["Color"].size() == 3) {
                    light->Color = glm::vec3(
                        lj["Color"][0].get<float>(),
                        lj["Color"][1].get<float>(),
                        lj["Color"][2].get<float>()
                    );
                }
                light->Initialize();
            }
            else if (targetEntity->GetComponent<LightComponent>()) {
                targetEntity->RemoveComponent<LightComponent>();
            }

            if (entityJson.contains("AnimationComponent")) {
                const auto& aj = entityJson["AnimationComponent"];
                auto* anim = targetEntity->GetComponent<AnimationComponent>();
                if (!anim) anim = targetEntity->AddComponent<AnimationComponent>();

                anim->ActiveClipIndex = aj.value("ActiveClipIndex", 0u);
                anim->CurrentTime = aj.value("CurrentTime", 0.0f);
                anim->PlaybackSpeed = aj.value("PlaybackSpeed", 1.0f);
                anim->IsPlaying = aj.value("IsPlaying", true);
                anim->Loop = aj.value("Loop", true);

                anim->Initialize();
            }
            else if (targetEntity->GetComponent<AnimationComponent>()) {
                targetEntity->RemoveComponent<AnimationComponent>();
            }

            if (entityJson.contains("RigidBodyComponent")) {
                const auto& rbj = entityJson["RigidBodyComponent"];
                auto* rb = targetEntity->GetComponent<RigidBodyComponent>();
                if (!rb) rb = targetEntity->AddComponent<RigidBodyComponent>();
                
                rb->MotionType      = static_cast<BodyMotionType>(rbj.value("MotionType", 0u));
                rb->Mass            = rbj.value("Mass", 1.0f);
                rb->Friction        = rbj.value("Friction", 0.5f);
                rb->Restitution     = rbj.value("Restitution", 0.25f);
                rb->LinearDamping   = rbj.value("LinearDamping", 0.05f);
                rb->AngularDamping  = rbj.value("AngularDamping", 0.05f);
                rb->UseGravity      = rbj.value("UseGravity", true);

                rb->Initialize();
            }
            else if (targetEntity->GetComponent<RigidBodyComponent>()) {
                targetEntity->RemoveComponent<RigidBodyComponent>();
            }

            if (entityJson.contains("BoxColliderComponent")) {
                const auto& bcj = entityJson["BoxColliderComponent"];
                auto* bc = targetEntity->GetComponent<BoxColliderComponent>();
                if (!bc) bc = targetEntity->AddComponent<BoxColliderComponent>();

                if (bcj.contains("HalfExtents") && bcj["HalfExtents"].size() == 3) {
                    bc->HalfExtents = glm::vec3(
                        bcj["HalfExtents"][0].get<float>(),
                        bcj["HalfExtents"][1].get<float>(),
                        bcj["HalfExtents"][2].get<float>()
                    );
                }                
                if (bcj.contains("Offset") && bcj["Offset"].size() == 3) {
                    bc->Offset = glm::vec3(
                        bcj["Offset"][0].get<float>(),
                        bcj["Offset"][1].get<float>(),
                        bcj["Offset"][2].get<float>()
                    );
                }
                bc->IsTrigger   = bcj.value("IsTrigger", false);

                bc->Initialize();
            }
            else if (targetEntity->GetComponent<BoxColliderComponent>()) {
                targetEntity->RemoveComponent<BoxColliderComponent>();
            }

            if (entityJson.contains("ParticleEmitterComponent")) {
                const auto& pej = entityJson["ParticleEmitterComponent"];
                auto* pe = targetEntity->GetComponent<ParticleEmitterComponent>();
                if (!pe) pe = targetEntity->AddComponent<ParticleEmitterComponent>();

                if (pej.contains("PeOffset") && pej["PeOffset"].size() == 3) {
                    pe->Offset = glm::vec3(
                        pej["PeOffset"][0].get<float>(),
                        pej["PeOffset"][1].get<float>(),
                        pej["PeOffset"][2].get<float>()
                    );
                }
                if (pej.contains("Direction") && pej["Direction"].size() == 3) {
                    pe->Direction = glm::vec3(
                        pej["Direction"][0].get<float>(),
                        pej["Direction"][1].get<float>(),
                        pej["Direction"][2].get<float>()
                    );
                }
                if (pej.contains("Color") && pej["Color"].size() == 3) {
                    pe->Color = glm::vec3(
                        pej["Color"][0].get<float>(),
                        pej["Color"][1].get<float>(),
                        pej["Color"][2].get<float>()
                    );
                }
                pe->HDRIntensity = pej.value("HDRIntensity", 4.0f);
                pe->ParticleSize = pej.value("ParticleSize", 0.16f);
                pe->InitialSpeed = pej.value("InitialSpeed", 3.5f);
                pe->Spread = pej.value("Spread", 0.45f);
                pe->Gravity = pej.value("Gravity", -6.5f);
                pe->MinLife = pej.value("MinLife", 0.8f);
                pe->MaxLife = pej.value("MaxLife", 1.8f);
                pe->ActiveParticles = pej.value("ActiveParticles", 2048);
                pe->Enabled = pej.value("Enabled", true);

                pe->Initialize();
            }
            else if (targetEntity->GetComponent<ParticleEmitterComponent>()) {
                targetEntity->RemoveComponent<ParticleEmitterComponent>();
            }
            
            if (entityJson.contains("MaterialOverrideComponent") && entityJson["MaterialOverrideComponent"].is_array()) {
                auto* mo = targetEntity->GetComponent<MaterialOverrideComponent>();
                if (!mo) mo = targetEntity->AddComponent<MaterialOverrideComponent>();
                mo->Overrides.clear();

                for (const auto& item : entityJson["MaterialOverrideComponent"]) {
                    uint32_t matIdx = item.value("MaterialIndex", 0u);
                    MaterialOverride ov{};
                    ov.Enabled = item.value("Enabled", true);

                    if (item.contains("BaseColorFactor") && item["BaseColorFactor"].size() == 4) {
                        ov.BaseColorFactor = glm::vec4(
                            item["BaseColorFactor"][0].get<float>(),
                            item["BaseColorFactor"][1].get<float>(),
                            item["BaseColorFactor"][2].get<float>(),
                            item["BaseColorFactor"][3].get<float>()
                        );
                    }

                    ov.MetallicFactor = item.value("MetallicFactor", 0.0f);
                    ov.RoughnessFactor = item.value("RoughnessFactor", 0.5f);
                    ov.ClearcoatFactor = item.value("ClearcoatFactor", 0.0f);
                    ov.ClearcoatRoughness = item.value("ClearcoatRoughness", 0.0f);
                    ov.IridescenceFactor = item.value("IridescenceFactor", 0.0f);

                    if (item.contains("EmissiveFactor") && item["EmissiveFactor"].size() == 3) {
                        ov.EmissiveFactor = glm::vec3(
                            item["EmissiveFactor"][0].get<float>(),
                            item["EmissiveFactor"][1].get<float>(),
                            item["EmissiveFactor"][2].get<float>()
                        );
                    }

                    mo->Overrides[matIdx] = ov;
                }
                mo->Initialize();
            }
            else if (targetEntity->GetComponent<MaterialOverrideComponent>()) {
                targetEntity->RemoveComponent<MaterialOverrideComponent>();
            }
        }

        // Reconnect parent-child hierarchy (e.g. Moon_Child -> Planet_Root)
        for (auto& [child, parentName] : pendingParents)
        {
            if (auto it = entityMap.find(parentName); it != entityMap.end())
            {
                child->SetParent(it->second);
            }
        }

        if (root.contains("Environment") && root["Environment"].is_object() && ServiceLocator::Contains<SceneRenderer>()) {
            const auto& env = root["Environment"];
            auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();

            pbrRenderer.m_IBLIntensity = env.value("IBLIntensity", 0.25f);
            pbrRenderer.m_EmissiveStrength = env.value("EmissiveStrength", 1.0f);
            pbrRenderer.m_BloomIntensity = env.value("BloomIntensity", 0.15f);
            pbrRenderer.m_SSRIntensity = env.value("SSRIntensity", 0.85f);
            pbrRenderer.m_SSRMaxDistance = env.value("SSRMaxDistance", 25.0f);
            pbrRenderer.m_SSRThickness = env.value("SSRThickness", 0.22f);

            if (env.contains("FogColor") && env["FogColor"].size() == 3) {
                pbrRenderer.m_FogColor = glm::vec3(
                    env["FogColor"][0].get<float>(),
                    env["FogColor"][1].get<float>(),
                    env["FogColor"][2].get<float>()
                );
            }
            pbrRenderer.m_FogDensity = env.value("FogDensity", 0.015f);
            pbrRenderer.m_FogSunScatter = env.value("FogSunScatter", 1.8f);
            pbrRenderer.m_FogHeightFalloff = env.value("FogHeightFalloff", 0.18f);
            pbrRenderer.m_FogBaseHeight = env.value("FogBaseHeight", -1.0f);
            pbrRenderer.m_FogMaxDistance = env.value("FogMaxDistance", 65.0f);

            std::string savedHDRI = env.value("HDRIPath", std::string(""));
            if (savedHDRI != pbrRenderer.m_ActiveHDRIPath) {
                pbrRenderer.SetEnvironmentHDRI(savedHDRI);
            }
        }
        return true;
    }
}