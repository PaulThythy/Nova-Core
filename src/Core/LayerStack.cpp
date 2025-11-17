#include "Core/LayerStack.h"

// from https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Core/LayerStack.cpp

namespace Nova::Core {

    LayerStack::~LayerStack()
	{
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
			delete layer;
		}
	}

    // PushLayer: insert a Layer before the overlay insertion point.
    // Regular layers are inserted in the stack at position m_LayerInsertIndex,
    // and the insert index is incremented so subsequent layers go after it.
	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
	}

    // PushOverlay: append an overlay to the end of the stack.
    // Overlays are rendered/updated after regular layers and are not counted in m_LayerInsertIndex.
    void LayerStack::PushOverlay(Layer* overlay)
	{
		m_Layers.emplace_back(overlay);
	}

    // PopLayer: remove a regular layer if it exists in the regular-layer region.
    // Finds the layer among [begin, begin + m_LayerInsertIndex). If found,
    // calls OnDetach(), erases it from the container, and decrements the insert index.
    void LayerStack::PopLayer(Layer* layer)
	{
		auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
		if (it != m_Layers.begin() + m_LayerInsertIndex)
		{
			layer->OnDetach();
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}

    // PopOverlay: remove an overlay if it exists in the overlay region.
    // Searches in [begin + m_LayerInsertIndex, end). If found, calls OnDetach() and erases it.
	void LayerStack::PopOverlay(Layer* overlay)
	{
		auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);
		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}

	// Process any queued transitions: for each pending {from, to}, find 'from' in m_Layers,
    // if found detach+delete it, replace pointer with 'to' and call to->OnAttach().
    // If 'from' not found, delete 'to' to avoid leak.
    void LayerStack::ProcessPendingTransitions() {
        if (m_PendingTransitions.empty())
            return;

        for (auto& tr : m_PendingTransitions) {
            Layer* from = tr.from;
            Layer* to   = tr.to;

            auto it = std::find(m_Layers.begin(), m_Layers.end(), from);
            if (it != m_Layers.end()) {
                // detach and delete old
                from->OnDetach();
                delete from;

                // replace and attach new
                *it = to;
                if (to) to->OnAttach();
            } else {
                // not found -> cleanup new to avoid leak
                if (to) {
                    to->OnDetach();
                    delete to;
                }
            }
        }

        m_PendingTransitions.clear();
    }

} // namespace Nova::Core