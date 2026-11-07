#pragma once

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <stdexcept>
//#include <assert.h>

namespace Proto {

	class ServiceLocator
	{
	public:
		// Register a service instance by interface type
		template<typename T>
		static void Register(T* service)
		{
			auto typeIdx = std::type_index(typeid(T));
			s_Services[typeIdx] = static_cast<void*>(service);
		}

		// Resolve and fetch an active service interface safely
		template<typename T>
		static T& Get()
		{
			auto typeIdx = std::type_index(typeid(T));
			auto it = s_Services.find(typeIdx);

#ifdef PROTO_DEBUG
			if (it == s_Services.end()) {
				throw std::runtime_error("Service locator failed to resolve requested service interface type!");
			}
#endif

			assert(it != s_Services.end());
			return *static_cast<T*>(it->second);
		}

		// Checks if a service layout interface type has been fully registered in the locator maps
		template<typename T>
		static bool Contains()
		{
			auto typeIdx = std::type_index(typeid(T));
			return s_Services.find(typeIdx) != s_Services.end();
		}

		// Clear out all active lookup pointers during system shutdown
		static void Shutdown()
		{
			s_Services.clear();
		}

	private:
		static std::unordered_map<std::type_index, void*> s_Services;
	};
}