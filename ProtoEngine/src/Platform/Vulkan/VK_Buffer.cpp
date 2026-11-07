#include "VK_Buffer.hpp"

namespace Proto
{
	const std::vector<Vertex> Vertex::GenerateDefaultVertexData()
	{
		// Review Noraml data
		return {
			{ {-0.5f, -0.5f,  0.0f}, {1.0f, 0.0f, 0.0f}, { 1.0f, 0.0f, 0.0f }, {0.0f, 0.0f} },
			{ { 0.5f, -0.5f,  0.0f}, {0.0f, 1.0f, 0.0f}, { 0.0f, 1.0f, 0.0f }, {1.0f, 0.0f} },
			{ { 0.5f,  0.5f,  0.0f}, {0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f, 1.0f }, {1.0f, 1.0f} },
			{ {-0.5f,  0.5f,  0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f} },

			{ {-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, { 1.0f, 0.0f, 0.0f }, {0.0f, 0.0f} },
			{ { 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, { 0.0f, 1.0f, 0.0f }, {1.0f, 0.0f} },
			{ { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f, 1.0f }, {1.0f, 1.0f} },
			{ {-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f} } };
	}

	const std::vector<uint16_t> Index::GenerateDefaultIndexData()
	{
		return {
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4 };
	}
}