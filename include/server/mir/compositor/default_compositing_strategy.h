/*
 * Copyright © 2012 Canonical Ltd.
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

#ifndef MIR_COMPOSITOR_DEFAULT_COMPOSITING_STRATEGY_H_
#define MIR_COMPOSITOR_DEFAULT_COMPOSITING_STRATEGY_H_

#include "mir/compositor/basic_compositing_strategy.h"

#include <memory>

namespace mir
{
///  Compositing. Combining renderables into a display image.
namespace compositor
{
class OverlayRenderer;
class Scene;
class Renderer;

class DefaultCompositingStrategy : public BasicCompositingStrategy
{
public:
    DefaultCompositingStrategy(
        std::shared_ptr<Scene> const& scene,
        std::shared_ptr<Renderer> const& renderer,
        std::shared_ptr<OverlayRenderer> const& overlay_renderer);

    void render(graphics::DisplayBuffer& display_buffer);
    void compose(
        mir::geometry::Rectangle const& view_area,
        std::function<void(std::shared_ptr<void> const&)> save_resource);

private:
    std::shared_ptr<Scene> const scene;
    std::shared_ptr<Renderer> const renderer;
    std::shared_ptr<OverlayRenderer> const overlay_renderer;
};

}
}

#endif /* MIR_COMPOSITOR_DEFAULT_COMPOSITING_STRATEGY_H_ */
