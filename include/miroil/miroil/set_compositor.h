/*
 * Copyright © 2021 Canonical Ltd.
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
 */

#ifndef MIROIL_SET_COMPOSITOR_H
#define MIROIL_SET_COMPOSITOR_H
#include <memory>
#include <functional>

namespace mir { class Server; }
namespace mir { namespace graphics { class Display; } }
namespace mir { namespace compositor { class DisplayListener; } } 

namespace miroil
{
    class Compositor;
       
// Configure the server for using the Qt compositor
class SetCompositor
{
    using InitFunction = std::function<void(const std::shared_ptr<mir::graphics::Display>& display,
                       const std::shared_ptr<Compositor> & compositor,
                       const std::shared_ptr<mir::compositor::DisplayListener>& displayListener)>;
                       
    using ConstructorFunction = std::function<std::shared_ptr<Compositor>()>;    
    
public:
    SetCompositor(ConstructorFunction constructor, InitFunction init);
    
    void operator()(mir::Server& server);

private:
    struct CompositorImpl;
    
    std::weak_ptr<CompositorImpl> compositor_impl;
    ConstructorFunction           constructor_function;    
    InitFunction                  init_function;    
};

}

#endif //MIROIL_SET_COMPOSITOR_H
