#ifndef LAYER_H
#define LAYER_H

namespace Nova::Core {
    class Layer {
    public:
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnSuspend() {}
        virtual void OnResume() {}

        virtual void OnEvent(/*Event& event*/) {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnRender() {}
    };
}

#endif // LAYER_H