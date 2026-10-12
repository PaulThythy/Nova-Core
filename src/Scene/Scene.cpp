#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "ECS/Components/IDComponent.h"
#include "ECS/Components/NameComponent.h"
#include "ECS/Components/TransformComponent.h"

namespace Nova::Core::Scene {

	static void DecomposeMatrixToTRS(
		const glm::mat4& matrix,
		glm::vec3& outTranslation,
		glm::quat& outRotation,
		glm::vec3& outScale)
	{
		outTranslation = glm::vec3(matrix[3]);

		glm::vec3 column0 = glm::vec3(matrix[0]);
		glm::vec3 column1 = glm::vec3(matrix[1]);
		glm::vec3 column2 = glm::vec3(matrix[2]);

		outScale = glm::vec3(
			glm::length(column0),
			glm::length(column1),
			glm::length(column2));

		if (glm::determinant(glm::mat3(matrix)) < 0.0f)
			outScale.x = -outScale.x;

		if (std::abs(outScale.x) > 1e-8f) column0 /= outScale.x;
		if (std::abs(outScale.y) > 1e-8f) column1 /= outScale.y;
		if (std::abs(outScale.z) > 1e-8f) column2 /= outScale.z;

		outRotation = glm::normalize(glm::quat_cast(glm::mat3(column0, column1, column2)));
	}

	Scene::Scene(const std::string& sceneName) {
		m_Name = sceneName;

		m_Root = m_Registry.create();

		m_Registry.emplace<ECS::Components::NameComponent>(m_Root, m_Name);

		m_Nodes.emplace(m_Root, Node{ entt::null, {} });
	}

	void Scene::Clear() {
		m_Registry.clear();
		m_EntityMap.clear();
		m_Nodes.clear();
		m_MainCamera = entt::null;

		// Recreate the root entity.
		//m_Root = m_Registry.create();
		//m_Registry.emplace<ECS::Components::NameComponent>(m_Root, "Root");
		//m_Nodes.emplace(m_Root, Node{ entt::null, {} });
	}

	entt::entity Scene::CreateEntity(const std::string& name) {
		UUID uuid = GenerateUUID();
		return CreateEntity(uuid, name);
	}

	entt::entity Scene::CreateEntity(UUID id, const std::string& name) {
		entt::entity entity = m_Registry.create();
		m_Registry.emplace<ECS::Components::IDComponent>(entity, id);
		m_Registry.emplace<ECS::Components::NameComponent>(entity, name.empty() ? "Entity" : name);

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

		// Already parented correctly.
		if (m_Nodes[child].m_Parent == newParent)
			return true;

		// Keep world pose stable across reparent (children keep their local offsets).
		const bool hasTransform = m_Registry.all_of<ECS::Components::TransformComponent>(child);
		const glm::mat4 world = hasTransform ? GetWorldTransform(child) : glm::mat4(1.0f);

		DetachFromParent(child);
		AttachToParent(child, newParent);

		if (hasTransform)
			SetWorldTransform(child, world);

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

	glm::mat4 Scene::GetWorldTransform(entt::entity entity) const {
		if (!IsValidEntity(entity) || entity == m_Root)
			return glm::mat4(1.0f);

		std::vector<entt::entity> chain;
		for (entt::entity e = entity; e != entt::null && e != m_Root; e = GetParent(e))
			chain.push_back(e);

		glm::mat4 world{ 1.0f };
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			if (const auto* tc = m_Registry.try_get<ECS::Components::TransformComponent>(*it))
				world *= tc->GetTransform();
		}
		return world;
	}

	void Scene::SetWorldTransform(entt::entity entity, const glm::mat4& worldMatrix) {
		auto* tc = m_Registry.try_get<ECS::Components::TransformComponent>(entity);
		if (!tc)
			return;

		const entt::entity parent = GetParent(entity);
		const glm::mat4 parentWorld = (parent != entt::null) ? GetWorldTransform(parent) : glm::mat4(1.0f);
		const glm::mat4 local = glm::inverse(parentWorld) * worldMatrix;

		glm::vec3 translation{};
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f };
		DecomposeMatrixToTRS(local, translation, rotation, scale);

		tc->m_Translation = translation;
		tc->m_Rotation = glm::eulerAngles(rotation);
		tc->m_Scale = scale;
	}

} // namespace Nova::Core::Scene