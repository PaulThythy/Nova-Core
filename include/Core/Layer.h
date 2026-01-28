#ifndef LAYER_H
#define LAYER_H

#include <string>

#include "Events/Event.h"

using namespace Nova::Core::Events;

namespace Nova::Core {

    class Layer {
    public:
        Layer(const std::string& name = "Layer") : m_DebugName(name) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnSuspend() {}
        virtual void OnResume() {}
        virtual void OnImGuiRender() {}

        virtual void OnEvent(Event& event) {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnBegin() {}
        virtual void OnRender() {}
        virtual void OnEnd() {}

    protected:
        std::string m_DebugName;
    };
}

#endif // LAYER_H