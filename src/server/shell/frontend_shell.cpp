/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
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
 * Authored By: Alan Griffiths <alan@octopull.co.uk>
 */

#include "frontend_shell.h"
#include "mir/frontend/session.h"

#include "mir/shell/persistent_surface_store.h"
#include "mir/shell/shell.h"

#include "mir/scene/surface.h"
#include "mir/scene/surface_creation_parameters.h"
#include "mir/scene/prompt_session.h"

#include <boost/throw_exception.hpp>

namespace mf = mir::frontend;
namespace ms = mir::scene;
namespace msh = mir::shell::detail;

std::shared_ptr<mf::Session> msh::FrontendShell::open_session(
    pid_t client_pid,
    std::string const& name,
    std::shared_ptr<mf::EventSink> const& sink)
{
    auto shell_session = wrapped->open_session(client_pid, name, sink);
    return std::dynamic_pointer_cast<frontend::Session>(shell_session);
}

void msh::FrontendShell::close_session(std::shared_ptr<mf::Session> const& session)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);

    wrapped->close_session(scene_session);
}

std::shared_ptr<mf::PromptSession> msh::FrontendShell::start_prompt_session_for(
    std::shared_ptr<mf::Session> const& session,
    ms::PromptSessionCreationParameters const& params)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);

    return wrapped->start_prompt_session_for(scene_session, params);
}

void msh::FrontendShell::add_prompt_provider_for(
    std::shared_ptr<mf::PromptSession> const& prompt_session,
    std::shared_ptr<mf::Session> const& session)
{
    auto const scene_prompt_session = std::dynamic_pointer_cast<ms::PromptSession>(prompt_session);
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    wrapped->add_prompt_provider_for(scene_prompt_session, scene_session);
}

void msh::FrontendShell::stop_prompt_session(std::shared_ptr<mf::PromptSession> const& prompt_session)
{
    auto const scene_prompt_session = std::dynamic_pointer_cast<ms::PromptSession>(prompt_session);
    wrapped->stop_prompt_session(scene_prompt_session);
}

mf::SurfaceId msh::FrontendShell::create_surface(
    std::shared_ptr<mf::Session> const& session,
    ms::SurfaceCreationParameters const& params,
    std::shared_ptr<mf::EventSink> const& sink)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);

    auto populated_params = params;

    // TODO: Fish out a policy verification object that enforces the various invariants
    //       in the surface spec requirements (eg: regular surface has no parent,
    //       dialog may have a parent, gloss must have a parent).
    if (populated_params.parent.lock() &&
        populated_params.type.value() != mir_window_type_inputmethod)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("Foreign parents may only be set on surfaces of type mir_window_type_inputmethod"));
    }

    if (populated_params.parent_id.is_set())
        populated_params.parent = scene_session->surface(populated_params.parent_id.value());

    return wrapped->create_surface(scene_session, populated_params, sink);
}

void msh::FrontendShell::modify_surface(std::shared_ptr<mf::Session> const& session, mf::SurfaceId surface_id, SurfaceSpecification const& modifications)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    auto const surface = scene_session->surface(surface_id);

    auto populated_modifications = modifications;

    if (populated_modifications.parent_id.is_set())
        populated_modifications.parent = scene_session->surface(populated_modifications.parent_id.value());

    wrapped->modify_surface(scene_session, surface, populated_modifications);
}

void msh::FrontendShell::destroy_surface(std::shared_ptr<mf::Session> const& session, mf::SurfaceId surface)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    wrapped->destroy_surface(scene_session, surface);
}

std::string msh::FrontendShell::persistent_id_for(std::shared_ptr<mf::Session> const& session, mf::SurfaceId surface_id)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    auto const surface = scene_session->surface(surface_id);

    return surface_store->id_for_surface(surface).serialize_to_string();
}

std::shared_ptr<ms::Surface> msh::FrontendShell::surface_for_id(std::string const& serialized_id)
{
    PersistentSurfaceStore::Id const id{serialized_id};
    return surface_store->surface_for_id(id);
}

int msh::FrontendShell::set_surface_attribute(
    std::shared_ptr<mf::Session> const& session,
    mf::SurfaceId surface_id,
    MirWindowAttrib attrib,
    int value)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    auto const surface = scene_session->surface(surface_id);
    return wrapped->set_surface_attribute(scene_session, surface, attrib, value);
}

int msh::FrontendShell::get_surface_attribute(
    std::shared_ptr<mf::Session> const& session,
    mf::SurfaceId surface_id,
    MirWindowAttrib attrib)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    auto const surface = scene_session->surface(surface_id);
    return wrapped->get_surface_attribute(surface, attrib);
}

void msh::FrontendShell::request_operation(
    std::shared_ptr<mf::Session> const& session,
    mf::SurfaceId surface_id,
    uint64_t timestamp,
    UserRequest request,
    optional_value <uint32_t> hint)
{
    auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
    auto const surface = scene_session->surface(surface_id);

    switch (request)
    {
    case UserRequest::activate:
    {
        auto const scene_session = std::dynamic_pointer_cast<ms::Session>(session);
        auto const surface = scene_session->surface(surface_id);
        wrapped->raise_surface(scene_session, surface, timestamp);
        break;
    }

    case UserRequest::drag_and_drop:
        wrapped->request_drag_and_drop(scene_session, surface, timestamp);
        break;

    case UserRequest::move:
        wrapped->request_move(scene_session, surface, timestamp);
        break;

    case UserRequest::resize:
        if (!hint.is_set())
            BOOST_THROW_EXCEPTION(std::logic_error("Resize request must identify edge(s)"));
        wrapped->request_resize(scene_session, surface, timestamp, MirResizeEdge(hint.value()));
        break;

    default:
        break;
    }
}
