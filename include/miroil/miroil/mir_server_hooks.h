/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#ifndef MIROIL_MIRSERVERHOOKS_H
#define MIROIL_MIRSERVERHOOKS_H
#include <miroil/input_device_observer.h>
#include <miroil/prompt_session_listener.h>
#include <mir/graphics/cursor_image.h>
#include <functional>
#include <memory>

namespace mir { class Server; }
namespace mir { namespace scene { class PromptSessionManager; }}
namespace mir { namespace graphics { class Display; }}
namespace mir { namespace shell { class DisplayConfigurationController; } }

namespace miroil
{
using CreateNamedCursor = std::function<std::shared_ptr< mir::graphics::CursorImage>(std::string const& name)>;        
    
class MirServerHooks
{
public:
    MirServerHooks();

    void operator()(mir::Server& server);

    auto the_prompt_session_listener() const -> PromptSessionListener*;
    auto the_prompt_session_manager() const -> std::shared_ptr<mir::scene::PromptSessionManager>;
    auto the_mir_display() const -> std::shared_ptr<mir::graphics::Display>;
    auto the_display_configuration_controller() const -> std::shared_ptr<mir::shell::DisplayConfigurationController>;
    void create_named_cursor(CreateNamedCursor func);
    void create_input_device_observer(std::shared_ptr<InputDeviceObserver> & observer);
    void create_prompt_session_listener(std::shared_ptr<PromptSessionListener> listener);

private:
    struct Self;
    std::shared_ptr<Self> self;
};
}

#endif //MIROIL_MIRSERVERHOOKS_H
