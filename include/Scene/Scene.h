#ifndef SCENE_H
#define SCENE_H

#include <entt/entt.hpp>
#include <unordered_map>
#include <string>
#include <vector>

#include "Core/UUID.h"

namespace Nova::Core::Scene {

	class Scene {
	public: 
		struct Node {
			entt::entity m_Parent{ entt::null };
			std::vector<entt::entity> m_Children;
		};

		Scene(const std::string& sceneName);
		~Scene() = default;

		entt::entity CreateEntity(const std::string& name);
		entt::entity CreateEntity(UUID id, const std::string& name);

		void DestroyEntity(entt::entity entity);
		void DestroyEntity(UUID id);

		entt::entity GetEntityByUUID(UUID id);

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }

		void SetMainCamera(entt::entity entity) { m_MainCamera = entity; }

		// tree

		entt::entity GetRootEntity() const { return m_Root; }

		bool ParentEntity(entt::entity child, entt::entity newParent);
		void UnparentEntity(entt::entity child);

		entt::entity GetParent(entt::entity entity) const;
		const std::vector<entt::entity>& GetChildren(entt::entity entity) const;

		void Clear();

		std::string GetName() { return m_Name; }

	private:
		struct EntityHash {
			std::size_t operator()(entt::entity e) const noexcept {
				return static_cast<std::size_t>(entt::to_integral(e));
			}
		};

		bool IsValidEntity(entt::entity e) const;
		bool WouldCreateCycle(entt::entity child, entt::entity newParent) const;

		void EnsureNode(entt::entity e);
		void DetachFromParent(entt::entity e);
		void AttachToParent(entt::entity e, entt::entity parent);

		std::string m_Name;

		entt::registry m_Registry;

		std::unordered_map<UUID, entt::entity> m_EntityMap;

		entt::entity m_Root{ entt::null };

		std::unordered_map<entt::entity, Node, EntityHash> m_Nodes;

		entt::entity m_MainCamera{ entt::null };
	};

} // namespace Nova::Core::Scene

#endif // SCENE_H