#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace Proto
{
    // Resource base class
    class Resource {
    private:
        std::string resourceId;     // Unique identifier for this resource within the system

    public:
        bool loaded = false;        // Loading state flag for resource lifecycle management
        explicit Resource(const std::string& id) : resourceId(id) {}
        virtual ~Resource() = default;

        // Core resource identity and state access methods
        const std::string& GetId() const { return resourceId; }
        bool IsLoaded() const { return loaded; }

        // Virtual interface for resource-specific loading and unloading behavior
        bool Load() {
            if (typeid(*this) == typeid(Resource)) {
                return false;
            }
            if (loaded) return true; // Prevent double loading
            loaded = doLoad();       // The flag gets set strictly here!
            return loaded;
        }

        void Unload() {
            if (!loaded) return;
            doUnload();              // Let the derived class clean up its specific resources
            loaded = false;          // The flag gets cleared strictly here
        }
    protected:
        virtual bool doLoad() = 0;
        virtual void doUnload() = 0;
    };

    class ResourceManager;

    // Resource handle
    template<typename T>
    class ResourceHandle {
    private:
        std::string resourceId;
        ResourceManager* resourceManager;

    public:
        ResourceHandle() : resourceManager(nullptr) {}

        ResourceHandle(const std::string& id, ResourceManager* manager)
            : resourceId(id), resourceManager(manager) {}

        T* Get() const;
        bool IsValid() const;

        const std::string& GetId() const {
            return resourceId;
        }

        // Convenience operators
        T* operator->() const {
            return Get();
        }

        T& operator*() const {
            return *Get();
        }

        operator bool() const {
            return IsValid();
        }
    };

    // Resource manager
    class ResourceManager {
    public:
        // Two-level storage system: organize by type first, then by unique identifier. This approach enables type-safe resource access while maintaining efficient lookup
        std::unordered_map<std::type_index,
            std::unordered_map<std::string, std::shared_ptr<Resource>>> resources;

        // Two-level reference counting system for automatic resource lifecycle management. First level maps resource type, second level maps resource IDs to their data
        struct ResourceData {
            std::shared_ptr<Resource> resource;  // The actual resource
            int refCount;                        // Reference count for this resource
        };
        std::unordered_map<std::type_index,
            std::unordered_map<std::string, ResourceData>> refCounts;

        template<typename T>
        ResourceHandle<T> Load(const std::string& resourceId)
        {
            static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

            auto typeIdx = std::type_index(typeid(T));
            auto& typeResources = resources[typeIdx];
            auto it = typeResources.find(resourceId);

            if (it != typeResources.end()) {
                refCounts[typeIdx][resourceId].refCount++;
                return ResourceHandle<T>(resourceId, this);
            }

            std::shared_ptr<T> resource = std::make_shared<T>(resourceId);

            // DEVIRTUALIZATION FIX:
            // Cast explicitly to the derived class T and call doLoad directly. This completely bypasses the broken vtable lookup routine.
            T* concretePtr = resource.get();
            if (!concretePtr->T::doLoad()) {
                return ResourceHandle<T>();
            }

            // Explicitly update the base class state flag since we bypassed Resource::Load(). This allows Resource::IsLoaded() to return the correct state.
            struct ResourceAccessor : public Resource {
                static void MarkLoaded(Resource* r) { r->loaded = true; }
            };
            ResourceAccessor::MarkLoaded(concretePtr);

            typeResources[resourceId] = resource;
            refCounts[typeIdx][resourceId] = ResourceData{ resource, 1 };

            return ResourceHandle<T>(resourceId, this);
        }

        template<typename T>
        T* GetResource(const std::string& resourceId) {
            // Access type-specific resource container using compile-time type information
            auto& typeResources = resources[std::type_index(typeid(T))];
            auto it = typeResources.find(resourceId);

            if (it != typeResources.end()) {
                // Resource found - perform safe downcast and return typed pointer
                return static_cast<T*>(it->second.get());
            }

            // Resource not found - return null for safe handling by caller
            return nullptr;
        }

        template<typename T>
        bool HasResource(const std::string& resourceId) {
            // Efficient existence check without resource access overhead
            auto typeIt = resources.find(std::type_index(typeid(T)));
            if (typeIt == resources.end()) return false;
            return typeIt->second.find(resourceId) != typeIt->second.end();
        }

        template <typename T>
        void Release(const std::string& resourceId)
        {
            static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

            // Look up using the compiled type index first!
            auto typeIdx = std::type_index(typeid(T));
            auto typeRefCountsIt = refCounts.find(typeIdx);

            if (typeRefCountsIt != refCounts.end()) {
                // Access the inner string-keyed map for this resource type
                auto& typeMap = typeRefCountsIt->second;
                auto it = typeMap.find(resourceId);

                if (it != typeMap.end()) {
                    // Decrement the active reference track metric safely
                    it->second.refCount--;

                    // If no objects are tracking this resource, unload it from GPU memory
                    if (it->second.refCount <= 0) {

                        // Clear from active tracking caches cleanly
                        auto typeResourcesIt = resources.find(typeIdx);
                        if (typeResourcesIt != resources.end()) {
                            auto& typeResources = typeResourcesIt->second;
                            auto resourceIt = typeResources.find(resourceId);

                            if (resourceIt != typeResources.end()) {
                                // Execute the concrete virtual hardware cleanup commands
                                resourceIt->second->Unload();
                                typeResources.erase(resourceIt);
                            }
                        }

                        // Remove tracking metrics from reference loop entirely
                        typeMap.erase(it);
                    }
                }
            }
        }

        void UnloadAll() {
            // Emergency cleanup method for system shutdown or major state changes
            for (auto& [type, typeResources] : resources) {
                for (auto& [id, resource] : typeResources) {
                    resource->Unload();     // Ensure all resources clean up properly
                }
                typeResources.clear();      // Clear type-specific containers
            }
            refCounts.clear();              // Reset all reference counts
        }

        template<typename T>
        ResourceHandle<T> RegisterExternal(const std::string& resourceId, std::shared_ptr<T> resource)
        {
            static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

            auto typeIdx = std::type_index(typeid(T));
            auto& typeResources = resources[typeIdx];
            auto it = typeResources.find(resourceId);

            if (it != typeResources.end()) {
                refCounts[typeIdx][resourceId].refCount++;
                return ResourceHandle<T>(resourceId, this);
            }

            struct ResourceAccessor : public Resource {
                static void MarkLoaded(Resource* r) { r->loaded = true; }
            };
            ResourceAccessor::MarkLoaded(resource.get());

            typeResources[resourceId] = resource;
            refCounts[typeIdx][resourceId] = ResourceData{ resource, 1 };

            return ResourceHandle<T>(resourceId, this);
        }

        void RegisterAssetPath(const std::string& id, const std::string& path) { assetPaths[id] = path; }
        std::string GetAssetPath(const std::string& id) const {
            auto it = assetPaths.find(id);
            return (it != assetPaths.end()) ? it->second : "";
        }
    private:
            std::unordered_map<std::string, std::string> assetPaths;
    };
    template<typename T>
    T* ResourceHandle<T>::Get() const {
        if (!resourceManager) return nullptr;
        return resourceManager->template GetResource<T>(resourceId);
    }

    template<typename T>
    bool ResourceHandle<T>::IsValid() const {
        return resourceManager && resourceManager->template HasResource<T>(resourceId);
    }
}