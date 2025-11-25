#include "Scene/Scene.h"

#include "Scene/ECS/Components/IDComponent.h"
#include "Scene/ECS/Components/NameComponent.h"

namespace Nova::Core::Scene {

	entt::entity Scene::CreateEntity(const std::string& name) {
		UUID uuid = GenerateUUID();
		return CreateEntity(uuid, name);
	}

	entt::entity Scene::CreateEntity(UUID id, const std::string& name) {
		entt::entity entity = m_Registry.create();
		m_Registry.emplace<ECS::Components::IDComponent>(entity, id);
		m_Registry.emplace<ECS::Components::NameComponent>(entity, name.empty() ? "Entity" : name);
		
		m_EntityMap[id] = entity;

		return entity;
	}

	void Scene::DestroyEntity(entt::entity entity) {
		for (auto it = m_EntityMap.begin(); it != m_EntityMap.end(); ++it) {
			if (it->second == entity) {
				m_EntityMap.erase(it);
				break;
			}
		}

		if (m_Registry.valid(entity))
			m_Registry.destroy(entity);
	}

	void Scene::DestroyEntity(UUID id) {
		auto it = m_EntityMap.find(id);
		if (it == m_EntityMap.end())
			return;

		entt::entity entity = it->second;

		m_EntityMap.erase(it);

		if (m_Registry.valid(entity))
			m_Registry.destroy(entity);
	}

	entt::entity Scene::GetEntityByUUID(UUID id)
	{
		auto it = m_EntityMap.find(id);
		if (it == m_EntityMap.end())
			return entt::null;

		return it->second;
	}
}