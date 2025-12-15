#include "Scene/Scene.h"

#include "Scene/ECS/Components/IDComponent.h"
#include "Scene/ECS/Components/NameComponent.h"
#include "Scene/ECS/Components/WorldTransformComponent.h"

namespace Nova::Core::Scene {

	Scene::Scene() {
		m_Root = m_Registry.create();

		m_Registry.emplace<ECS::Components::NameComponent>(m_Root, "Root");
		m_Registry.emplace<ECS::Components::WorldTransformComponent>(m_Root);

		m_Nodes.emplace(m_Root, Node{ entt::null, {} });
	}

	void Scene::Clear() {
		m_Registry.clear();
		m_EntityMap.clear();
		m_Nodes.clear();
		m_MainCamera = entt::null;

		// Recrée la racine
		m_Root = m_Registry.create();
		m_Registry.emplace<ECS::Components::NameComponent>(m_Root, "Root");
		m_Registry.emplace<ECS::Components::WorldTransformComponent>(m_Root);
		m_Nodes.emplace(m_Root, Node{ entt::null, {} });
	}

	entt::entity Scene::CreateEntity(const std::string& name) {
		UUID uuid = GenerateUUID();
		return CreateEntity(uuid, name);
	}

	entt::entity Scene::CreateEntity(UUID id, const std::string& name) {
		entt::entity entity = m_Registry.create();
		m_Registry.emplace<ECS::Components::IDComponent>(entity, id);
		m_Registry.emplace<ECS::Components::NameComponent>(entity, name.empty() ? "Entity" : name);

		m_Registry.emplace<ECS::Components::WorldTransformComponent>(entity);
		
		m_EntityMap[id] = entity;

		EnsureNode(entity);

		return entity;
	}

	void Scene::DestroyEntity(entt::entity entity) {
		if (!IsValidEntity(entity))
			return;

		if (entity == m_Root)
			return;

		auto itNode = m_Nodes.find(entity);
		if (itNode != m_Nodes.end()) {
			auto childrenCopy = itNode->second.m_Children;
			for (auto child : childrenCopy) {
				DestroyEntity(child);
			}
		}

		DetachFromParent(entity);
		m_Nodes.erase(entity);

		if (auto* id = m_Registry.try_get<ECS::Components::IDComponent>(entity)) {
			m_EntityMap.erase(id->m_ID);
		}

		m_Registry.destroy(entity);
	}

	void Scene::DestroyEntity(UUID id) {
		entt::entity entity = GetEntityByUUID(id);
		if (entity == entt::null)
			return;
		DestroyEntity(entity);
	}

	entt::entity Scene::GetEntityByUUID(UUID id)
	{
		auto it = m_EntityMap.find(id);
		if (it == m_EntityMap.end())
			return entt::null;

		return it->second;
	}

	bool Scene::IsValidEntity(entt::entity e) const {
		return e != entt::null && m_Registry.valid(e);
	}

	void Scene::EnsureNode(entt::entity e) {
		if (m_Nodes.find(e) == m_Nodes.end()) {
			m_Nodes.emplace(e, Node{ m_Root, {} });
			m_Nodes[m_Root].m_Children.push_back(e);
		}
	}

	void Scene::DetachFromParent(entt::entity e) {
		auto it = m_Nodes.find(e);
		if (it == m_Nodes.end())
			return;

		entt::entity parent = it->second.m_Parent;
		if (parent == entt::null)
			return;

		auto pit = m_Nodes.find(parent);
		if (pit != m_Nodes.end()) {
			auto& siblings = pit->second.m_Children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), e), siblings.end());
		}

		it->second.m_Parent = entt::null;
	}

	void Scene::AttachToParent(entt::entity e, entt::entity parent) {
		EnsureNode(parent);
		auto& node = m_Nodes[e];
		node.m_Parent = parent;
		m_Nodes[parent].m_Children.push_back(e);
	}

	bool Scene::WouldCreateCycle(entt::entity child, entt::entity newParent) const {
		if (child == newParent)
			return true;

		entt::entity current = newParent;
		while (current != entt::null) {
			if (current == child)
				return true;

			auto it = m_Nodes.find(current);
			if (it == m_Nodes.end())
				break;

			current = it->second.m_Parent;
		}
		return false;
	}

	bool Scene::ParentEntity(entt::entity child, entt::entity newParent) {
		if (!IsValidEntity(child))
			return false;

		if (child == m_Root)
			return false;

		if (newParent == entt::null)
			newParent = m_Root;

		if (!IsValidEntity(newParent))
			return false;

		EnsureNode(child);
		EnsureNode(newParent);

		if (WouldCreateCycle(child, newParent))
			return false;

		// déjà parenté correctement
		if (m_Nodes[child].m_Parent == newParent)
			return true;

		DetachFromParent(child);
		AttachToParent(child, newParent);
		return true;
	}
	
	void Scene::UnparentEntity(entt::entity child) {
		(void)ParentEntity(child, m_Root);
	}

	entt::entity Scene::GetParent(entt::entity entity) const {
		auto it = m_Nodes.find(entity);
		if (it == m_Nodes.end())
			return entt::null;
		return it->second.m_Parent;
	}

	const std::vector<entt::entity>& Scene::GetChildren(entt::entity entity) const {
		static const std::vector<entt::entity> s_Empty;
		auto it = m_Nodes.find(entity);
		if (it == m_Nodes.end())
			return s_Empty;
		return it->second.m_Children;
	}
}