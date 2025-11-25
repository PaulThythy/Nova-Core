#ifndef VERTEX_H
#define VERTEX_H

#include <glm/glm.hpp>

namespace Nova::Core::Renderer {

	struct Vertex {
		glm::vec3 m_Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec2 m_TexCoord{ 0.0f, 0.0f };
		glm::vec3 m_Color{ 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Tangent{ 1.0f, 0.0f, 0.0f };
		glm::vec3 m_Bitangent{ 0.0f, 0.0f, 1.0f };
	};

} // namespace Nova::Core::Renderer

#endif // VERTEX_H