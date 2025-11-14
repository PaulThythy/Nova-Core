#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include <SDL3/SDL.h>

#include "Core/Layer.h"
#include "Core/Window.h"
#include "Core/GraphicsAPI.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
//#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdlrenderer3.h"

namespace Nova::Core {

    class ImGuiLayer : public Layer {
    public:
        ImGuiLayer(Window& window, GraphicsAPI api);
        ~ImGuiLayer() = default;

        virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(/*Event& e*/) override {}

        void Begin();
        void End();

        void BlockEvents(bool block) { m_BlockEvents = block; }

        void ProcessSDLEvent(const SDL_Event& e);

    private:
        bool m_BlockEvents = true;
        Window& m_Window;
        GraphicsAPI m_GraphicsAPI;
    };

} // namespace Nova::Core

#endif // IMGUI_LAYER_H