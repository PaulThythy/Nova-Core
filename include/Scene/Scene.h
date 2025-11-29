#ifndef SCENE_H
#define SCENE_H

#include <entt/entt.hpp>
#include <unordered_map>
#include <string>

#include "Core/UUID.h"

namespace Nova::Core::Scene {

	class Scene {
	public: 
		Scene() = default;
		~Scene() = default;

		entt::entity CreateEntity(const std::string& name);
		entt::entity CreateEntity(UUID id, const std::string& name);

		void DestroyEntity(entt::entity entity);
		void DestroyEntity(UUID id);

		entt::entity GetEntityByUUID(UUID id);

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }

		void SetMainCamera(entt::entity entity) { m_MainCamera = entity; }

	private:
		entt::registry m_Registry;

		std::unordered_map<UUID, entt::entity> m_EntityMap;

		entt::entity m_MainCamera{ entt::null };
	};

} // namespace Nova::Core::Scene

#endif // SCENE_H