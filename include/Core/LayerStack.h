#ifndef LAYERSTACK_H
#define LAYERSTACK_H

// from https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Core/LayerStack.h

#include <vector>
#include <memory>
#include <algorithm>

#include "Core/Layer.h"

namespace Nova::Core {

    class LayerStack {
    public:
        LayerStack() = default;
        ~LayerStack();

        void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

        template<typename T, typename... Args>
        T& PushLayer(Args&&... args) {
            static_assert(std::is_base_of<Layer, T>::value, "T must be derived from Layer");
            T* layer = new T(std::forward<Args>(args)...);
            PushLayer(layer);
            layer->OnAttach();
            return *layer;
        }

        template<typename T, typename... Args>
        T& PushOverlay(Args&&... args) {
            static_assert(std::is_base_of<Layer, T>::value, "T must be derived from Layer");
            T* overlay = new T(std::forward<Args>(args)...);
            PushOverlay(overlay);
            overlay->OnAttach();
            return *overlay;
        }

        template<typename T, typename... Args>
        T& QueueLayerTransition(Layer* from, Args&&... args) {
            static_assert(std::is_base_of<Layer, T>::value, "T must be derived from Layer");
            T* to = new T(std::forward<Args>(args)...);
            m_PendingTransitions.push_back({ from, to });
            return *to;
        }

        std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

		std::vector<Layer*>::const_iterator begin() const { return m_Layers.begin(); }
		std::vector<Layer*>::const_iterator end()	const { return m_Layers.end(); }
		std::vector<Layer*>::const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
		std::vector<Layer*>::const_reverse_iterator rend() const { return m_Layers.rend(); }

        void ProcessPendingTransitions();
    
    private:
        std::vector<Layer*> m_Layers;
        unsigned int m_LayerInsertIndex = 0;

        struct PendingTransition {
            Layer* from;
            Layer* to;
        };
        std::vector<PendingTransition> m_PendingTransitions;
    };
}

#endif // LAYERSTACK_H