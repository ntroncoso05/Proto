#include "protopch.hpp"
#include "ServiceLocator.hpp"

namespace Proto {
	// Allocate the global tracking map memory segment
	std::unordered_map<std::type_index, void*> ServiceLocator::s_Services;
}