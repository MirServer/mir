/*
 * Copyright © 2016-2021 Canonical Ltd.
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

#ifndef MIROIL_PROMPT_SESSION_LISTENER_H
#define MIROIL_PROMPT_SESSION_LISTENER_H
#include <memory>

namespace mir { namespace scene { class PromptSession; } }
namespace mir { namespace scene { class Session; } }

namespace miroil {
    
class PromptSessionListener
{
public:
    virtual ~PromptSessionListener();
    
    PromptSessionListener& operator=(PromptSessionListener const&) = delete;    

    virtual void starting(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) = 0;
    virtual void stopping(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) = 0;
    virtual void suspending(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) = 0;
    virtual void resuming(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) = 0;
    virtual void prompt_provider_added(mir::scene::PromptSession const& prompt_session,
                               std::shared_ptr<mir::scene::Session> const& prompt_provider) = 0;
    virtual void prompt_provider_removed(mir::scene::PromptSession const& prompt_session,
                                 std::shared_ptr<mir::scene::Session> const& prompt_provider) = 0;
                                 
protected:
    PromptSessionListener() = default;
    PromptSessionListener(PromptSessionListener const&) = delete;
};

}

#endif // MIROIL_PROMPT_SESSION_LISTENER_H
