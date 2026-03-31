#ifndef MESHRENDERERCOMPONENT_H
#define MESHRENDERERCOMPONENT_H

#include <memory>

#include "Api.h"
#include "Asset/Assets/MeshAsset.h"
#include "Renderer/Graphics/Material.h"

namespace Nova::Core::Scene::ECS::Components {

    struct NV_API MeshRendererComponent {
        std::shared_ptr<Asset::Assets::MeshAsset> m_MeshAsset;
        Renderer::Graphics::Material m_Material{};

        MeshRendererComponent() = default;

        explicit MeshRendererComponent(const std::shared_ptr<Asset::Assets::MeshAsset>& meshAsset)
            : m_MeshAsset(meshAsset) {}

        MeshRendererComponent(const std::shared_ptr<Asset::Assets::MeshAsset>& meshAsset,
                              const Renderer::Graphics::Material& material)
            : m_MeshAsset(meshAsset), m_Material(material) {}
    };

} // namespace Nova::Core::Scene::ECS::Components

#endif // MESHRENDERERCOMPONENT_H
